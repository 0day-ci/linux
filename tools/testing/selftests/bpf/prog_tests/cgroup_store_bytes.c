// SPDX-License-Identifier: GPL-2.0-only

#include <test_progs.h>
#include <network_helpers.h>

void test_cgroup_store_bytes(void)
{
	int server_fd, cgroup_fd, prog_fd, map_fd, client_fd;
	int err;
	struct bpf_object *obj;
	struct bpf_program *prog;
	struct bpf_map *test_result;
	__u32 duration = 0;

	__u32 map_key = 0;
	__u32 map_value = 0;

	cgroup_fd = test__join_cgroup("/cgroup_store_bytes");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	server_fd = start_server(AF_INET, SOCK_DGRAM, NULL, 0, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;

	err = bpf_prog_load("./cgroup_store_bytes.o", BPF_PROG_TYPE_CGROUP_SKB,
						&obj, &prog_fd);

	if (CHECK_FAIL(err))
		goto close_server_fd;

	test_result = bpf_object__find_map_by_name(obj, "test_result");
	if (CHECK_FAIL(!test_result))
		goto close_bpf_object;

	map_fd = bpf_map__fd(test_result);
	if (map_fd < 0)
		goto close_bpf_object;

	prog = bpf_object__find_program_by_name(obj, "cgroup_store_bytes");
	if (CHECK_FAIL(!prog))
		goto close_bpf_object;

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_INET_EGRESS,
							BPF_F_ALLOW_MULTI);
	if (CHECK_FAIL(err))
		goto close_bpf_object;

	client_fd = start_server(AF_INET, SOCK_DGRAM, NULL, 0, 0);
	if (CHECK_FAIL(client_fd < 0))
		goto close_bpf_object;

	struct sockaddr server_addr;
	socklen_t addrlen = sizeof(server_addr);

	if (getsockname(server_fd, &server_addr, &addrlen)) {
		perror("Failed to get server addr");
		return -1;
	}

	char buf[] = "testing";

	if (CHECK_FAIL(sendto(client_fd, buf, sizeof(buf), 0, &server_addr,
			sizeof(server_addr)) != sizeof(buf))) {
		perror("Can't write on client");
		goto close_client_fd;
	}

	struct sockaddr_storage ss;
	char recv_buf[BUFSIZ];
	socklen_t slen;

	if (recvfrom(server_fd, &recv_buf, sizeof(recv_buf), 0,
			(struct sockaddr *)&ss, &slen) <= 0) {
		perror("Recvfrom received no packets");
		goto close_client_fd;
	}

	struct in_addr addr = ((struct sockaddr_in *)&ss)->sin_addr;

	CHECK(addr.s_addr != 0xac100164, "bpf", "bpf program failed to change saddr");

	unsigned short port = ((struct sockaddr_in *)&ss)->sin_port;

	CHECK(port != htons(5555), "bpf", "bpf program failed to change port");

	err = bpf_map_lookup_elem(map_fd, &map_key, &map_value);
	if (CHECK_FAIL(err))
		goto close_client_fd;

	CHECK(map_value != 1, "bpf", "bpf program returned failure");

close_client_fd:
	close(client_fd);

close_bpf_object:
	bpf_object__close(obj);

close_server_fd:
	close(server_fd);

close_cgroup_fd:
	close(cgroup_fd);
}
