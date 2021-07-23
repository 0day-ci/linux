// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/version.h>

#include <bpf/bpf_helpers.h>
#include "netcnt_common.h"

#define MAX_BPS	(3 * 1024 * 1024)

#define REFRESH_TIME_NS	100000000
#define NS_PER_SEC	1000000000

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, struct percpu_net_cnt);
} percpu_netcnt SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, struct net_cnt);
} netcnt SEC(".maps");

SEC("cgroup/skb")
int bpf_nextcnt(struct __sk_buff *skb)
{
	struct percpu_net_cnt *percpu_cnt;
	char fmt[] = "%d %llu %llu\n";
	struct net_cnt *cnt;
	__u64 ts, dt;
	int ret;

	cnt = bpf_get_local_storage(&netcnt, 0);
	percpu_cnt = bpf_get_local_storage(&percpu_netcnt, 0);

	percpu_cnt->val.packets++;
	percpu_cnt->val.bytes += skb->len;

	if (percpu_cnt->val.packets > MAX_PERCPU_PACKETS) {
		__sync_fetch_and_add(&cnt->val.packets,
				     percpu_cnt->val.packets);
		percpu_cnt->val.packets = 0;

		__sync_fetch_and_add(&cnt->val.bytes,
				     percpu_cnt->val.bytes);
		percpu_cnt->val.bytes = 0;
	}

	ts = bpf_ktime_get_ns();
	dt = ts - percpu_cnt->val.prev_ts;

	dt *= MAX_BPS;
	dt /= NS_PER_SEC;

	if (cnt->val.bytes + percpu_cnt->val.bytes -
	    percpu_cnt->val.prev_bytes < dt)
		ret = 1;
	else
		ret = 0;

	if (dt > REFRESH_TIME_NS) {
		percpu_cnt->val.prev_ts = ts;
		percpu_cnt->val.prev_packets = cnt->val.packets;
		percpu_cnt->val.prev_bytes = cnt->val.bytes;
	}

	return !!ret;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = LINUX_VERSION_CODE;
