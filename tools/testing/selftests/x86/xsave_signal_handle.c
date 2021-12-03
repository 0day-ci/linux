// SPDX-License-Identifier: GPL-2.0-only
/*
 * It's for xsave/xrstor during signal handling tests
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>

#include "../kselftest.h"
#include "xsave_common.h"

void *aligned_alloc(size_t alignment, size_t size);
static unsigned char *xsave_buf0, *xsave_buf1, *xsave_buf2, *xsave_buf3;
static int result[2], xsave_size;

static void change_fpu_content(uint32_t ui32_random, double flt)
{
	asm volatile("fldl %0" : : "m" (flt));
	asm volatile("vbroadcastss %0, %%ymm0" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm1" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm2" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm3" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm4" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm5" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm6" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm7" : : "m" (ui32_random));
	#ifndef __i386__
	asm volatile("vbroadcastss %0, %%ymm8" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm9" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm10" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm11" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm12" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm13" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm14" : : "m" (ui32_random));
	asm volatile("vbroadcastss %0, %%ymm15" : : "m" (ui32_random));
	#endif
}

static void usr1_handler(int signum, siginfo_t *info, void *__ctxp)
{
	uint32_t ui32_random;
	double flt;
	int xsave_size;
	const char *usr1_name = "Child XSAVE should not change in nested signal";

	ui32_random = rand();
	flt = ui32_random/10000.0;
	if (signum == SIGUSR1) {
		ksft_print_msg("SIGUSR1:0x%x changed fld:%f & ymm0-15:0x%x\n",
			SIGUSR1, flt, ui32_random);
		change_fpu_content(ui32_random, flt);
	}
	xsave_size = get_xsave_size();
	/*
	 * SIGUSR1 handler has independent XSAVE content, which is not affected by
	 * the SIGUSR2 handler
	 */
	XSAVE(xsave_buf2, XSAVE_TEST_MASK);
	raise(SIGUSR2);
	XSAVE(xsave_buf3, XSAVE_TEST_MASK);
	result[0] = compare_xsave_buf(xsave_buf2, xsave_buf3, xsave_size, usr1_name,
		NO_CHANGE);
}

static void usr2_handler(int signum, siginfo_t *info, void *__ctxp)
{
	uint32_t ui32_random;
	double flt;

	ui32_random = rand();
	flt = ui32_random/10000.0;
	if (signum == SIGUSR2) {
		ksft_print_msg("SIGUSR2:0x%x changed fld:%f & ymm0-15:0x%x\n",
			SIGUSR2, flt, ui32_random);
		change_fpu_content(ui32_random, flt);
	}
}

static void set_signal_handle(void)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(sigact));
	if (sigemptyset(&sigact.sa_mask))
		execution_failed("FAIL: sigemptyset error\n");

	sigact.sa_flags = SA_SIGINFO;

	sigact.sa_sigaction = usr1_handler;
	if (sigaction(SIGUSR1, &sigact, NULL))
		execution_failed("FAIL: SIGUSR1 handling failed\n");

	sigact.sa_sigaction = usr2_handler;
	if (sigaction(SIGUSR2, &sigact, NULL))
		execution_failed("FAIL: SIGUSR2 handling failed\n");
}

static void sig_handle_xsave_test(void)
{
	int i, loop_times = 100;
	const char *sig_test_name = "Child XSAVE content was same after signal";

	srand(time(NULL));

	XSAVE(xsave_buf0, XSAVE_TEST_MASK);
	for (i = 1; i <= loop_times; i++) {
		raise(SIGUSR1);
		XSAVE(xsave_buf1, XSAVE_TEST_MASK);
		result[1] = compare_xsave_buf(xsave_buf0, xsave_buf1, xsave_size,
			sig_test_name, NO_CHANGE);
		if (result[1] != RESULT_PASS)
			break;
	}
}

static int test_xsave_sig_handle(void)
{
	const char *test_name0 = "Siganl handling xstate was same after nested signal handling";
	const char *test_name1 = "xstate was same after child signal handling test";
	pid_t child;
	int status, fd[2], readbuf[2];

	set_signal_handle();

	xsave_size = get_xsave_size();
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
	xsave_buf3 = aligned_alloc(64, xsave_size);
	if (!xsave_buf3)
		execution_failed("aligned_alloc xsave_buf3 failed\n");

	/* Use pipe to transfer test result of child process to parent process */
	if (pipe(fd) < 0)
		execution_failed("FAIL: create pipe failed\n");

	populate_xstate_regs();

	/* Use child process testing to avoid abnormal blocking the next test */
	child = fork();
	if (child < 0)
		execution_failed("FAIL: create child pid failed\n");
	else if	(child == 0) {
		XSAVE(xsave_buf0, XSAVE_TEST_MASK);

		sig_handle_xsave_test();
		close(fd[0]);
		if (!write(fd[1], &result, sizeof(result)))
			execution_failed("FAIL: write fd failed.\n");
		return 0;
	}

	if (child) {
		if (waitpid(child, &status, 0) != child || !WIFEXITED(status))
			execution_failed("FAIL: Child died unexpectedly\n");
		else {
			close(fd[1]);
			if (!read(fd[0], &readbuf, sizeof(readbuf)))
				execution_failed("FAIL: read fd failed.\n");
		}
	}

	ksft_set_plan(2);
	check_result(readbuf[0], test_name0);
	check_result(readbuf[1], test_name1);

	return 0;
}

int main(void)
{
	ksft_print_header();

	test_xsave_sig_handle();

	ksft_exit(!ksft_get_fail_cnt());
}
