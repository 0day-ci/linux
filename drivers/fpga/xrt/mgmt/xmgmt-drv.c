// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo Management Function Driver
 *
 * Copyright (C) 2020-2022 Xilinx, Inc.
 *
 * Authors:
 *     Cheng Zhen <maxz@xilinx.com>
 *     Lizhi Hou <lizhih@xilinx.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/of_pci.h>

#define XMGMT_MODULE_NAME	"xrt-mgmt"

/* PCI Device IDs */
#define PCI_DEVICE_ID_U50	0x5020
static const struct pci_device_id xmgmt_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_U50), }, /* Alveo U50 */
	{ 0, }
};

static int xmgmt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	devm_of_pci_create_bus_endpoint(pdev);

	return 0;
}

static struct pci_driver xmgmt_driver = {
	.name = XMGMT_MODULE_NAME,
	.id_table = xmgmt_pci_ids,
	.probe = xmgmt_probe,
};

static int __init xmgmt_init(void)
{
	int res;

	res = pci_register_driver(&xmgmt_driver);
	if (res)
		return res;

	return 0;
}

static __exit void xmgmt_exit(void)
{
	pci_unregister_driver(&xmgmt_driver);
}

module_init(xmgmt_init);
module_exit(xmgmt_exit);

MODULE_DEVICE_TABLE(pci, xmgmt_pci_ids);
MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo management function driver");
MODULE_LICENSE("GPL v2");
