/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XRT_ROOT_H_
#define _XRT_ROOT_H_

struct xroot_range {
	__be32 child_addr[3];
	__be32 parent_addr[2];
	__be32 child_size[2];
};

struct xroot_info {
	u32 addr;
	int num_range;
	struct xroot_range *ranges;
};

int xroot_probe(struct device *dev, struct xroot_info *info, void **root);
void xroot_remove(void *root);

int xroot_create_group(void *xr, void *dtb, u32 len);
void xroot_destroy_group(void *xr, u32 grp_id);

#endif	/* _XRT_ROOT_H_ */
