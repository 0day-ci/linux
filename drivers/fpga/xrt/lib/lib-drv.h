/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _LIB_DRV_H_
#define _LIB_DRV_H_

#include <linux/device/class.h>
#include <linux/device/bus.h>

extern struct class *xrt_class;
extern struct bus_type xrt_bus_type;

const char *xrt_drv_name(enum xrt_subdev_id id);
struct xrt_dev_endpoints *xrt_drv_get_endpoints(enum xrt_subdev_id id);

#endif	/* _LIB_DRV_H_ */
