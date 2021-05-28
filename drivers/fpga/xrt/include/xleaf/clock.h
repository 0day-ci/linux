/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Lizhi Hou <Lizhi.Hou@xilinx.com>
 */

#ifndef _XRT_CLOCK_H_
#define _XRT_CLOCK_H_

#include "xleaf.h"
#include <linux/xrt/xclbin.h>

/*
 * CLOCK driver leaf calls.
 */
enum xrt_clock_leaf_cmd {
	XRT_CLOCK_SET = XRT_XLEAF_CUSTOM_BASE, /* See comments in xleaf.h */
	XRT_CLOCK_GET,
	XRT_CLOCK_VERIFY,
};

struct xrt_clock_get {
	u16 freq;
	u32 freq_cnter;
};

#endif	/* _XRT_CLOCK_H_ */
