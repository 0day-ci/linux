// SPDX-License-Identifier: GPL-2.0-only
/*
 * It's for XSAVE fork test.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>

#include "xsave_common.h"

static unsigned char *xsave_buf0, *xsave_buf1, *xsave_buf2;
static int xsave_size, read_result[2];
static const char *fork_test = "xsave fork for xsave context switch test";

void set_xstates(void)
{
	/* SDM XSAVE: misalignment to a 64-byte boundary will result in #GP */
	xsave_buf0 = aligned_alloc(64, xsave_size);
	if (!xsave_buf0)
		execution_failed("aligned_alloc xsave_buf0 failed\n");
	xsave_buf1 = aligned_alloc(64, xsave_size);
	if (!xsave_buf1)
		execution_failed("aligned_alloc xsave_buf1 failed\n");
	xsave_buf2 = aligned_alloc(64, xsave_size);
	if (!xsave_buf2)
		execution_failed("aligned_alloc xsave_buf2 failed\n");

	populate_xstate_regs();
}

void clear_avx(void)
{
	asm volatile("vzeroall");
}

/* use fork to create pid and trigger context switch test */
int test_fork(void)
{
	pid_t child;
	int status, fd[2], result[2];
	const char *test_xsave_child = "Child xstate was same as parent";
	const char *test_process_swich = "Xstate after process switch was same";

	/* Use pipe to transfer test result of child process to parent process */
	if (pipe(fd) < 0)
		execution_failed("FAIL: create pipe failed\n");

	XSAVE(xsave_buf0, XSAVE_TEST_MASK);
	child = fork();
	if (child < 0)
		execution_failed("fork failed\n");
	if (child == 0) {
		XSAVE(xsave_buf1, XSAVE_TEST_MASK);
		result[0] = compare_xsave_buf(xsave_buf0, xsave_buf1, xsave_size,
			fork_test, NO_CHANGE);
		close(fd[0]);
		if (!write(fd[1], &result, sizeof(result)))
			execution_failed("FAIL: write fd failed.\n");

		XSAVE(xsave_buf1, XSAVE_TEST_MASK);
		pid_t grandchild;

		/* fork grandchild will trigger process switching in child */
		grandchild = fork();
		if (grandchild == 0) {
			ksft_print_msg("Grandchild pid:%d clean it's XMM YMM ZMM xstates\n",
				getpid());
			clear_avx();
			return 0;
		}
		if (grandchild)
			waitpid(grandchild, NULL, 0);

		/* After swich back to child process and check xstate */
		XSAVE(xsave_buf2, XSAVE_TEST_MASK);
		ksft_print_msg("Child pid:%d check xstate after swtich back\n",
			getpid());
		result[1] = compare_xsave_buf(xsave_buf1, xsave_buf2, xsave_size,
			fork_test, NO_CHANGE);
		if (!write(fd[1], &result, sizeof(result)))
			execution_failed("FAIL: write fd failed.\n");

		return 0;
	}

	if (child) {
		if (waitpid(child, &status, 0) != child || !WIFEXITED(status))
			ksft_test_result_fail("Child exit with error, status:0x%x\n",
				status);
		else {
			ksft_print_msg("Parent pid:%d get results\n", getpid());
			close(fd[1]);
			if (!read(fd[0], &read_result, sizeof(read_result)))
				execution_failed("FAIL: read fd failed.\n");
		}
	}

	ksft_set_plan(2);
	check_result(read_result[0], test_xsave_child);
	check_result(read_result[1], test_process_swich);

	return 0;
}

int main(int argc, char *argv[])
{
	cpu_set_t set;

	xsave_size = get_xsave_size();
	ksft_print_header();

	CPU_ZERO(&set);
	CPU_SET(0, &set);

	set_xstates();
	test_fork();

	ksft_exit(!ksft_get_fail_cnt());
}
