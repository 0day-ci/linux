// SPDX-License-Identifier: GPL-2.0
#include <linux/ptrace.h>
#include <linux/types.h>
#include <sys/types.h>

#include "bpf_syscall_macro_common.h"

#if defined(__KERNEL__) || defined(__VMLINUX_H__)
#error This test must be compiled with userspace headers
#endif
