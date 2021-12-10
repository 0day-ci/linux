// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern void bpf_kfunc_call_test_pass_ctx(struct __sk_buff *skb) __ksym;

SEC("tc")
int kfunc_call_test_fail4(struct __sk_buff *skb)
{
	struct __sk_buff local_skb = {};

	bpf_kfunc_call_test_pass_ctx(&local_skb);
	return 0;
}

char _license[] SEC("license") = "GPL";
