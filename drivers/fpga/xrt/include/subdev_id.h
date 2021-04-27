/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XRT_SUBDEV_ID_H_
#define _XRT_SUBDEV_ID_H_

/*
 * Every subdev driver has an ID for others to refer to it. There can be multiple number of
 * instances of a subdev driver. A <subdev_id, subdev_instance> tuple is a unique identification
 * of a specific instance of a subdev driver.
 */
enum xrt_subdev_id {
	XRT_SUBDEV_GRP = 1,
	XRT_SUBDEV_VSEC = 2,
	XRT_SUBDEV_VSEC_GOLDEN = 3,
	XRT_SUBDEV_DEVCTL = 4,
	XRT_SUBDEV_AXIGATE = 5,
	XRT_SUBDEV_ICAP = 6,
	XRT_SUBDEV_TEST = 7,
	XRT_SUBDEV_MGNT_MAIN = 8,
	XRT_SUBDEV_QSPI = 9,
	XRT_SUBDEV_MAILBOX = 10,
	XRT_SUBDEV_CMC = 11,
	XRT_SUBDEV_CALIB = 12,
	XRT_SUBDEV_CLKFREQ = 13,
	XRT_SUBDEV_CLOCK = 14,
	XRT_SUBDEV_SRSR = 15,
	XRT_SUBDEV_UCS = 16,
	XRT_SUBDEV_NUM = 17, /* Total number of subdevs. */
	XRT_ROOT = -1, /* Special ID for root driver. */
};

#endif	/* _XRT_SUBDEV_ID_H_ */
