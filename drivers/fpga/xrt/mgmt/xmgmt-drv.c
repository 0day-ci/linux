// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo Management Function Driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 *	Lizhi Hou <lizhih@xilinx.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include "xroot.h"
#include "dt-test.h"

#define XMGMT_MODULE_NAME	"xrt-mgmt"

#define XMGMT_PDEV(xm)		((xm)->pdev)
#define XMGMT_DEV(xm)		(&(XMGMT_PDEV(xm)->dev))
#define xmgmt_err(xm, fmt, args...)	\
	dev_err(XMGMT_DEV(xm), "%s: " fmt, __func__, ##args)
#define xmgmt_warn(xm, fmt, args...)	\
	dev_warn(XMGMT_DEV(xm), "%s: " fmt, __func__, ##args)
#define xmgmt_info(xm, fmt, args...)	\
	dev_info(XMGMT_DEV(xm), "%s: " fmt, __func__, ##args)
#define xmgmt_dbg(xm, fmt, args...)	\
	dev_dbg(XMGMT_DEV(xm), "%s: " fmt, __func__, ##args)
#define XMGMT_DEV_ID(_pcidev)			\
	({ typeof(_pcidev) (pcidev) = (_pcidev);	\
	((pci_domain_nr((pcidev)->bus) << 16) |	\
	PCI_DEVID((pcidev)->bus->number, (pcidev)->devfn)); })

#define XRT_MAX_READRQ		512

/* PCI Device IDs */
#define PCI_DEVICE_ID_U50		0x5020
static const struct pci_device_id xmgmt_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_U50), }, /* Alveo U50 */
	{ 0, }
};

struct xmgmt {
	struct pci_dev *pdev;
	void *root;

	bool ready;
};

static int xmgmt_config_pci(struct xmgmt *xm)
{
	struct pci_dev *pdev = XMGMT_PDEV(xm);
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc < 0) {
		xmgmt_err(xm, "failed to enable device: %d", rc);
		return rc;
	}

	rc = pci_enable_pcie_error_reporting(pdev);
	if (rc)
		xmgmt_warn(xm, "failed to enable AER: %d", rc);

	pci_set_master(pdev);

	rc = pcie_get_readrq(pdev);
	if (rc > XRT_MAX_READRQ)
		pcie_set_readrq(pdev, XRT_MAX_READRQ);
	return 0;
}

static int xmgmt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct xroot_range ranges[PCI_NUM_RESOURCES];
	struct xroot_info xr_info = { 0 };
	struct device *dev = &pdev->dev;
	int ret, i, idx = 0;
	struct xmgmt *xm;
	__be64 tmp;

	xm = devm_kzalloc(dev, sizeof(*xm), GFP_KERNEL);
	if (!xm)
		return -ENOMEM;
	xm->pdev = pdev;
	pci_set_drvdata(pdev, xm);

	ret = xmgmt_config_pci(xm);
	if (ret)
		goto failed;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (pci_resource_len(pdev, i) > 0) {
			ranges[idx].child_addr[0] = cpu_to_be32(i);
			tmp = cpu_to_be64(pci_resource_start(pdev, i));
			memcpy(ranges[idx].parent_addr, &tmp, sizeof(tmp));
			tmp = cpu_to_be64(pci_resource_len(pdev, i));
			memcpy(ranges[idx].child_size, &tmp, sizeof(tmp));
			idx++;
		}
	}
	xr_info.num_range = idx;
	xr_info.ranges = ranges;
	xr_info.addr = XMGMT_DEV_ID(pdev);
	ret = xroot_probe(&pdev->dev, &xr_info, &xm->root);
	if (ret)
		goto failed;

	ret = xroot_create_group(xm->root, __dtb_dt_test_begin,
				 (u32)(__dtb_dt_test_end - __dtb_dt_test_begin));
	if (ret) {
		xmgmt_err(xm, "failed to create root group: %d", ret);
		goto failed;
	}

	xmgmt_info(xm, "%s started successfully", XMGMT_MODULE_NAME);
	return 0;

failed:
	if (xm->root)
		xroot_remove(xm->root);
	pci_set_drvdata(pdev, NULL);
	return ret;
}

static void xmgmt_remove(struct pci_dev *pdev)
{
	struct xmgmt *xm = pci_get_drvdata(pdev);

	xroot_remove(xm->root);
	pci_disable_pcie_error_reporting(xm->pdev);
	xmgmt_info(xm, "%s cleaned up successfully", XMGMT_MODULE_NAME);
}

static struct pci_driver xmgmt_driver = {
	.name = XMGMT_MODULE_NAME,
	.id_table = xmgmt_pci_ids,
	.probe = xmgmt_probe,
	.remove = xmgmt_remove,
};

static int __init xmgmt_init(void)
{
	int res = 0;

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
