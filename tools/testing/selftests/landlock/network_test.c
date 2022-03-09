// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Network
 *
 * Copyright (C) 2022 Huawei Tech. Co., Ltd.
 * Author: Konstantin Meskhidze <konstantin.meskhidze@huawei.com>
 *
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "common.h"

#define MAX_SOCKET_NUM 10

#define SOCK_PORT_START 3470
#define SOCK_PORT_ADD 10

#define IP_ADDRESS "127.0.0.1"

uint port[MAX_SOCKET_NUM];
struct sockaddr_in addr[MAX_SOCKET_NUM];

const int one = 1;

/* Number pending connections queue to be hold */
#define BACKLOG 10

static int create_socket(struct __test_metadata *const _metadata)
{

		int sockfd;

		sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		ASSERT_LE(0, sockfd);
		/* Allows to reuse of local address */
		ASSERT_EQ(0, setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)));

		return sockfd;
}

static void enforce_ruleset(struct __test_metadata *const _metadata,
		const int ruleset_fd)
{
	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, landlock_restrict_self(ruleset_fd, 0)) {
		TH_LOG("Failed to enforce ruleset: %s", strerror(errno));
	}
}

FIXTURE(socket) { };

FIXTURE_SETUP(socket)
{
	int i;
	/* Creates socket addresses */
	for (i = 0; i < MAX_SOCKET_NUM; i++) {
		port[i] = SOCK_PORT_START + SOCK_PORT_ADD*i;
		addr[i].sin_family = AF_INET;
		addr[i].sin_port = htons(port[i]);
		addr[i].sin_addr.s_addr = inet_addr(IP_ADDRESS);
		memset(&(addr[i].sin_zero), '\0', 8);
	}
}

FIXTURE_TEARDOWN(socket)
{ }

TEST_F_FORK(socket, bind_no_restrictions) {

	int sockfd;

	sockfd = create_socket(_metadata);
	ASSERT_LE(0, sockfd);

	/* Binds a socket to port[0] */
	ASSERT_EQ(0, bind(sockfd, (struct sockaddr *)&addr[0], sizeof(addr[0])));

	ASSERT_EQ(0, close(sockfd));
}

TEST_F_FORK(socket, bind_with_restrictions) {

	int sockfd_1, sockfd_2, sockfd_3;

	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
				      LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};
	struct landlock_net_service_attr net_service_1 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
				  LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = port[0],
	};
	struct landlock_net_service_attr net_service_2 = {
		.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = port[1],
	};
	struct landlock_net_service_attr net_service_3 = {
		.allowed_access = 0,
		.port = port[2],
	};

	const int ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	/* Allows connect and bind operations to the port[0] socket. */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_SERVICE,
				&net_service_1, 0));
	/* Allows connect and deny bind operations to the port[1] socket. */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_SERVICE,
				&net_service_2, 0));
	/* Empty allowed_access (i.e. deny rules) are ignored in network actions
	 * for port[2] socket.
	 */
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_SERVICE,
				&net_service_3, 0));
	ASSERT_EQ(ENOMSG, errno);

	/* Enforces the ruleset. */
	enforce_ruleset(_metadata, ruleset_fd);

	sockfd_1 = create_socket(_metadata);
	ASSERT_LE(0, sockfd_1);
	/* Binds a socket to port[0] */
	ASSERT_EQ(0, bind(sockfd_1, (struct sockaddr  *)&addr[0], sizeof(addr[0])));

	/* Close bounded socket*/
	ASSERT_EQ(0, close(sockfd_1));

	sockfd_2 = create_socket(_metadata);
	ASSERT_LE(0, sockfd_2);
	/* Binds a socket to port[1] */
	ASSERT_EQ(-1, bind(sockfd_2, (struct sockaddr *)&addr[1], sizeof(addr[1])));
	ASSERT_EQ(EACCES, errno);

	sockfd_3 = create_socket(_metadata);
	ASSERT_LE(0, sockfd_3);
	/* Binds a socket to port[2] */
	ASSERT_EQ(-1, bind(sockfd_3, (struct sockaddr *)&addr[2], sizeof(addr[2])));
	ASSERT_EQ(EACCES, errno);
}

