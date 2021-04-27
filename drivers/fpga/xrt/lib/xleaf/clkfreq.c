// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Clock Frequency Counter Driver
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
#include "xleaf/clkfreq.h"

#define CLKFREQ_ERR(clkfreq, fmt, arg...)   \
	xrt_err((clkfreq)->xdev, fmt "\n", ##arg)
#define CLKFREQ_WARN(clkfreq, fmt, arg...)  \
	xrt_warn((clkfreq)->xdev, fmt "\n", ##arg)
#define CLKFREQ_INFO(clkfreq, fmt, arg...)  \
	xrt_info((clkfreq)->xdev, fmt "\n", ##arg)
#define CLKFREQ_DBG(clkfreq, fmt, arg...)   \
	xrt_dbg((clkfreq)->xdev, fmt "\n", ##arg)

#define XRT_CLKFREQ		"xrt_clkfreq"

#define XRT_CLKFREQ_CONTROL_STATUS_MASK		0xffff

#define XRT_CLKFREQ_CONTROL_START	0x1
#define XRT_CLKFREQ_CONTROL_DONE	0x2
#define XRT_CLKFREQ_V5_CLK0_ENABLED	0x10000

#define XRT_CLKFREQ_CONTROL_REG		0
#define XRT_CLKFREQ_COUNT_REG		0x8
#define XRT_CLKFREQ_V5_COUNT_REG	0x10

#define XRT_CLKFREQ_READ_RETRIES	10

XRT_DEFINE_REGMAP_CONFIG(clkfreq_regmap_config);

struct clkfreq {
	struct xrt_device	*xdev;
	struct regmap		*regmap;
	const char		*clkfreq_ep_name;
	struct mutex		clkfreq_lock; /* clock counter dev lock */
};

static int clkfreq_read(struct clkfreq *clkfreq, u32 *freq)
{
	int times = XRT_CLKFREQ_READ_RETRIES;
	u32 status;
	int ret;

	*freq = 0;
	mutex_lock(&clkfreq->clkfreq_lock);
	ret = regmap_write(clkfreq->regmap, XRT_CLKFREQ_CONTROL_REG, XRT_CLKFREQ_CONTROL_START);
	if (ret) {
		CLKFREQ_INFO(clkfreq, "write start to control reg failed %d", ret);
		goto failed;
	}
	while (times != 0) {
		ret = regmap_read(clkfreq->regmap, XRT_CLKFREQ_CONTROL_REG, &status);
		if (ret) {
			CLKFREQ_INFO(clkfreq, "read control reg failed %d", ret);
			goto failed;
		}
		if ((status & XRT_CLKFREQ_CONTROL_STATUS_MASK) == XRT_CLKFREQ_CONTROL_DONE)
			break;
		mdelay(1);
		times--;
	};

	if (!times) {
		ret = -ETIMEDOUT;
		goto failed;
	}

	if (status & XRT_CLKFREQ_V5_CLK0_ENABLED)
		ret = regmap_read(clkfreq->regmap, XRT_CLKFREQ_V5_COUNT_REG, freq);
	else
		ret = regmap_read(clkfreq->regmap, XRT_CLKFREQ_COUNT_REG, freq);
	if (ret) {
		CLKFREQ_INFO(clkfreq, "read count failed %d", ret);
		goto failed;
	}

	mutex_unlock(&clkfreq->clkfreq_lock);

	return 0;

failed:
	mutex_unlock(&clkfreq->clkfreq_lock);

	return ret;
}

static ssize_t freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct clkfreq *clkfreq = xrt_get_drvdata(to_xrt_dev(dev));
	ssize_t count;
	u32 freq;

	if (clkfreq_read(clkfreq, &freq))
		return -EINVAL;

	count = snprintf(buf, 64, "%u\n", freq);

	return count;
}
static DEVICE_ATTR_RO(freq);

static struct attribute *clkfreq_attrs[] = {
	&dev_attr_freq.attr,
	NULL,
};

static struct attribute_group clkfreq_attr_group = {
	.attrs = clkfreq_attrs,
};

static int
xrt_clkfreq_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	struct clkfreq *clkfreq;
	int ret = 0;

	clkfreq = xrt_get_drvdata(xdev);

	switch (cmd) {
	case XRT_XLEAF_EVENT:
		/* Does not handle any event. */
		break;
	case XRT_CLKFREQ_READ:
		ret = clkfreq_read(clkfreq, arg);
		break;
	default:
		xrt_err(xdev, "unsupported cmd %d", cmd);
		return -EINVAL;
	}

	return ret;
}

static void clkfreq_remove(struct xrt_device *xdev)
{
	sysfs_remove_group(&xdev->dev.kobj, &clkfreq_attr_group);
}

static int clkfreq_probe(struct xrt_device *xdev)
{
	struct clkfreq *clkfreq = NULL;
	void __iomem *base = NULL;
	struct resource *res;
	int ret;

	clkfreq = devm_kzalloc(&xdev->dev, sizeof(*clkfreq), GFP_KERNEL);
	if (!clkfreq)
		return -ENOMEM;

	xrt_set_drvdata(xdev, clkfreq);
	clkfreq->xdev = xdev;
	mutex_init(&clkfreq->clkfreq_lock);

	res = xrt_get_resource(xdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto failed;
	}
	base = devm_ioremap_resource(&xdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto failed;
	}

	clkfreq->regmap = devm_regmap_init_mmio(&xdev->dev, base, &clkfreq_regmap_config);
	if (IS_ERR(clkfreq->regmap)) {
		CLKFREQ_ERR(clkfreq, "regmap %pR failed", res);
		ret = PTR_ERR(clkfreq->regmap);
		goto failed;
	}
	clkfreq->clkfreq_ep_name = res->name;

	ret = sysfs_create_group(&xdev->dev.kobj, &clkfreq_attr_group);
	if (ret) {
		CLKFREQ_ERR(clkfreq, "create clkfreq attrs failed: %d", ret);
		goto failed;
	}

	CLKFREQ_INFO(clkfreq, "successfully initialized clkfreq subdev");

	return 0;

failed:
	return ret;
}

static struct xrt_dev_endpoints xrt_clkfreq_endpoints[] = {
	{
		.xse_names = (struct xrt_dev_ep_names[]) {
			{ .compat = XRT_MD_COMPAT_CLKFREQ },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{ 0 },
};

static struct xrt_driver xrt_clkfreq_driver = {
	.driver = {
		.name = XRT_CLKFREQ,
	},
	.subdev_id = XRT_SUBDEV_CLKFREQ,
	.endpoints = xrt_clkfreq_endpoints,
	.probe = clkfreq_probe,
	.remove = clkfreq_remove,
	.leaf_call = xrt_clkfreq_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(clkfreq);
