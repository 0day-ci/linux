// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 *	Lizhi Hou <lizhi.hou@xilinx.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/of_device.h>
#include "lib-drv.h"

static int xrt_bus_ovcs_id;

static __init int xrt_lib_init(void)
{
	int ret;

	ret = of_overlay_fdt_apply(__dtb_xrt_bus_begin,
				   __dtb_xrt_bus_end - __dtb_xrt_bus_begin,
				   &xrt_bus_ovcs_id);
	if (ret)
		return ret;

	return 0;
}

static __exit void xrt_lib_fini(void)
{
	of_overlay_remove(&xrt_bus_ovcs_id);
}

module_init(xrt_lib_init);
module_exit(xrt_lib_fini);

MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo IP Lib driver");
MODULE_LICENSE("GPL v2");
