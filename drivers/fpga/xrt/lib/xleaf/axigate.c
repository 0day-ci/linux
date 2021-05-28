// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA AXI Gate Driver
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
#include "xleaf/axigate.h"

#define XRT_AXIGATE "xrt_axigate"

#define XRT_AXIGATE_WRITE_REG		0
#define XRT_AXIGATE_READ_REG		8

#define XRT_AXIGATE_CTRL_CLOSE		0
#define XRT_AXIGATE_CTRL_OPEN_BIT0	1
#define XRT_AXIGATE_CTRL_OPEN_BIT1	2

#define XRT_AXIGATE_INTERVAL		500 /* ns */

struct xrt_axigate {
	struct xrt_device	*xdev;
	struct regmap		*regmap;
	struct mutex		gate_lock; /* gate dev lock */
	void			*evt_hdl;
	const char		*ep_name;
	bool			gate_closed;
};

XRT_DEFINE_REGMAP_CONFIG(axigate_regmap_config);

/* the ep names are in the order of hardware layers */
static const char * const xrt_axigate_epnames[] = {
	XRT_MD_NODE_GATE_PLP, /* PLP: Provider Logic Partition */
	XRT_MD_NODE_GATE_ULP  /* ULP: User Logic Partition */
};

static inline int close_gate(struct xrt_axigate *gate)
{
	u32 val;
	int ret;

	ret = regmap_write(gate->regmap, XRT_AXIGATE_WRITE_REG, XRT_AXIGATE_CTRL_CLOSE);
	if (ret) {
		xrt_err(gate->xdev, "write gate failed %d", ret);
		return ret;
	}
	ndelay(XRT_AXIGATE_INTERVAL);
	/*
	 * Legacy hardware requires extra read work properly.
	 * This is not on critical path, thus the extra read should not impact performance much.
	 */
	ret = regmap_read(gate->regmap, XRT_AXIGATE_READ_REG, &val);
	if (ret) {
		xrt_err(gate->xdev, "read gate failed %d", ret);
		return ret;
	}

	return 0;
}

static inline int open_gate(struct xrt_axigate *gate)
{
	u32 val;
	int ret;

	ret = regmap_write(gate->regmap, XRT_AXIGATE_WRITE_REG, XRT_AXIGATE_CTRL_OPEN_BIT1);
	if (ret) {
		xrt_err(gate->xdev, "write 2 failed %d", ret);
		return ret;
	}
	ndelay(XRT_AXIGATE_INTERVAL);
	/*
	 * Legacy hardware requires extra read work properly.
	 * This is not on critical path, thus the extra read should not impact performance much.
	 */
	ret = regmap_read(gate->regmap, XRT_AXIGATE_READ_REG, &val);
	if (ret) {
		xrt_err(gate->xdev, "read 2 failed %d", ret);
		return ret;
	}
	ret = regmap_write(gate->regmap, XRT_AXIGATE_WRITE_REG,
			   XRT_AXIGATE_CTRL_OPEN_BIT0 | XRT_AXIGATE_CTRL_OPEN_BIT1);
	if (ret) {
		xrt_err(gate->xdev, "write 3 failed %d", ret);
		return ret;
	}
	ndelay(XRT_AXIGATE_INTERVAL);
	ret = regmap_read(gate->regmap, XRT_AXIGATE_READ_REG, &val);
	if (ret) {
		xrt_err(gate->xdev, "read 3 failed %d", ret);
		return ret;
	}

	return 0;
}

static int xrt_axigate_epname_idx(struct xrt_device *xdev)
{
	struct resource	*res;
	int ret, i;

	res = xrt_get_resource(xdev, IORESOURCE_MEM, 0);
	if (!res) {
		xrt_err(xdev, "Empty Resource!");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(xrt_axigate_epnames); i++) {
		ret = strncmp(xrt_axigate_epnames[i], res->name,
			      strlen(xrt_axigate_epnames[i]) + 1);
		if (!ret)
			return i;
	}

	return -EINVAL;
}

static int xrt_axigate_close(struct xrt_device *xdev)
{
	struct xrt_axigate *gate;
	u32 status = 0;
	int ret;

	gate = xrt_get_drvdata(xdev);

	mutex_lock(&gate->gate_lock);
	ret = regmap_read(gate->regmap, XRT_AXIGATE_READ_REG, &status);
	if (ret) {
		xrt_err(xdev, "read gate failed %d", ret);
		goto failed;
	}
	if (status) {		/* gate is opened */
		xleaf_broadcast_event(xdev, XRT_EVENT_PRE_GATE_CLOSE, false);
		ret = close_gate(gate);
		if (ret)
			goto failed;
	}

	gate->gate_closed = true;

failed:
	mutex_unlock(&gate->gate_lock);

	xrt_info(xdev, "close gate %s", gate->ep_name);
	return ret;
}