TEST_F_FORK(socket, connect_no_restrictions) {

	int sockfd, new_fd;
	pid_t child;
	int status;

	/* Creates a server socket */
	sockfd = create_socket(_metadata);
	ASSERT_LE(0, sockfd);

	/* Binds a socket to port[0] */
	ASSERT_EQ(0, bind(sockfd, (struct sockaddr *)&addr[0], sizeof(addr[0])));

	/* Makes listening socket */
	ASSERT_EQ(0, listen(sockfd, BACKLOG));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int child_sockfd;

		/* Closes listening socket for the child */
		ASSERT_EQ(0, close(sockfd));
		/* Create a stream client socket */
		child_sockfd = create_socket(_metadata);
		ASSERT_LE(0, child_sockfd);

		/* Makes connection to the listening socket */
		ASSERT_EQ(0, connect(child_sockfd, (struct sockaddr *)&addr[0],
						   sizeof(addr[0])));
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}
	/* Accepts connection from the child */
	new_fd = accept(sockfd, NULL, 0);
	ASSERT_LE(0, new_fd);

	/* Closes connection */
	ASSERT_EQ(0, close(new_fd));

	/* Closes listening socket for the parent*/
	ASSERT_EQ(0, close(sockfd));

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

TEST_F_FORK(socket, connect_with_restrictions) {

	int new_fd;
	int sockfd_1, sockfd_2;
	pid_t child_1, child_2;
	int status;

	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
				      LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};
	struct landlock_net_service_attr net_service_1 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP |
				  LANDLOCK_ACCESS_NET_CONNECT_TCP,
		.port = port[0],
	};
	struct landlock_net_service_attr net_service_2 = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = port[1],
	};

	const int ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	/* Allows connect and bind operations to the port[0] socket */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_SERVICE,
				&net_service_1, 0));
	/* Allows connect and deny bind operations to the port[1] socket */
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_SERVICE,
				&net_service_2, 0));

	/* Enforces the ruleset. */
	enforce_ruleset(_metadata, ruleset_fd);

	/* Creates a server socket 1 */
	sockfd_1 = create_socket(_metadata);
	ASSERT_LE(0, sockfd_1);

	/* Binds the socket 1 to address with port[0] */
	ASSERT_EQ(0, bind(sockfd_1, (struct sockaddr *)&addr[0], sizeof(addr[0])));

	/* Makes listening socket 1 */
	ASSERT_EQ(0, listen(sockfd_1, BACKLOG));

	child_1 = fork();
	ASSERT_LE(0, child_1);
	if (child_1 == 0) {
		int child_sockfd;

		/* Closes listening socket for the child */
		ASSERT_EQ(0, close(sockfd_1));
		/* Creates a stream client socket */
		child_sockfd = create_socket(_metadata);
		ASSERT_LE(0, child_sockfd);

		/* Makes connection to the listening socket */
		ASSERT_EQ(0, connect(child_sockfd, (struct sockaddr *)&addr[0],
						   sizeof(addr[0])));
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}
	/* Accepts connection from the child 1 */
	new_fd = accept(sockfd_1, NULL, 0);
	ASSERT_LE(0, new_fd);

	/* Closes connection */
	ASSERT_EQ(0, close(new_fd));

	/* Closes listening socket 1 for the parent*/
	ASSERT_EQ(0, close(sockfd_1));

	ASSERT_EQ(child_1, waitpid(child_1, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

	/* Creates a server socket 2 */
	sockfd_2 = create_socket(_metadata);
	ASSERT_LE(0, sockfd_2);

	/* Binds the socket 2 to address with port[1] */
	ASSERT_EQ(0, bind(sockfd_2, (struct sockaddr *)&addr[1], sizeof(addr[1])));

	/* Makes listening socket 2 */
	ASSERT_EQ(0, listen(sockfd_2, BACKLOG));

	child_2 = fork();
	ASSERT_LE(0, child_2);
	if (child_2 == 0) {
		int child_sockfd;

		/* Closes listening socket for the child */
		ASSERT_EQ(0, close(sockfd_2));
		/* Creates a stream client socket */
		child_sockfd = create_socket(_metadata);
		ASSERT_LE(0, child_sockfd);

		/* Makes connection to the listening socket */
		ASSERT_EQ(-1, connect(child_sockfd, (struct sockaddr *)&addr[1],
						   sizeof(addr[1])));
		ASSERT_EQ(EACCES, errno);
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}

	/* Closes listening socket 2 for the parent*/
	ASSERT_EQ(0, close(sockfd_2));

	ASSERT_EQ(child_2, waitpid(child_2, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

TEST_HARNESS_MAIN
