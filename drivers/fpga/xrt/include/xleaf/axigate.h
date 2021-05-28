/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Lizhi Hou <Lizhi.Hou@xilinx.com>
 */

#ifndef _XRT_AXIGATE_H_
#define _XRT_AXIGATE_H_

#include "xleaf.h"
#include "metadata.h"

/*
 * AXIGATE driver leaf calls.
 */
enum xrt_axigate_leaf_cmd {
	XRT_AXIGATE_CLOSE = XRT_XLEAF_CUSTOM_BASE, /* See comments in xleaf.h */
	XRT_AXIGATE_OPEN,
};

#endif	/* _XRT_AXIGATE_H_ */
