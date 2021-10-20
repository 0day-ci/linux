// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <sys/types.h>
#include <sys/wait.h>
#include <test_progs.h>
#include "task_local_storage.skel.h"
#include "task_local_storage_exit_creds.skel.h"
#include "task_ls_recursion.skel.h"
#include "task_ls_prealloc.skel.h"

#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434
#endif

static inline int sys_pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(__NR_pidfd_open, pid, flags);
}

static void test_sys_enter_exit(void)
{
	struct task_local_storage *skel;
	int err;

	skel = task_local_storage__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	skel->bss->target_pid = syscall(SYS_gettid);

	err = task_local_storage__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	syscall(SYS_gettid);
	syscall(SYS_gettid);

	/* 3x syscalls: 1x attach and 2x gettid */
	ASSERT_EQ(skel->bss->enter_cnt, 3, "enter_cnt");
	ASSERT_EQ(skel->bss->exit_cnt, 3, "exit_cnt");
	ASSERT_EQ(skel->bss->mismatch_cnt, 0, "mismatch_cnt");
out:
	task_local_storage__destroy(skel);
}

static void test_exit_creds(void)
{
	struct task_local_storage_exit_creds *skel;
	int err;

	skel = task_local_storage_exit_creds__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = task_local_storage_exit_creds__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* trigger at least one exit_creds() */
	if (CHECK_FAIL(system("ls > /dev/null")))
		goto out;

	/* sync rcu to make sure exit_creds() is called for "ls" */
	kern_sync_rcu();
	ASSERT_EQ(skel->bss->valid_ptr_count, 0, "valid_ptr_count");
	ASSERT_NEQ(skel->bss->null_ptr_count, 0, "null_ptr_count");
out:
	task_local_storage_exit_creds__destroy(skel);
}

static void test_recursion(void)
{
	struct task_ls_recursion *skel;
	int err;

	skel = task_ls_recursion__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = task_ls_recursion__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* trigger sys_enter, make sure it does not cause deadlock */
	syscall(SYS_gettid);

out:
	task_ls_recursion__destroy(skel);
}

static int fork_prealloc_child(int *pipe_fd)
{
	int pipe_fds[2], pid_fd, err;
	pid_t pid;

	err = pipe(pipe_fds);
	if (!ASSERT_OK(err, "pipe"))
		return -1;

	*pipe_fd = pipe_fds[1];

	pid = fork();
	if (pid == 0) {
		char ch;
		close(pipe_fds[1]);
		read(pipe_fds[0], &ch, 1);
		exit(0);
	}

	if (!ASSERT_GE(pid, 0, "fork"))
		return -1;

	pid_fd = sys_pidfd_open(pid, 0);
	if (!ASSERT_GE(pid_fd, 0, "pidfd_open"))
		return -1;

	return pid_fd;
}

static void test_prealloc_elem(int map_fd, int pid_fd)
{
	int val, err;

	err = bpf_map_lookup_elem(map_fd, &pid_fd, &val);
	if (ASSERT_OK(err, "bpf_map_lookup_elem"))
		ASSERT_EQ(val, 0, "elem value == 0");

	val = 0xdeadbeef;
	err = bpf_map_update_elem(map_fd, &pid_fd, &val, BPF_EXIST);
	ASSERT_OK(err, "bpf_map_update_elem to 0xdeadbeef");

	err = bpf_map_lookup_elem(map_fd, &pid_fd, &val);
	if (ASSERT_OK(err, "bpf_map_lookup_elem"))
		ASSERT_EQ(val, 0xdeadbeef, "elem value == 0xdeadbeef");
}

static void test_prealloc(void)
{
	struct task_ls_prealloc *skel = NULL;
	int pre_pipe_fd = -1, post_pipe_fd = -1;
	int pre_pid_fd, post_pid_fd;
	int map_fd, err;

	pre_pid_fd = fork_prealloc_child(&pre_pipe_fd);
	if (pre_pid_fd < 0)
		goto out;

	skel = task_ls_prealloc__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out;

	err = task_ls_prealloc__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	post_pid_fd = fork_prealloc_child(&post_pipe_fd);
	if (post_pid_fd < 0)
		goto out;

	map_fd = bpf_map__fd(skel->maps.prealloc_map);
	if (!ASSERT_GE(map_fd, 0, "bpf_map__fd"))
		goto out;

	test_prealloc_elem(map_fd, pre_pid_fd);
	test_prealloc_elem(map_fd, post_pid_fd);
out:
	if (pre_pipe_fd >= 0)
		close(pre_pipe_fd);
	if (post_pipe_fd >= 0)
		close(post_pipe_fd);
	do {
		err = wait4(-1, NULL, 0, NULL);
	} while (!err);

	if (skel)
		task_ls_prealloc__destroy(skel);
}

void test_task_local_storage(void)
{
	if (test__start_subtest("sys_enter_exit"))
		test_sys_enter_exit();
	if (test__start_subtest("exit_creds"))
		test_exit_creds();
	if (test__start_subtest("recursion"))
		test_recursion();
	if (test__start_subtest("prealloc"))
		test_prealloc();
}
