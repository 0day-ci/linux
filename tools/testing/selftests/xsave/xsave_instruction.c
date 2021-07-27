// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test kernel support for XSAVE-managed features.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "../kselftest.h"
#include "xsave_common.h"

static unsigned char *xsave_buf0, *xsave_buf1;

static void set_ymm0_reg(uint32_t ui32)
{
	asm volatile ("vbroadcastss %0, %%ymm0" : : "m" (ui32));
}

static void dump_xsave_content(int xsave_size)
{
	XSAVE(xsave_buf0, SAVE_MASK);
	dump_buffer(xsave_buf0, xsave_size);
	ksft_print_msg("Entire contents of XSAVE is as above\n");
}

static void test_xsave_ymm_change(int xsave_size)
{
	const char *test_name = "xsave test after ymm change";
	uint32_t ui32_set = 0x1234, ui32_change = 0x5678;
	int result = RESULT_ERROR;

	set_ymm0_reg(ui32_set);
	XSAVE(xsave_buf0, SAVE_MASK);
	set_ymm0_reg(ui32_change);
	XSAVE(xsave_buf1, SAVE_MASK);
	result = check_xsave_buf(xsave_buf0, xsave_buf1, xsave_size, test_name,
				CHANGE);
	check_result(result, test_name);
}

static void test_xsave_xrstor(int xsave_size)
{
	const char *test_name = "xsave after xrstor test";
	int result = RESULT_ERROR;

	XSAVE(xsave_buf0, SAVE_MASK);
	XRSTOR(xsave_buf0, SAVE_MASK);
	XSAVE(xsave_buf1, SAVE_MASK);
	result = check_xsave_buf(xsave_buf0, xsave_buf1, xsave_size, test_name,
				NO_CHANGE);
	check_result(result, test_name);
}

int main(void)
{
	int xsave_size;

	ksft_print_header();
	ksft_set_plan(2);

	xsave_size = get_xsave_size();
	/* SDM XSAVE: misalignment to a 64-byte boundary will result in #GP */
	xsave_buf0 = aligned_alloc(64, xsave_size);
	if (!xsave_buf0)
		execution_failed("aligned_alloc xsave_buf0 failed\n");
	xsave_buf1 = aligned_alloc(64, xsave_size);
	if (!xsave_buf1)
		execution_failed("aligned_alloc xsave_buf1 failed\n");

	populate_fpu_regs();
	/* Show the entire contents of xsave for issue debug */
	dump_xsave_content(xsave_size);

	test_xsave_ymm_change(xsave_size);
	test_xsave_xrstor(xsave_size);

	ksft_exit(!ksft_get_fail_cnt());
}
