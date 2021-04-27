/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XRT_DDR_CALIBRATION_H_
#define _XRT_DDR_CALIBRATION_H_

#include "xleaf.h"
#include <linux/xrt/xclbin.h>

/*
 * Memory calibration driver leaf calls.
 */
enum xrt_calib_results {
	XRT_CALIB_UNKNOWN = 0,
	XRT_CALIB_SUCCEEDED,
	XRT_CALIB_FAILED,
};

enum xrt_calib_leaf_cmd {
	XRT_CALIB_RESULT = XRT_XLEAF_CUSTOM_BASE, /* See comments in xleaf.h */
};

#endif	/* _XRT_DDR_CALIBRATION_H_ */
