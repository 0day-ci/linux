// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern void bpf_kfunc_call_test_mem_len_fail2(__u64 *mem, int len) __ksym;

SEC("tc")
int kfunc_call_test_fail6(struct __sk_buff *skb)
{
	int a = 0;

	bpf_kfunc_call_test_mem_len_fail2((void *)&a, sizeof(a));
	return 0;
}

char _license[] SEC("license") = "GPL";
