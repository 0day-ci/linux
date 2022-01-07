// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <bpf/bpf_helpers.h>

#define IP_SRC_OFF offsetof(struct iphdr, saddr)
#define UDP_SPORT_OFF (sizeof(struct iphdr) + offsetof(struct udphdr, source))

#define IS_PSEUDO 0x10

#define UDP_CSUM_OFF (sizeof(struct iphdr) + offsetof(struct udphdr, check))
#define IP_CSUM_OFF offsetof(struct iphdr, check)
#define TOS_OFF offsetof(struct iphdr, tos)

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} test_result SEC(".maps");

SEC("cgroup_skb/egress")
int cgroup_store_bytes(struct __sk_buff *skb)
{
	struct ethhdr eth;
	struct iphdr iph;
	struct udphdr udph;

	__u32 map_key = 0;
	__u32 test_passed = 0;

	if (bpf_skb_load_bytes_relative(skb, 0, &iph, sizeof(iph),
									BPF_HDR_START_NET))
		goto fail;

	if (bpf_skb_load_bytes_relative(skb, sizeof(iph), &udph, sizeof(udph),
									BPF_HDR_START_NET))
		goto fail;

	__u32 old_ip = htonl(iph.saddr);
	__u32 new_ip = 0xac100164; //172.16.1.100

	bpf_l4_csum_replace(skb, UDP_CSUM_OFF, old_ip, new_ip,
						IS_PSEUDO | sizeof(new_ip));
	bpf_l3_csum_replace(skb, IP_CSUM_OFF, old_ip, new_ip, sizeof(new_ip));
	if (bpf_skb_store_bytes(skb, IP_SRC_OFF, &new_ip, sizeof(new_ip), 0) < 0)
		goto fail;

	__u16 old_port = udph.source;
	__u16 new_port = 5555;

	bpf_l4_csum_replace(skb, UDP_CSUM_OFF, old_port, new_port,
						IS_PSEUDO | sizeof(new_port));
	if (bpf_skb_store_bytes(skb, UDP_SPORT_OFF, &new_port, sizeof(new_port),
							0) < 0)
		goto fail;

	test_passed = 1;

fail:
	bpf_map_update_elem(&test_result, &map_key, &test_passed, BPF_ANY);

	return 1;
}
