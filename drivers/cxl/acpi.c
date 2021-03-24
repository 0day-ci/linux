// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include "cxl.h"

static int cxl_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cxl_root *cxl_root;

	cxl_root = devm_cxl_add_root(dev, NULL, 0);
	if (IS_ERR(cxl_root))
		return PTR_ERR(cxl_root);
	dev_dbg(dev, "register: %s\n", dev_name(&cxl_root->port.dev));

	return 0;
}

static const struct acpi_device_id cxl_acpi_ids[] = {
	{ "ACPI0017", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, cxl_acpi_ids);

static struct platform_driver cxl_acpi_driver = {
	.probe = cxl_acpi_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = cxl_acpi_ids,
	},
};

module_platform_driver(cxl_acpi_driver);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
