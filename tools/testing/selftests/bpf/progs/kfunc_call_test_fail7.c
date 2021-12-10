// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern struct prog_test_ref_kfunc *bpf_kfunc_call_test_acquire(unsigned long *sp) __ksym;
extern void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p) __ksym;

SEC("tc")
int kfunc_call_test_fail7(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *p, *p2;
	unsigned long sp = 0;

	p = bpf_kfunc_call_test_acquire(&sp);
	if (p) {
		p2 = p->next->next;
		bpf_kfunc_call_test_release(p);
		if (p2->a == 42)
			return 1;
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
