// SPDX-License-Identifier: GPL-2.0
#include "bpf_syscall_macro_kernel.skel.h"

void test_bpf_syscall_macro_kernel(void);

#define test_bpf_syscall_macro test_bpf_syscall_macro_kernel
#define bpf_syscall_macro bpf_syscall_macro_kernel
#define bpf_syscall_macro__open bpf_syscall_macro_kernel__open
#define bpf_syscall_macro__load bpf_syscall_macro_kernel__load
#define bpf_syscall_macro__attach bpf_syscall_macro_kernel__attach
#define bpf_syscall_macro__destroy bpf_syscall_macro_kernel__destroy

#include "test_bpf_syscall_macro_common.h"
