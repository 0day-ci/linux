// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dc_trace.h"

#include <asm/fpu/api.h>

/**
 * DOC: Overview
 *
 * DC core uses FPU operations in multiple parts of the code, which requires a
 * more specialized way to manage these areas' entrance. To fulfill this
 * requirement, we created some wrapper functions that encapsulate
 * kernel_fpu_begin/end to better fit our need in the display component. In
 * summary, in this file, you can find functions related to FPU operation
 * management.
 */

static DEFINE_PER_CPU(atomic_t, fpu_ref);

/**
 * dc_fpu_begin - Enables FPU protection
 * @function_name: A string containing the function name for debug purposes
 * @line: A-line number where DC_FP_START was invoked for debug purpose
 *
 * This function is responsible for managing the use of kernel_fpu_begin() with
 * the advantage of providing an event trace for debugging.
 *
 * Note: Do not call this function directly; always use DC_FP_START().
 */
void dc_fpu_begin(const char *function_name, const int line)
{
	int ret;
	atomic_t *local_fpu_ref = this_cpu_ptr(&fpu_ref);

	ret = atomic_inc_return(local_fpu_ref);
	TRACE_DCN_FPU(true, function_name, line, ret);

	if (ret == 1)
		kernel_fpu_begin();
}

/**
 * dc_fpu_end - Disable FPU protection
 * @function_name: A string containing the function name for debug purposes
 * @line: A-line number where DC_FP_END was invoked for debug purpose
 *
 * This function is responsible for managing the use of kernel_fpu_end() with
 * the advantage of providing an event trace for debugging.
 *
 * Note: Do not call this function directly; always use DC_FP_END().
 */
void dc_fpu_end(const char *function_name, const int line)
{

	int ret;
	atomic_t *local_fpu_ref = this_cpu_ptr(&fpu_ref);

	ret = atomic_dec_return(local_fpu_ref);
	TRACE_DCN_FPU(false, function_name, line, ret);

	if (!ret)
		kernel_fpu_end();
}
