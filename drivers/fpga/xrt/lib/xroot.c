// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Root Functions
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 *	Lizhi Hou <lizhih@xilinx.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/xrt/xdevice.h>
#include "xroot.h"

#define XROOT_FDT_ALIGN		8

#define xroot_err(xr, fmt, args...) dev_err((xr)->dev, "%s: " fmt, __func__, ##args)
#define xroot_warn(xr, fmt, args...) dev_warn((xr)->dev, "%s: " fmt, __func__, ##args)
#define xroot_info(xr, fmt, args...) dev_info((xr)->dev, "%s: " fmt, __func__, ##args)
#define xroot_dbg(xr, fmt, args...) dev_dbg((xr)->dev, "%s: " fmt, __func__, ##args)

struct xroot {
	struct device *dev;
	struct list_head groups;
	u32 addr;
	struct xroot_range *ranges;
	int num_range;
	struct ida grp_ida;
};

struct xroot_group {
	struct list_head node;
	struct xroot *xr;
	struct xrt_device *grp_dev;
	struct property ranges;
	struct of_changeset chgset;
	bool chgset_applied;
	void *dn_mem;
	char *name;
	void *fdt;
	int id;
};

#define XRT_GROUP	"xrt-group"
#define MAX_GRP_NAME_LEN	64

static void xroot_cleanup_group(struct xroot_group *grp)
{
	if (grp->grp_dev)
		xrt_device_unregister(grp->grp_dev);
	if (grp->chgset_applied)
		of_changeset_revert(&grp->chgset);
	of_changeset_destroy(&grp->chgset);

	if (grp->id >= 0)
		ida_free(&grp->xr->grp_ida, grp->id);
	kfree(grp->dn_mem);
	kfree(grp->name);
	kfree(grp->fdt);
}

void xroot_destroy_group(void *root, u32 grp_id)
{
	struct xroot *xr = root;
	struct xroot_group *grp;

	list_for_each_entry(grp, &xr->groups, node) {
		if (grp->id == grp_id)
			break;
	}
	if (list_entry_is_head(grp, &xr->groups, node))
		return;

	list_del(&grp->node);

	xroot_cleanup_group(grp);
	kfree(grp);
}
EXPORT_SYMBOL_GPL(xroot_destroy_group);

/*
 * Create XRT group device.
 *
 * Unflatten the device tree blob attached to the group and
 * overlay the device nodes under /xrt-bus. Then create group device
 * and link it to device node.
 */
int xroot_create_group(void *root, void *dtb, u32 len)
{
	struct device_node *dn, *bus, *grp_dn;
	struct xroot *xr = root;
	struct xroot_group *grp;
	void *dtb_aligned;
	int ret;

	grp = kzalloc(sizeof(*grp), GFP_KERNEL);
	if (!grp)
		return -ENOMEM;

	bus = of_find_node_by_path("/xrt-bus");
	if (!bus) {
		kfree(grp);
		return -EINVAL;
	}
	grp->xr = xr;
	of_changeset_init(&grp->chgset);

	ret = ida_alloc(&xr->grp_ida, GFP_KERNEL);
	if (ret < 0)
		goto failed;

	grp->id = ret;

	grp->name = kzalloc(MAX_GRP_NAME_LEN, GFP_KERNEL);
	if (!grp->name) {
		ret = -ENOMEM;
		goto failed;
	}
	snprintf(grp->name, MAX_GRP_NAME_LEN, "%s@%x,%x", XRT_GROUP, xr->addr, grp->id);

	grp->fdt = kmalloc(len + XROOT_FDT_ALIGN, GFP_KERNEL);
	if (!grp->fdt) {
		ret = -ENOMEM;
		goto failed;
	}
	dtb_aligned = PTR_ALIGN(grp->fdt, XROOT_FDT_ALIGN);
	memcpy(dtb_aligned, dtb, len);

	grp->dn_mem = of_fdt_unflatten_tree(dtb_aligned, NULL, &grp_dn);
	if (!grp->dn_mem) {
		ret = -EINVAL;
		goto failed;
	}

	of_node_get(grp_dn);
	grp_dn->full_name = grp->name;
	grp_dn->parent = bus;
	for (dn = grp_dn; dn; dn = of_find_all_nodes(dn))
		of_changeset_attach_node(&grp->chgset, dn);

	grp->ranges.name = "ranges";
	grp->ranges.length = xr->num_range * sizeof(*xr->ranges);
	grp->ranges.value = xr->ranges;
	ret = of_changeset_add_property(&grp->chgset, grp_dn, &grp->ranges);
	if (ret)
		goto failed;

	ret = of_changeset_apply(&grp->chgset);
	if (ret)
		goto failed;
	grp->chgset_applied = true;

	of_node_put(bus);
	bus = NULL;

	grp->grp_dev = xrt_device_register(xr->dev, grp_dn, NULL, 0, NULL, 0);
	if (!grp->grp_dev) {
		ret = -EFAULT;
		goto failed;
	}

	if (device_attach(&grp->grp_dev->dev) != 1) {
		ret = -EFAULT;
		xroot_err(xr, "failed to attach");
		goto failed;
	}

	list_add(&grp->node, &xr->groups);

	return grp->id;

failed:
	if (bus)
		of_node_put(bus);
	xroot_cleanup_group(grp);
	return ret;
}
EXPORT_SYMBOL_GPL(xroot_create_group);

int xroot_probe(struct device *dev, struct xroot_info *info, void **root)
{
	struct xroot *xr = NULL;

	dev_info(dev, "%s: probing...", __func__);

	xr = devm_kzalloc(dev, sizeof(*xr), GFP_KERNEL);
	if (!xr)
		return -ENOMEM;

	xr->dev = dev;
	INIT_LIST_HEAD(&xr->groups);

	xr->addr = info->addr;
	xr->num_range = info->num_range;
	xr->ranges = devm_kzalloc(dev, sizeof(*info->ranges) * info->num_range, GFP_KERNEL);
	if (!xr->ranges)
		return -ENOMEM;

	memcpy(&xr->ranges, info->ranges, sizeof(*info->ranges) * info->num_range);
	ida_init(&xr->grp_ida);

	*root = xr;
	return 0;
}
EXPORT_SYMBOL_GPL(xroot_probe);

void xroot_remove(void *root)
{
	struct xroot *xr = (struct xroot *)root;
	struct xroot_group *grp, *tmp;

	xroot_info(xr, "leaving...");
	list_for_each_entry_safe(grp, tmp, &xr->groups, node) {
		list_del(&grp->node);
		xroot_cleanup_group(grp);
		kfree(grp);
	}
}
EXPORT_SYMBOL_GPL(xroot_remove);
