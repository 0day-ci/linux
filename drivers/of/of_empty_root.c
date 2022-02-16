// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Xilinx, Inc.
 */

#include <linux/of.h>
#include <linux/slab.h>

#include "of_private.h"

static int __init of_root_init(void)
{
	struct property *prop = NULL;
	struct device_node *node;
	__be32 *val = NULL;

	if (of_root)
		return 0;

	pr_info("Create empty OF root node\n");
	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	of_node_init(node);
	node->full_name = "/";

	prop = kcalloc(2, sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	val = kzalloc(sizeof(*val), GFP_KERNEL);
	if (!val)
		return -ENOMEM;
	*val = cpu_to_be32(sizeof(void *) / sizeof(u32));

	prop->name = "#address-cells";
	prop->value = val;
	prop->length = sizeof(u32);
	of_add_property(node, prop);
	prop++;
	prop->name = "#size-cells";
	prop->value = val;
	prop->length = sizeof(u32);
	of_add_property(node, prop);
	of_root = node;
	for_each_of_allnodes(node)
		__of_attach_node_sysfs(node);

	return 0;
}
pure_initcall(of_root_init);
