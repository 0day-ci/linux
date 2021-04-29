// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * This test sets up 3 netns (src <-> fwd <-> dst). There is no direct veth link
 * between src and dst. The netns fwd has veth links to each src and dst. The
 * client is in src and server in dst. The test installs a TC BPF program to each
 * host facing veth in fwd which calls into i) bpf_redirect_neigh() to perform the
 * neigh addr population and redirect or ii) bpf_redirect_peer() for namespace
 * switch from ingress side; it also installs a checker prog on the egress side
 * to drop unexpected traffic.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "test_progs.h"
#include "network_helpers.h"
#include "test_tc_neigh_fib.skel.h"
#include "test_tc_neigh.skel.h"
#include "test_tc_peer.skel.h"

#define NS_SRC "ns_src"
#define NS_FWD "ns_fwd"
#define NS_DST "ns_dst"

#define IP4_SRC "172.16.1.100"
#define IP4_DST "172.16.2.100"
#define IP4_PORT 9004

#define IP6_SRC "::1:dead:beef:cafe"
#define IP6_DST "::2:dead:beef:cafe"
#define IP6_PORT 9006

#define IP4_SLL "169.254.0.1"
#define IP4_DLL "169.254.0.2"
#define IP4_NET "169.254.0.0"

#define IFADDR_STR_LEN 18
#define PING_ARGS "-c 3 -w 10 -q"

#define SRC_PROG_PIN_FILE "/sys/fs/bpf/test_tc_src"
#define DST_PROG_PIN_FILE "/sys/fs/bpf/test_tc_dst"
#define CHK_PROG_PIN_FILE "/sys/fs/bpf/test_tc_chk"

#define TIMEOUT_MILLIS 10000

static const char * const namespaces[] = {NS_SRC, NS_FWD, NS_DST, NULL};
static int root_netns_fd = -1;
static __u32 duration;

static void restore_root_netns(void)
{
	CHECK_FAIL(setns(root_netns_fd, CLONE_NEWNET));
}

int setns_by_name(const char *name)
{
	int nsfd;
	char nspath[PATH_MAX];
	int err;

	snprintf(nspath, sizeof(nspath), "%s/%s", "/var/run/netns", name);
	nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
	if (CHECK(nsfd < 0, nspath, "failed to open\n"))
		return -EINVAL;

	err = setns(nsfd, CLONE_NEWNET);
	close(nsfd);

	if (CHECK(err, name, "failed to setns\n"))
		return -1;

	return 0;
}

static int netns_setup_namespaces(const char *verb)
{
	const char * const *ns = namespaces;
	char cmd[128];

	while (*ns) {
		snprintf(cmd, sizeof(cmd), "ip netns %s %s", verb, *ns);
		if (CHECK(system(cmd), cmd, "failed\n"))
			return -1;
		ns++;
	}
	return 0;
}

struct netns_setup_result {
	int ifindex_veth_src_fwd;
	int ifindex_veth_dst_fwd;
};

