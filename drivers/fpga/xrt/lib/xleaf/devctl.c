// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA devctl Driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *      Lizhi Hou<Lizhi.Hou@xilinx.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include "metadata.h"
#include "xleaf.h"
#include "xleaf/devctl.h"

#define XRT_DEVCTL "xrt_devctl"

struct xrt_name_id {
	char *ep_name;
	int id;
};

static struct xrt_name_id name_id[XRT_DEVCTL_MAX] = {
	{ XRT_MD_NODE_BLP_ROM, XRT_DEVCTL_ROM_UUID },
	{ XRT_MD_NODE_GOLDEN_VER, XRT_DEVCTL_GOLDEN_VER },
};

XRT_DEFINE_REGMAP_CONFIG(devctl_regmap_config);

struct xrt_devctl {
	struct xrt_device	*xdev;
	struct regmap		*regmap[XRT_DEVCTL_MAX];
	ulong			sizes[XRT_DEVCTL_MAX];
};

static int xrt_devctl_name2id(struct xrt_devctl *devctl, const char *name)
{
	int i;

	for (i = 0; i < XRT_DEVCTL_MAX && name_id[i].ep_name; i++) {
		if (!strncmp(name_id[i].ep_name, name, strlen(name_id[i].ep_name) + 1))
			return name_id[i].id;
	}

	return -EINVAL;
}

static int
xrt_devctl_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	struct xrt_devctl *devctl;
	int ret = 0;

	devctl = xrt_get_drvdata(xdev);

	switch (cmd) {
	case XRT_XLEAF_EVENT:
		/* Does not handle any event. */
		break;
	case XRT_DEVCTL_READ: {
		struct xrt_devctl_rw *rw_arg = arg;

		if (rw_arg->xdr_len & 0x3) {
			xrt_err(xdev, "invalid len %d", rw_arg->xdr_len);
			return -EINVAL;
		}

		if (rw_arg->xdr_id >= XRT_DEVCTL_MAX) {
			xrt_err(xdev, "invalid id %d", rw_arg->xdr_id);
			return -EINVAL;
		}

		if (!devctl->regmap[rw_arg->xdr_id]) {
			xrt_err(xdev, "io not found, id %d",
				rw_arg->xdr_id);
			return -EINVAL;
		}

		ret = regmap_bulk_read(devctl->regmap[rw_arg->xdr_id], rw_arg->xdr_offset,
				       rw_arg->xdr_buf,
				       rw_arg->xdr_len / devctl_regmap_config.reg_stride);
		break;
	}
	default:
		xrt_err(xdev, "unsupported cmd %d", cmd);
		return -EINVAL;
	}

	return ret;
}

static int xrt_devctl_probe(struct xrt_device *xdev)
{
	struct xrt_devctl *devctl = NULL;
	void __iomem *base = NULL;
	struct resource *res;
	int i, id, ret = 0;

	devctl = devm_kzalloc(&xdev->dev, sizeof(*devctl), GFP_KERNEL);
	if (!devctl)
		return -ENOMEM;

	devctl->xdev = xdev;
	xrt_set_drvdata(xdev, devctl);

	xrt_info(xdev, "probing...");
	for (i = 0, res = xrt_get_resource(xdev, IORESOURCE_MEM, 0);
	    res;
	    res = xrt_get_resource(xdev, IORESOURCE_MEM, ++i)) {
		struct regmap_config config = devctl_regmap_config;

		id = xrt_devctl_name2id(devctl, res->name);
		if (id < 0) {
			xrt_err(xdev, "ep %s not found", res->name);
			continue;
		}
		base = devm_ioremap_resource(&xdev->dev, res);
		if (IS_ERR(base)) {
			ret = PTR_ERR(base);
			break;
		}
		config.max_register = res->end - res->start + 1;
		devctl->regmap[id] = devm_regmap_init_mmio(&xdev->dev, base, &config);
		if (IS_ERR(devctl->regmap[id])) {
			xrt_err(xdev, "map base failed %pR", res);
			ret = PTR_ERR(devctl->regmap[id]);
			break;
		}
		devctl->sizes[id] = res->end - res->start + 1;
	}

	return ret;
}

static struct xrt_dev_endpoints xrt_devctl_endpoints[] = {
	{
		.xse_names = (struct xrt_dev_ep_names[]) {
			/* add name if ep is in same partition */
			{ .ep_name = XRT_MD_NODE_BLP_ROM },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{
		.xse_names = (struct xrt_dev_ep_names[]) {
			{ .ep_name = XRT_MD_NODE_GOLDEN_VER },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	/* adding ep bundle generates devctl device instance */
	{ 0 },
};

static struct xrt_driver xrt_devctl_driver = {
	.driver = {
		.name = XRT_DEVCTL,
	},
	.subdev_id = XRT_SUBDEV_DEVCTL,
	.endpoints = xrt_devctl_endpoints,
	.probe = xrt_devctl_probe,
	.leaf_call = xrt_devctl_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(devctl);
