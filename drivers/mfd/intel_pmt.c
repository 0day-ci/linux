// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitoring Technology PMT driver
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include "intel_extended_caps.h"

static const struct intel_ext_cap_platform_info tgl_info = {
	.quirks = EXT_CAP_QUIRK_NO_WATCHER | EXT_CAP_QUIRK_NO_CRASHLOG |
		  EXT_CAP_QUIRK_TABLE_SHIFT,
};

/* DG1 Platform with DVSEC quirk*/
static struct intel_ext_cap_header dg1_telemetry = {
	.length = 0x10,
	.id = 2,
	.num_entries = 1,
	.entry_size = 3,
	.tbir = 0,
	.offset = 0x466000,
};

static struct intel_ext_cap_header *dg1_capabilities[] = {
	&dg1_telemetry,
	NULL
};

static const struct intel_ext_cap_platform_info dg1_info = {
	.quirks = EXT_CAP_QUIRK_NO_DVSEC,
	.capabilities = dg1_capabilities,
};


static int pmt_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
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

static void pmt_pci_remove(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
}

#define PCI_DEVICE_ID_INTEL_PMT_ADL	0x467d
#define PCI_DEVICE_ID_INTEL_PMT_DG1	0x490e
#define PCI_DEVICE_ID_INTEL_PMT_TGL	0x9a0d
static const struct pci_device_id pmt_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, PMT_ADL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, PMT_DG1, &dg1_info) },
	{ PCI_DEVICE_DATA(INTEL, PMT_TGL, &tgl_info) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pmt_pci_ids);

static struct pci_driver pmt_pci_driver = {
	.name = "intel-pmt",
	.id_table = pmt_pci_ids,
	.probe = pmt_pci_probe,
	.remove = pmt_pci_remove,
};
module_pci_driver(pmt_pci_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Platform Monitoring Technology PMT driver");
MODULE_LICENSE("GPL v2");
