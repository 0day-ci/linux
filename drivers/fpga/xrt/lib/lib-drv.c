// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 *	Lizhi Hou <lizhi.hou@xilinx.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include "xpartition.h"
#include "lib-drv.h"

#define XRT_PARTITION_FDT_ALIGN		8
#define XRT_PARTITION_NAME_LEN		64

struct xrt_partition {
	struct device *dev;
	u32 id;
	char name[XRT_PARTITION_NAME_LEN];
	void *fdt;
	struct property ranges;
	struct of_changeset chgset;
	bool chgset_applied;
	void *dn_mem;
};

DEFINE_IDA(xrt_partition_id);

static int xrt_partition_set_ranges(struct xrt_partition *xp, struct xrt_partition_range *ranges,
				    int num_range)
{
	__be64 *prop;
	u32 prop_len;
	int i;

	prop_len = num_range * (sizeof(u64) * 3);
	prop = kzalloc(prop_len, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	xp->ranges.name = "ranges";
	xp->ranges.length = prop_len;
	xp->ranges.value = prop;

	for (i = 0; i < num_range; i++) {
		*prop = cpu_to_be64((u64)ranges[i].bar_idx << 60);
		prop++;
		*prop = cpu_to_be64(ranges[i].base);
		prop++;
		*prop = cpu_to_be64(ranges[i].size);
		prop++;
	}

	return 0;
}

void xrt_partition_destroy(void *handle)
{
	struct xrt_partition *xp = handle;

	if (xp->chgset_applied)
		of_changeset_revert(&xp->chgset);
	of_changeset_destroy(&xp->chgset);

	ida_free(&xrt_partition_id, xp->id);
	kfree(xp->dn_mem);
	kfree(xp->fdt);
	kfree(xp->ranges.value);
	kfree(xp);
}
EXPORT_SYMBOL_GPL(xrt_partition_destroy);

int xrt_partition_create(struct device *dev, struct xrt_partition_info *info, void **handle)
{
	struct device_node *parent_dn = NULL, *dn, *part_dn;
	struct xrt_partition *xp = NULL;
	void *fdt_aligned;
	int ret;

	xp = kzalloc(sizeof(*xp), GFP_KERNEL);
	if (!xp)
		return -ENOMEM;

	ret = ida_alloc(&xrt_partition_id, GFP_KERNEL);
	if (ret < 0) {
		dev_err(dev, "alloc id failed, ret %d", ret);
		kfree(xp);
		return ret;
	}
	xp->id = ret;
	of_changeset_init(&xp->chgset);

	parent_dn = of_find_node_by_path("/");
	if (!parent_dn) {
		dev_err(dev, "did not find xrt node");
		ret = -EINVAL;
		goto failed;
	}

	xp->dev = dev;
	snprintf(xp->name, XRT_PARTITION_NAME_LEN, "xrt-part@%x", xp->id);
	ret = xrt_partition_set_ranges(xp, info->ranges, info->num_range);
	if (ret)
		goto failed;

	xp->fdt = kmalloc(info->fdt_len + XRT_PARTITION_FDT_ALIGN, GFP_KERNEL);
	if (!xp->fdt) {
		ret = -ENOMEM;
		goto failed;
	}
	fdt_aligned = PTR_ALIGN(xp->fdt, XRT_PARTITION_FDT_ALIGN);
	memcpy(fdt_aligned, info->fdt, info->fdt_len);

	xp->dn_mem = of_fdt_unflatten_tree(fdt_aligned, NULL, &part_dn);
	if (!xp->dn_mem) {
		ret = -EINVAL;
		goto failed;
	}

	of_node_get(part_dn);
	part_dn->full_name = xp->name;
	part_dn->parent = parent_dn;
	for (dn = part_dn; dn; dn = of_find_all_nodes(dn))
		of_changeset_attach_node(&xp->chgset, dn);

	ret = of_changeset_add_property(&xp->chgset, part_dn, &xp->ranges);
	if (ret) {
		dev_err(dev, "failed to add property, ret %d", ret);
		goto failed;
	}

	ret = of_changeset_apply(&xp->chgset);
	if (ret) {
		dev_err(dev, "failed to apply changeset, ret %d", ret);
		goto failed;
	}
	xp->chgset_applied = true;
	of_node_put(parent_dn);

	ret = of_platform_populate(part_dn, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to populate devices, ret %d", ret);
		goto failed;
	}

	*handle = xp;
	return 0;

failed:
	if (parent_dn)
		of_node_put(parent_dn);
	xrt_partition_destroy(xp);
	return ret;
}
EXPORT_SYMBOL_GPL(xrt_partition_create);

static __init int xrt_lib_init(void)
{
	return 0;
}

static __exit void xrt_lib_fini(void)
{
}

module_init(xrt_lib_init);
module_exit(xrt_lib_fini);

MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo IP Lib driver");
MODULE_LICENSE("GPL v2");
