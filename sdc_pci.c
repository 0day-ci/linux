// SPDX-License-Identifier: GPL-2.0-only
/*
 * SUNIX SDC PCIe driver.
 *
 * Copyright (C) 2021, SUNIX Co., Ltd.
 *
 * Based on Intel Sunrisepoint LPSS PCI driver written by
 * - Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 * - Mika Westerberg <mika.westerberg@linux.intel.com>
 * Copyright (C) 2015, Intel Corporation
 */

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include "sdc_mfd.h"

static int sdc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct sdc_platform_info info;
	unsigned long flags;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	flags = pci_resource_flags(pdev, 0);
	if (!(flags & IORESOURCE_MEM)) {
		pr_err("bar0 resource flag x%lx invalid\n", flags);
		return -ENODEV;
	}

	flags = pci_resource_flags(pdev, 1);
	if (!(flags & IORESOURCE_IO)) {
		pr_err("bar1 resource flag x%lx invalid\n", flags);
		return -ENODEV;
	}

	flags = pci_resource_flags(pdev, 2);
	if (!(flags & IORESOURCE_MEM)) {
		pr_err("bar2 resource flag x%lx invalid\n", flags);
		return -ENODEV;
	}

	memset(&info, 0, sizeof(info));
	info.pdev = pdev;
	info.bus_number = pdev->bus->number;
	info.device_number = PCI_SLOT(pdev->devfn);
	info.irq = pdev->irq;

	ret = sdc_probe(&pdev->dev, &info);
	if (ret)
		return ret;

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void sdc_pci_remove(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	sdc_remove(&pdev->dev);
}

static SDC_PM_OPS(sdc_pci_pm_ops);

static const struct pci_device_id sdc_pci_ids[] = {
	{ PCI_VDEVICE(SUNIX, 0x2000) },
	{ },
};
MODULE_DEVICE_TABLE(pci, sdc_pci_ids);

static struct pci_driver sdc_pci_driver = {
	.name = "sdc_pci",
	.id_table = sdc_pci_ids,
	.probe = sdc_pci_probe,
	.remove = sdc_pci_remove,
	.driver = {
		.pm = &sdc_pci_pm_ops,
	},
};
module_pci_driver(sdc_pci_driver);

MODULE_AUTHOR("Jason Lee <jason_lee@sunix.com>");
MODULE_DESCRIPTION("SUNIX SDC PCIe driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: sdc_mfd");
