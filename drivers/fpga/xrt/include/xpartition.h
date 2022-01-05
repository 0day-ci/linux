/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Xilinx, Inc.
 *
 * Authors:
 *	Lizhi Hou <lizhih@xilinx.com>
 */

#ifndef _XRT_PARTITION_H_
#define _XRT_PARTITION_H_

struct xrt_partition_range {
	u32 bar_idx;
	u64 base;
	u64 size;
};

struct xrt_partition_info {
	int num_range;
	struct xrt_partition_range *ranges;
	void *fdt;
	u32 fdt_len;
};

int xrt_partition_create(struct device *dev, struct xrt_partition_info *info, void **handle);
void xrt_partition_destroy(void *handle);

#endif
