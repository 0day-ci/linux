// SPDX-License-Identifier: GPL-2.0
#include "bpf_syscall_macro_user.skel.h"

void test_bpf_syscall_macro_user(void);

#define test_bpf_syscall_macro test_bpf_syscall_macro_user
#define bpf_syscall_macro bpf_syscall_macro_user
#define bpf_syscall_macro__open bpf_syscall_macro_user__open
#define bpf_syscall_macro__load bpf_syscall_macro_user__load
#define bpf_syscall_macro__attach bpf_syscall_macro_user__attach
#define bpf_syscall_macro__destroy bpf_syscall_macro_user__destroy

#include "test_bpf_syscall_macro_common.h"
