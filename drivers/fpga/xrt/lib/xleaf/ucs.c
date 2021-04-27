// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA UCS Driver
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
#include "xleaf/clock.h"

#define UCS_ERR(ucs, fmt, arg...)   \
	xrt_err((ucs)->xdev, fmt "\n", ##arg)
#define UCS_WARN(ucs, fmt, arg...)  \
	xrt_warn((ucs)->xdev, fmt "\n", ##arg)
#define UCS_INFO(ucs, fmt, arg...)  \
	xrt_info((ucs)->xdev, fmt "\n", ##arg)
#define UCS_DBG(ucs, fmt, arg...)   \
	xrt_dbg((ucs)->xdev, fmt "\n", ##arg)

#define XRT_UCS		"xrt_ucs"

#define XRT_UCS_CHANNEL1_REG			0
#define XRT_UCS_CHANNEL2_REG			8

#define CLK_MAX_VALUE			6400

XRT_DEFINE_REGMAP_CONFIG(ucs_regmap_config);

struct xrt_ucs {
	struct xrt_device	*xdev;
	struct regmap		*regmap;
	struct mutex		ucs_lock; /* ucs dev lock */
};

static void xrt_ucs_event_cb(struct xrt_device *xdev, void *arg)
{
	struct xrt_event *evt = (struct xrt_event *)arg;
	enum xrt_events e = evt->xe_evt;
	struct xrt_device *leaf;
	enum xrt_subdev_id id;
	int instance;

	id = evt->xe_subdev.xevt_subdev_id;
	instance = evt->xe_subdev.xevt_subdev_instance;

	if (e != XRT_EVENT_POST_CREATION) {
		xrt_dbg(xdev, "ignored event %d", e);
		return;
	}

	if (id != XRT_SUBDEV_CLOCK)
		return;

	leaf = xleaf_get_leaf_by_id(xdev, XRT_SUBDEV_CLOCK, instance);
	if (!leaf) {
		xrt_err(xdev, "does not get clock subdev");
		return;
	}

	xleaf_call(leaf, XRT_CLOCK_VERIFY, NULL);
	xleaf_put_leaf(xdev, leaf);
}

static int ucs_enable(struct xrt_ucs *ucs)
{
	int ret;

	mutex_lock(&ucs->ucs_lock);
	ret = regmap_write(ucs->regmap, XRT_UCS_CHANNEL2_REG, 1);
	mutex_unlock(&ucs->ucs_lock);

	return ret;
}

static int
xrt_ucs_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	switch (cmd) {
	case XRT_XLEAF_EVENT:
		xrt_ucs_event_cb(xdev, arg);
		break;
	default:
		xrt_err(xdev, "unsupported cmd %d", cmd);
		return -EINVAL;
	}

	return 0;
}

static int ucs_probe(struct xrt_device *xdev)
{
	struct xrt_ucs *ucs = NULL;
	void __iomem *base = NULL;
	struct resource *res;

	ucs = devm_kzalloc(&xdev->dev, sizeof(*ucs), GFP_KERNEL);
	if (!ucs)
		return -ENOMEM;

	xrt_set_drvdata(xdev, ucs);
	ucs->xdev = xdev;
	mutex_init(&ucs->ucs_lock);

	res = xrt_get_resource(xdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	base = devm_ioremap_resource(&xdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ucs->regmap = devm_regmap_init_mmio(&xdev->dev, base, &ucs_regmap_config);
	if (IS_ERR(ucs->regmap)) {
		UCS_ERR(ucs, "map base %pR failed", res);
		return PTR_ERR(ucs->regmap);
	}
	ucs_enable(ucs);

	return 0;
}

static struct xrt_dev_endpoints xrt_ucs_endpoints[] = {
	{
		.xse_names = (struct xrt_dev_ep_names[]) {
			{ .ep_name = XRT_MD_NODE_UCS_CONTROL_STATUS },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{ 0 },
};

static struct xrt_driver xrt_ucs_driver = {
	.driver = {
		.name = XRT_UCS,
	},
	.subdev_id = XRT_SUBDEV_UCS,
	.endpoints = xrt_ucs_endpoints,
	.probe = ucs_probe,
	.leaf_call = xrt_ucs_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(ucs);