static int get_ifaddr(const char *name, char *ifaddr)
{
	char path[PATH_MAX];
	FILE *f;

	snprintf(path, PATH_MAX, "/sys/class/net/%s/address", name);
	f = fopen(path, "r");
	if (CHECK(!f, path, "failed to open\n"))
		return -1;

	if (CHECK_FAIL(fread(ifaddr, 1, IFADDR_STR_LEN, f) != IFADDR_STR_LEN)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int get_ifindex(const char *name)
{
	char path[PATH_MAX];
	char buf[32];
	FILE *f;

	snprintf(path, PATH_MAX, "/sys/class/net/%s/ifindex", name);
	f = fopen(path, "r");
	if (CHECK(!f, path, "failed to open\n"))
		return -1;

	if (CHECK_FAIL(fread(buf, 1, sizeof(buf), f) <= 0)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return atoi(buf);
}

#define SYS(fmt, ...)						\
	({							\
		char cmd[1024];					\
		snprintf(cmd, sizeof(cmd), fmt, ##__VA_ARGS__);	\
		if (CHECK(system(cmd), cmd, "failed\n"))	\
			goto fail;				\
	})

static int netns_setup_links_and_routes(struct netns_setup_result *result)
{
	char veth_src_fwd_addr[IFADDR_STR_LEN+1] = {0,};
	char veth_dst_fwd_addr[IFADDR_STR_LEN+1] = {0,};

	SYS("ip link add veth_src type veth peer name veth_src_fwd");
	SYS("ip link add veth_dst type veth peer name veth_dst_fwd");
	if (CHECK_FAIL(get_ifaddr("veth_src_fwd", veth_src_fwd_addr)))
		goto fail;
	if (CHECK_FAIL(get_ifaddr("veth_dst_fwd", veth_dst_fwd_addr)))
		goto fail;

	result->ifindex_veth_src_fwd = get_ifindex("veth_src_fwd");
	if (CHECK_FAIL(result->ifindex_veth_src_fwd < 0))
		goto fail;
	result->ifindex_veth_dst_fwd = get_ifindex("veth_dst_fwd");
	if (CHECK_FAIL(result->ifindex_veth_dst_fwd < 0))
		goto fail;

	SYS("ip link set veth_src netns " NS_SRC);
	SYS("ip link set veth_src_fwd netns " NS_FWD);
	SYS("ip link set veth_dst_fwd netns " NS_FWD);
	SYS("ip link set veth_dst netns " NS_DST);

	/** setup in 'src' namespace */
	if (setns_by_name(NS_SRC))
		goto fail;

	SYS("ip addr add " IP4_SRC "/32 dev veth_src");
	SYS("ip addr add " IP6_SRC "/128 dev veth_src nodad");
	SYS("ip link set dev veth_src up");

	SYS("ip route add " IP4_DST "/32 dev veth_src scope global");
	SYS("ip route add " IP4_NET "/16 dev veth_src scope global");
	SYS("ip route add " IP6_DST "/128 dev veth_src scope global");

	SYS("ip neigh add " IP4_DST " dev veth_src lladdr %s",
	    veth_src_fwd_addr);
	SYS("ip neigh add " IP6_DST " dev veth_src lladdr %s",
	    veth_src_fwd_addr);

	/** setup in 'fwd' namespace */
	if (setns_by_name(NS_FWD))
		goto fail;

	/* The fwd netns automatically gets a v6 LL address / routes, but also
	 * needs v4 one in order to start ARP probing. IP4_NET route is added
	 * to the endpoints so that the ARP processing will reply.
	 */
	SYS("ip addr add " IP4_SLL "/32 dev veth_src_fwd");
	SYS("ip addr add " IP4_DLL "/32 dev veth_dst_fwd");
	SYS("ip link set dev veth_src_fwd up");
	SYS("ip link set dev veth_dst_fwd up");

	SYS("ip route add " IP4_SRC "/32 dev veth_src_fwd scope global");
	SYS("ip route add " IP6_SRC "/128 dev veth_src_fwd scope global");
	SYS("ip route add " IP4_DST "/32 dev veth_dst_fwd scope global");
	SYS("ip route add " IP6_DST "/128 dev veth_dst_fwd scope global");

	/** setup in 'dst' namespace */
	if (setns_by_name(NS_DST))
		goto fail;

	SYS("ip addr add " IP4_DST "/32 dev veth_dst");
	SYS("ip addr add " IP6_DST "/128 dev veth_dst nodad");
	SYS("ip link set dev veth_dst up");

	SYS("ip route add " IP4_SRC "/32 dev veth_dst scope global");
	SYS("ip route add " IP4_NET "/16 dev veth_dst scope global");
	SYS("ip route add " IP6_SRC "/128 dev veth_dst scope global");

	SYS("ip neigh add " IP4_SRC " dev veth_dst lladdr %s",
	    veth_dst_fwd_addr);
	SYS("ip neigh add " IP6_SRC " dev veth_dst lladdr %s",
	    veth_dst_fwd_addr);

	restore_root_netns();
	return 0;
fail:
	restore_root_netns();
	return -1;
}

static int netns_load_bpf(void)
{
	if (setns_by_name(NS_FWD))
		return -1;

	SYS("tc qdisc add dev veth_src_fwd clsact");
	SYS("tc filter add dev veth_src_fwd ingress bpf da object-pinned "
	    SRC_PROG_PIN_FILE);
	SYS("tc filter add dev veth_src_fwd egress bpf da object-pinned "
	    CHK_PROG_PIN_FILE);

	SYS("tc qdisc add dev veth_dst_fwd clsact");
	SYS("tc filter add dev veth_dst_fwd ingress bpf da object-pinned "
	    DST_PROG_PIN_FILE);
	SYS("tc filter add dev veth_dst_fwd egress bpf da object-pinned "
	    CHK_PROG_PIN_FILE);

	restore_root_netns();
	return -1;
fail:
	restore_root_netns();
	return -1;
}

static int netns_unload_bpf(void)
{
	if (setns_by_name(NS_FWD))
		goto fail;
	SYS("tc qdisc delete dev veth_src_fwd clsact");
	SYS("tc qdisc delete dev veth_dst_fwd clsact");

	restore_root_netns();
	return 0;
fail:
	restore_root_netns();
	return -1;
}


static void test_tcp(int family, const char *addr, __u16 port)
{
	int listen_fd = -1, accept_fd = -1, client_fd = -1;
	char buf[] = "testing testing";

	if (setns_by_name(NS_DST))
		return;

	listen_fd = start_server(family, SOCK_STREAM, addr, port, 0);
	if (CHECK_FAIL(listen_fd == -1))
		goto done;

	if (setns_by_name(NS_SRC))
		goto done;

	client_fd = connect_to_fd(listen_fd, TIMEOUT_MILLIS);
	if (CHECK_FAIL(client_fd < 0))
		goto done;

	accept_fd = accept(listen_fd, NULL, NULL);
	if (CHECK_FAIL(accept_fd < 0))
		goto done;

	if (CHECK_FAIL(settimeo(accept_fd, TIMEOUT_MILLIS)))
		goto done;

	if (CHECK_FAIL(write(client_fd, buf, sizeof(buf)) != sizeof(buf)))
		goto done;

	if (CHECK_FAIL(read(accept_fd, buf, sizeof(buf)) != sizeof(buf)))
		goto done;

done:
	restore_root_netns();
	if (listen_fd >= 0)
		close(listen_fd);
	if (accept_fd >= 0)
		close(accept_fd);
	if (client_fd >= 0)
		close(client_fd);
}

static int test_ping(int family, const char *addr)
{
	const char *ping = family == AF_INET6 ? "ping6" : "ping";

	SYS("ip netns exec " NS_SRC " %s " PING_ARGS " %s", ping, addr);
	return 0;
fail:
	return -1;
}

#undef SYS

static void test_connectivity(void)
{
	test_tcp(AF_INET, IP4_DST, IP4_PORT);
	test_ping(AF_INET, IP4_DST);
	test_tcp(AF_INET6, IP6_DST, IP6_PORT);
	test_ping(AF_INET6, IP6_DST);
}

#define CHECK_PIN_PROG(prog, pin_file) \
	CHECK(bpf_program__pin((prog), (pin_file)), "bpf_program__pin", \
	  "cannot pin bpf prog to %s\n", (pin_file))

void test_tc_redirect_neigh_fib(struct netns_setup_result *setup_result)
{
	struct test_tc_neigh_fib *skel;

	skel = test_tc_neigh_fib__open();
	if (CHECK(!skel, "test_tc_neigh_fib__open", "failed\n"))
		return;

	if (CHECK(test_tc_neigh_fib__load(skel),
		  "test_tc_neigh_fib__load", "failed\n")) {
		test_tc_neigh_fib__destroy(skel);
		return;
	}

	if (CHECK_PIN_PROG(skel->progs.tc_src, SRC_PROG_PIN_FILE))
		goto done;
	if (CHECK_PIN_PROG(skel->progs.tc_chk, CHK_PROG_PIN_FILE))
		goto done;
	if (CHECK_PIN_PROG(skel->progs.tc_dst, DST_PROG_PIN_FILE))
		goto done;

	if (netns_load_bpf())
		goto done;

	/* bpf_fib_lookup() checks if forwarding is enabled */
	system("ip netns exec " NS_FWD " sysctl -q -w "
	       "net.ipv4.ip_forward=1 "
	       "net.ipv6.conf.veth_src_fwd.forwarding=1 "
	       "net.ipv6.conf.veth_dst_fwd.forwarding=1");

	test_connectivity();
done:
	system("ip netns exec " NS_FWD " sysctl -q -w "
	       "net.ipv4.ip_forward=0 "
	       "net.ipv6.conf.veth_src_fwd.forwarding=0 "
	       "net.ipv6.conf.veth_dst_fwd.forwarding=0");

	bpf_program__unpin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	test_tc_neigh_fib__destroy(skel);
	netns_unload_bpf();
	restore_root_netns();
}

void test_tc_redirect_neigh(struct netns_setup_result *setup_result)
{
	struct test_tc_neigh *skel;

	skel = test_tc_neigh__open();
	if (CHECK(!skel, "test_tc_neigh__open", "failed\n"))
		return;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_veth_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	if (CHECK(test_tc_neigh__load(skel),
		  "test_tc_neigh__load", "failed\n")) {
		test_tc_neigh__destroy(skel);
		return;
	}

	if (CHECK_PIN_PROG(skel->progs.tc_src, SRC_PROG_PIN_FILE))
		goto done;
	if (CHECK_PIN_PROG(skel->progs.tc_chk, CHK_PROG_PIN_FILE))
		goto done;
	if (CHECK_PIN_PROG(skel->progs.tc_dst, DST_PROG_PIN_FILE))
		goto done;

	if (netns_load_bpf())
		goto done;

	test_connectivity();

done:
	bpf_program__unpin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	test_tc_neigh__destroy(skel);
	netns_unload_bpf();
	restore_root_netns();
}

void test_tc_redirect_peer(struct netns_setup_result *setup_result)
{
	struct test_tc_peer *skel;

	skel = test_tc_peer__open();
	if (CHECK(!skel, "test_tc_peer__open", "failed\n"))
		return;

	skel->rodata->IFINDEX_SRC = setup_result->ifindex_veth_src_fwd;
	skel->rodata->IFINDEX_DST = setup_result->ifindex_veth_dst_fwd;

	if (CHECK(test_tc_peer__load(skel),
		  "test_tc_peer__load", "failed\n")) {
		test_tc_peer__destroy(skel);
		return;
	}

	if (CHECK_PIN_PROG(skel->progs.tc_src, SRC_PROG_PIN_FILE))
		goto done;
	if (CHECK_PIN_PROG(skel->progs.tc_chk, CHK_PROG_PIN_FILE))
		goto done;
	if (CHECK_PIN_PROG(skel->progs.tc_dst, DST_PROG_PIN_FILE))
		goto done;

	if (netns_load_bpf())
		goto done;

	test_connectivity();

done:
	bpf_program__unpin(skel->progs.tc_src, SRC_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_chk, CHK_PROG_PIN_FILE);
	bpf_program__unpin(skel->progs.tc_dst, DST_PROG_PIN_FILE);
	test_tc_peer__destroy(skel);
	netns_unload_bpf();
	restore_root_netns();
}

void test_tc_redirect(void)
{
	struct netns_setup_result setup_result;

	root_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (CHECK_FAIL(root_netns_fd < 0))
		return;

	if (netns_setup_namespaces("add"))
		goto done;

	if (netns_setup_links_and_routes(&setup_result))
		goto done;

	if (test__start_subtest("tc_redirect_peer"))
		test_tc_redirect_peer(&setup_result);

	if (test__start_subtest("tc_redirect_neigh"))
		test_tc_redirect_neigh(&setup_result);

	if (test__start_subtest("tc_redirect_neigh_fib"))
		test_tc_redirect_neigh_fib(&setup_result);

done:
	close(root_netns_fd);
	netns_setup_namespaces("delete");
}
