// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Group Driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/xrt/xdevice.h>
#include "lib-drv.h"
#include "xroot.h"

#define XRT_GRP "xrt_group"

struct xgroup_leaf {
	struct list_head node;
	struct xrt_device *leaf_dev;
};

struct xgroup {
	struct xrt_device *xdev;
	struct list_head leaves;
};

static int xrt_grp_probe(struct xrt_device *xdev)
{
	struct xgroup_leaf *leaf;
	struct xgroup *xg = NULL;
	struct device_node *dn;

	dev_info(&xdev->dev, "probing\n");

	xg = devm_kzalloc(&xdev->dev, sizeof(*xg), GFP_KERNEL);
	if (!xg)
		return -ENOMEM;

	xg->xdev = xdev;
	INIT_LIST_HEAD(&xg->leaves);
	xrt_set_drvdata(xdev, xg);

	for_each_child_of_node(xdev->dev.of_node, dn) {
		leaf = kmalloc(sizeof(*leaf), GFP_KERNEL);
		if (!leaf)
			break;

		leaf->leaf_dev = xrt_device_register(&xdev->dev, dn, NULL, 0, NULL, 0);
		if (!leaf->leaf_dev) {
			kfree(leaf);
			continue;
		}
		list_add(&leaf->node, &xg->leaves);
	}

	return 0;
}

static void xrt_grp_remove(struct xrt_device *xdev)
{
	struct xgroup *xg = xrt_get_drvdata(xdev);
	struct xgroup_leaf *leaf, *tmp;

	list_for_each_entry_safe(leaf, tmp, &xg->leaves, node) {
		list_del(&leaf->node);
		xrt_device_unregister(leaf->leaf_dev);
		kfree(leaf);
	}
}

static int xrt_grp_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	return 0;
}

static const struct of_device_id group_match[] = {
	{ .compatible = "xlnx,xrt-group" },
	{ }
};

static struct xrt_driver xrt_group_driver = {
	.driver	= {
		.name = XRT_GRP,
		.of_match_table = group_match,
	},
	.probe = xrt_grp_probe,
	.remove = xrt_grp_remove,
	.leaf_call = xrt_grp_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(group);
