// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Out of Band Management Services Module driver
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include "intel_extended_caps.h"

static int intel_oobmsm_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_ext_cap_platform_info *info;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	info = (struct intel_ext_cap_platform_info *)id->driver_data;

	ret = intel_ext_cap_probe(pdev, info);
	if (ret)
		return ret;

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void intel_oobmsm_pci_remove(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
}

#define PCI_DEVICE_ID_INTEL_PMT_OOBMSM	0x09a7
static const struct pci_device_id intel_oobmsm_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, PMT_OOBMSM, NULL) },
	{ }
};
MODULE_DEVICE_TABLE(pci, intel_oobmsm_pci_ids);

static struct pci_driver intel_oobmsm_pci_driver = {
	.name = "intel-oobmsm",
	.id_table = intel_oobmsm_pci_ids,
	.probe = intel_oobmsm_pci_probe,
	.remove = intel_oobmsm_pci_remove,
};
module_pci_driver(intel_oobmsm_pci_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Out of Band Management Services Module driver");
MODULE_LICENSE("GPL v2");
