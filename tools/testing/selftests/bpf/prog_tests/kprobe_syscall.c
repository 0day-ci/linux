// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Hengqi Chen */

#include <test_progs.h>
#include <sys/prctl.h>
#include "test_kprobe_syscall.skel.h"

void test_kprobe_syscall(void)
{
	struct test_kprobe_syscall *skel;
	int err;

	skel = test_kprobe_syscall__open();
	if (!ASSERT_OK_PTR(skel, "test_kprobe_syscall__open"))
		return;

	skel->rodata->my_pid = getpid();

	err = test_kprobe_syscall__load(skel);
	if (!ASSERT_OK(err, "test_kprobe_syscall__load"))
		goto cleanup;

	err = test_kprobe_syscall__attach(skel);
	if (!ASSERT_OK(err, "test_kprobe_syscall__attach"))
		goto cleanup;

	prctl(1, 2, 3, 4, 5);

	ASSERT_EQ(skel->bss->option, 1, "BPF_KPROBE_SYSCALL failed");
	ASSERT_EQ(skel->bss->arg2, 2, "BPF_KPROBE_SYSCALL failed");
	ASSERT_EQ(skel->bss->arg3, 3, "BPF_KPROBE_SYSCALL failed");
	ASSERT_EQ(skel->bss->arg4, 4, "BPF_KPROBE_SYSCALL failed");
	ASSERT_EQ(skel->bss->arg5, 5, "BPF_KPROBE_SYSCALL failed");

cleanup:
	test_kprobe_syscall__destroy(skel);
}
