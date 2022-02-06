// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Hengqi Chen */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

const volatile pid_t my_pid = 0;
int option = 0;
unsigned long arg2 = 0;
unsigned long arg3 = 0;
unsigned long arg4 = 0;
unsigned long arg5 = 0;

SEC("kprobe/" SYS_PREFIX "sys_prctl")
int BPF_KPROBE_SYSCALL(prctl_enter, int opt, unsigned long a2,
		       unsigned long a3, unsigned long a4, unsigned long a5)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	option = opt;
	arg2 = a2;
	arg3 = a3;
	arg4 = a4;
	arg5 = a5;
	return 0;
}

char _license[] SEC("license") = "GPL";
