// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/udp.h>

#define PORT 5000
#define BUF_SIZE 256

#define UDP_REPAIR	2

char send_buf[BUF_SIZE];
struct udp_dump {
	union {
		struct sockaddr_in addr_v4;
		struct sockaddr_in6 addr_v6;
	};
	char buf[BUF_SIZE];
};

struct sockaddr_in addr_v4;
struct sockaddr_in6 addr_v6;

int udp_server(int is_udp4)
{
	int sock, ret;
	unsigned short family;
	struct sockaddr *server_addr;
	unsigned int addr_len;

	if (is_udp4) {
		family = AF_INET;
		server_addr = (struct sockaddr *) &addr_v4;
		addr_len = sizeof(addr_v4);
	} else {
		family = AF_INET6;
		server_addr = (struct sockaddr *) &addr_v6;
		addr_len = sizeof(addr_v6);
	}

	sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		error(1, errno, "socket server");

	ret = bind(sock, server_addr, addr_len);
	if (ret < 0)
		error(1, errno, "bind server socket");

	return sock;
}

void server_recv(int sock)
{
	char recv_buf[BUF_SIZE];
	int ret;

	ret = recv(sock, recv_buf, sizeof(recv_buf), 0);
	if (ret < 0)
		error(1, errno, "recv in server");

	if (memcmp(recv_buf, send_buf, BUF_SIZE))
		error(1, 0, "recv: data mismatch");
}

int create_corked_udp_client(int is_udp4)
{
	int sock, ret, val = 1;
	unsigned short family = is_udp4 ? AF_INET : AF_INET6;

	sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		error(1, errno, "socket client");

	ret = setsockopt(sock, SOL_UDP, UDP_CORK, &val, sizeof(val));
	if (ret < 0)
		error(1, errno, "setsockopt cork udp");

	return sock;
}

struct udp_dump *checkpoint(int sock, int is_udp4)
{
	int ret, val;
	unsigned int addr_len;
	struct udp_dump *dump;
	struct sockaddr *addr;

	dump = malloc(sizeof(*dump));
	if (!dump)
		error(1, 0, "malloc");

	if (is_udp4) {
		addr = (struct sockaddr *) &dump->addr_v4;
		addr_len = sizeof(dump->addr_v4);
	} else {
		addr = (struct sockaddr *) &dump->addr_v6;
		addr_len = sizeof(dump->addr_v6);
	}

	val = 1;
	ret = setsockopt(sock, SOL_UDP, UDP_REPAIR, &val, sizeof(val));
	if (ret < 0)
		error(1, errno, "setsockopt udp_repair");

	val = 0;
	ret = setsockopt(sock, SOL_SOCKET, SO_PEEK_OFF, &val, sizeof(val));
	if (ret < 0)
		error(1, errno, "setsockopt so_peek_off");

	ret = recvfrom(sock, dump->buf, BUF_SIZE / 2, MSG_PEEK,
		       addr, &addr_len);
	if (ret < 0)
		error(1, errno, "dumping send queue");

	ret = recvfrom(sock, dump->buf + BUF_SIZE / 2,
		       BUF_SIZE - BUF_SIZE / 2, MSG_PEEK,
		       addr, &addr_len);
	if (ret < 0)
		error(1, errno, "dumping send queue");

	if (memcmp(dump->buf, send_buf, BUF_SIZE))
		error(1, 0, "dump: data mismatch");

	return dump;
}

void restore(int sock, struct udp_dump *dump, int is_udp4)
{
	struct sockaddr *addr;
	int val;
	unsigned int addr_len;

	if (is_udp4) {
		addr = (struct sockaddr *) &dump->addr_v4;
		addr_len = sizeof(dump->addr_v4);
	} else {
		addr = (struct sockaddr *) &dump->addr_v6;
		addr_len = sizeof(dump->addr_v6);
	}

	if (sendto(sock, dump->buf, BUF_SIZE, 0, addr, addr_len) < 0)
		error(1, errno, "send data");

	val = 0;
	if (setsockopt(sock, SOL_UDP, UDP_CORK, &val, sizeof(val)) < 0)
		error(1, errno, "setsockopt un-cork udp");
}

void run_test(int is_udp4_sock, int is_udp4_packet)
{
	int server_sock, client_sock, ret, val;
	struct udp_dump *dump;
	struct sockaddr *addr;
	unsigned int addr_len;

	if (is_udp4_packet) {
		addr = (struct sockaddr *) &addr_v4;
		addr_len = sizeof(addr_v4);
	} else {
		addr = (struct sockaddr *) &addr_v6;
		addr_len = sizeof(addr_v6);
	}

	server_sock = udp_server(is_udp4_packet);
	client_sock = create_corked_udp_client(is_udp4_sock);

	ret = sendto(client_sock, send_buf, sizeof(send_buf), 0,
	       addr, addr_len);
	if (ret < 0)
		error(1, errno, "send data");

	dump = checkpoint(client_sock, is_udp4_sock);
	close(client_sock);

	client_sock = create_corked_udp_client(is_udp4_sock);
	restore(client_sock, dump, is_udp4_sock);

	val = 0;
	setsockopt(client_sock, SOL_UDP, UDP_CORK, &val, sizeof(val));
	server_recv(server_sock);

	close(server_sock);
	close(client_sock);
}

void init(void)
{
	addr_v4.sin_family	= AF_INET;
	addr_v4.sin_port	= htons(PORT);
	addr_v4.sin_addr.s_addr	= inet_addr("127.0.0.1");

	addr_v6.sin6_family	= AF_INET6;
	addr_v6.sin6_port	= htons(PORT);
	inet_pton(AF_INET6, "::1", &addr_v6.sin6_addr);

	memset(send_buf, 'A', BUF_SIZE / 2);
	memset(send_buf + BUF_SIZE / 2, 'B', BUF_SIZE - BUF_SIZE / 2);
}

int main(void)
{
	init();
	fprintf(stderr, "Test udp4 socket\n");
	run_test(1, 1);
	fprintf(stderr, "Test udp6 socket sending udp4 packet\n");
	run_test(0, 1);
	fprintf(stderr, "Test udp6 socket sending udp6 packet\n");
	run_test(0, 0);
	fprintf(stderr, "Ok\n");
	return 0;
}
