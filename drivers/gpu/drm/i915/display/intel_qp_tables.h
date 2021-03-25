/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_QP_TABLES_H_
#define _INTEL_QP_TABLES_H_

#include <linux/types.h>

#define RC_RANGE_QP(min_max, bpp, row, col) \
	    rc_range_##min_max##qp444_##bpp##bpc[row][col]

#ifndef DSC_NUM_BUF_RANGES
#define DSC_NUM_BUF_RANGES			15
#endif

/* from BPP 6 to 24 in steps of 0.5 */
#define RC_RANGE_QP444_8BPC_MAX_NUM_BPP		37

/* from BPP 6 to 30 in steps of 0.5 */
#define RC_RANGE_QP444_10BPC_MAX_NUM_BPP	49

/* from BPP 6 to 36 in steps of 0.5 */
#define RC_RANGE_QP444_12BPC_MAX_NUM_BPP	61

const u8 rc_range_minqp444_8bpc[DSC_NUM_BUF_RANGES][RC_RANGE_QP444_8BPC_MAX_NUM_BPP];
const u8 rc_range_maxqp444_8bpc[DSC_NUM_BUF_RANGES][RC_RANGE_QP444_8BPC_MAX_NUM_BPP];
const u8 rc_range_minqp444_10bpc[DSC_NUM_BUF_RANGES][RC_RANGE_QP444_10BPC_MAX_NUM_BPP];
const u8 rc_range_maxqp444_10bpc[DSC_NUM_BUF_RANGES][RC_RANGE_QP444_10BPC_MAX_NUM_BPP];
const u8 rc_range_minqp444_12bpc[DSC_NUM_BUF_RANGES][RC_RANGE_QP444_12BPC_MAX_NUM_BPP];
const u8 rc_range_maxqp444_12bpc[DSC_NUM_BUF_RANGES][RC_RANGE_QP444_12BPC_MAX_NUM_BPP];

#endif