static int xrt_axigate_open(struct xrt_device *xdev)
{
	struct xrt_axigate *gate;
	u32 status;
	int ret;

	gate = xrt_get_drvdata(xdev);

	mutex_lock(&gate->gate_lock);
	ret = regmap_read(gate->regmap, XRT_AXIGATE_READ_REG, &status);
	if (ret) {
		xrt_err(xdev, "read gate failed %d", ret);
		goto failed;
	}
	if (!status) {		/* gate is closed */
		ret = open_gate(gate);
		if (ret)
			goto failed;
		xleaf_broadcast_event(xdev, XRT_EVENT_POST_GATE_OPEN, true);
		/* xrt_axigate_open() could be called in event cb, thus
		 * we can not wait for the completes
		 */
	}

	gate->gate_closed = false;

failed:
	mutex_unlock(&gate->gate_lock);

	xrt_info(xdev, "open gate %s", gate->ep_name);
	return ret;
}

static void xrt_axigate_event_cb(struct xrt_device *xdev, void *arg)
{
	struct xrt_axigate *gate = xrt_get_drvdata(xdev);
	struct xrt_event *evt = (struct xrt_event *)arg;
	enum xrt_events e = evt->xe_evt;
	struct xrt_device *leaf;
	enum xrt_subdev_id id;
	struct resource	*res;
	int instance;

	if (e != XRT_EVENT_POST_CREATION)
		return;

	instance = evt->xe_subdev.xevt_subdev_instance;
	id = evt->xe_subdev.xevt_subdev_id;
	if (id != XRT_SUBDEV_AXIGATE)
		return;

	leaf = xleaf_get_leaf_by_id(xdev, id, instance);
	if (!leaf)
		return;

	res = xrt_get_resource(leaf, IORESOURCE_MEM, 0);
	if (!res || !strncmp(res->name, gate->ep_name, strlen(res->name) + 1)) {
		xleaf_put_leaf(xdev, leaf);
		return;
	}

	/* higher level axigate instance created, make sure the gate is opened. */
	if (xrt_axigate_epname_idx(leaf) > xrt_axigate_epname_idx(xdev))
		xrt_axigate_open(xdev);
	else
		xleaf_call(leaf, XRT_AXIGATE_OPEN, NULL);

	xleaf_put_leaf(xdev, leaf);
}

static int
xrt_axigate_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	int ret = 0;

	switch (cmd) {
	case XRT_XLEAF_EVENT:
		xrt_axigate_event_cb(xdev, arg);
		break;
	case XRT_AXIGATE_CLOSE:
		ret = xrt_axigate_close(xdev);
		break;
	case XRT_AXIGATE_OPEN:
		ret = xrt_axigate_open(xdev);
		break;
	default:
		xrt_err(xdev, "unsupported cmd %d", cmd);
		return -EINVAL;
	}

	return ret;
}

static int xrt_axigate_probe(struct xrt_device *xdev)
{
	struct xrt_axigate *gate = NULL;
	void __iomem *base = NULL;
	struct resource *res;
	int ret;

	gate = devm_kzalloc(&xdev->dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return -ENOMEM;

	gate->xdev = xdev;
	xrt_set_drvdata(xdev, gate);

	xrt_info(xdev, "probing...");
	res = xrt_get_resource(xdev, IORESOURCE_MEM, 0);
	if (!res) {
		xrt_err(xdev, "Empty resource 0");
		ret = -EINVAL;
		goto failed;
	}

	base = devm_ioremap_resource(&xdev->dev, res);
	if (IS_ERR(base)) {
		xrt_err(xdev, "map base iomem failed");
		ret = PTR_ERR(base);
		goto failed;
	}

	gate->regmap = devm_regmap_init_mmio(&xdev->dev, base, &axigate_regmap_config);
	if (IS_ERR(gate->regmap)) {
		xrt_err(xdev, "regmap %pR failed", res);
		ret = PTR_ERR(gate->regmap);
		goto failed;
	}
	gate->ep_name = res->name;

	mutex_init(&gate->gate_lock);

	return 0;

failed:
	return ret;
}

static struct xrt_dev_endpoints xrt_axigate_endpoints[] = {
	{
		.xse_names = (struct xrt_dev_ep_names[]) {
			{ .ep_name = XRT_MD_NODE_GATE_ULP },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{
		.xse_names = (struct xrt_dev_ep_names[]) {
			{ .ep_name = XRT_MD_NODE_GATE_PLP },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{ 0 },
};

static struct xrt_driver xrt_axigate_driver = {
	.driver = {
		.name = XRT_AXIGATE,
	},
	.subdev_id = XRT_SUBDEV_AXIGATE,
	.endpoints = xrt_axigate_endpoints,
	.probe = xrt_axigate_probe,
	.leaf_call = xrt_axigate_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(axigate);
