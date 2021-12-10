// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern void bpf_kfunc_call_test_fail2(struct prog_test_fail2 *p) __ksym;

SEC("tc")
int kfunc_call_test_fail2(struct __sk_buff *skb)
{
	struct prog_test_fail2 s = {};

	bpf_kfunc_call_test_fail2(&s);
	return 0;
}

char _license[] SEC("license") = "GPL";
