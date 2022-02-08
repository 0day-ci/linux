// SPDX-License-Identifier: GPL-2.0

#include <linux/ptrace.h>
#include <linux/bpf.h>

#include <netinet/in.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

static struct sockaddr_in old;

SEC("kprobe/" SYS_PREFIX "sys_connect")
int BPF_KPROBE(handle_sys_connect)
{
	struct pt_regs *real_regs;
	struct sockaddr_in new;
	void *ptr;

	real_regs = PT_REGS_SYSCALL_REGS(ctx);
	bpf_probe_read_kernel(&ptr, sizeof(ptr), &PT_REGS_PARM2(real_regs));

	bpf_probe_read_user(&old, sizeof(old), ptr);
	__builtin_memset(&new, 0xab, sizeof(new));
	bpf_probe_write_user(ptr, &new, sizeof(new));

	return 0;
}

char _license[] SEC("license") = "GPL";
