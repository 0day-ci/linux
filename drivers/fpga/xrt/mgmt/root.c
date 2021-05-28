// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo Management Function Driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

#include "xroot.h"
#include "xmgmt.h"
#include "metadata.h"

#define XMGMT_MODULE_NAME	"xrt-mgmt"
#define XMGMT_DRIVER_VERSION	"4.0.0"

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
	PCI_DEVID((pcidev)->bus->number, 0)); })
#define XRT_VSEC_ID		0x20
#define XRT_MAX_READRQ		512

static struct class *xmgmt_class;

/* PCI Device IDs */
/*
 * Golden image is preloaded on the device when it is shipped to customer.
 * Then, customer can load other shells (from Xilinx or some other vendor).
 * If something goes wrong with the shell, customer can always go back to
 * golden and start over again.
 */
#define PCI_DEVICE_ID_U50_GOLDEN	0xD020
#define PCI_DEVICE_ID_U50		0x5020
static const struct pci_device_id xmgmt_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_U50_GOLDEN), }, /* Alveo U50 (golden) */
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

static int xmgmt_match_slot_and_save(struct device *dev, void *data)
{
	struct xmgmt *xm = data;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (XMGMT_DEV_ID(pdev) == XMGMT_DEV_ID(xm->pdev)) {
		pci_cfg_access_lock(pdev);
		pci_save_state(pdev);
	}

	return 0;
}

static void xmgmt_pci_save_config_all(struct xmgmt *xm)
{
	bus_for_each_dev(&pci_bus_type, NULL, xm, xmgmt_match_slot_and_save);
}

static int xmgmt_match_slot_and_restore(struct device *dev, void *data)
{
	struct xmgmt *xm = data;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (XMGMT_DEV_ID(pdev) == XMGMT_DEV_ID(xm->pdev)) {
		pci_restore_state(pdev);
		pci_cfg_access_unlock(pdev);
	}

	return 0;
}

static void xmgmt_pci_restore_config_all(struct xmgmt *xm)
{
	bus_for_each_dev(&pci_bus_type, NULL, xm, xmgmt_match_slot_and_restore);
}

static void xmgmt_root_hot_reset(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *bus;
	u16 pci_cmd, devctl;
	struct xmgmt *xm;
	u8 pci_bctl;
	int i, ret;

	xm = pci_get_drvdata(pdev);
	xmgmt_info(xm, "hot reset start");
	xmgmt_pci_save_config_all(xm);
	pci_disable_device(pdev);
	bus = pdev->bus;

	/*
	 * When flipping the SBR bit, device can fall off the bus. This is
	 * usually no problem at all so long as drivers are working properly
	 * after SBR. However, some systems complain bitterly when the device
	 * falls off the bus.
	 * The quick solution is to temporarily disable the SERR reporting of
	 * switch port during SBR.
	 */

	pci_read_config_word(bus->self, PCI_COMMAND, &pci_cmd);
	pci_write_config_word(bus->self, PCI_COMMAND, (pci_cmd & ~PCI_COMMAND_SERR));
	pcie_capability_read_word(bus->self, PCI_EXP_DEVCTL, &devctl);
	pcie_capability_write_word(bus->self, PCI_EXP_DEVCTL, (devctl & ~PCI_EXP_DEVCTL_FERE));
	pci_read_config_byte(bus->self, PCI_BRIDGE_CONTROL, &pci_bctl);
	pci_write_config_byte(bus->self, PCI_BRIDGE_CONTROL, pci_bctl | PCI_BRIDGE_CTL_BUS_RESET);
	msleep(100);
	pci_write_config_byte(bus->self, PCI_BRIDGE_CONTROL, pci_bctl);
	ssleep(1);

	pcie_capability_write_word(bus->self, PCI_EXP_DEVCTL, devctl);
	pci_write_config_word(bus->self, PCI_COMMAND, pci_cmd);

	ret = pci_enable_device(pdev);
	if (ret)
		xmgmt_err(xm, "failed to enable device, ret %d", ret);

	for (i = 0; i < 300; i++) {
		pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd != 0xffff)
			break;
		msleep(20);
	}
	if (i == 300)
		xmgmt_err(xm, "timed out waiting for device to be online after reset");

	xmgmt_info(xm, "waiting for %d ms", i * 20);
	xmgmt_pci_restore_config_all(xm);
	xmgmt_config_pci(xm);
}

static int xmgmt_add_vsec_node(struct xmgmt *xm, char *dtb)
{
	struct pci_dev *pdev = XMGMT_PDEV(xm);
	struct xrt_md_endpoint ep = { 0 };
	struct device *dev = DEV(pdev);
	u32 off_low, off_high, header;
	int cap = 0, ret = 0;
	__be32 vsec_bar;
	__be64 vsec_off;

	while ((cap = pci_find_next_ext_capability(pdev, cap, PCI_EXT_CAP_ID_VNDR))) {
		pci_read_config_dword(pdev, cap + PCI_VNDR_HEADER, &header);
		if (PCI_VNDR_HEADER_ID(header) == XRT_VSEC_ID)
			break;
	}
	if (!cap) {
		xmgmt_info(xm, "No Vendor Specific Capability.");
		return -ENOENT;
	}

	if (pci_read_config_dword(pdev, cap + 8, &off_low) ||
	    pci_read_config_dword(pdev, cap + 12, &off_high)) {
		xmgmt_err(xm, "pci_read vendor specific failed.");
		return -EINVAL;
	}

	ep.ep_name = XRT_MD_NODE_VSEC;
	ret = xrt_md_add_endpoint(dev, dtb, &ep);
	if (ret) {
		xmgmt_err(xm, "add vsec metadata failed, ret %d", ret);
		goto failed;
	}

	vsec_bar = cpu_to_be32(off_low & 0xf);
	ret = xrt_md_set_prop(dev, dtb, XRT_MD_NODE_VSEC, NULL,
			      XRT_MD_PROP_BAR_IDX, &vsec_bar, sizeof(vsec_bar));
	if (ret) {
		xmgmt_err(xm, "add vsec bar idx failed, ret %d", ret);
		goto failed;
	}

	vsec_off = cpu_to_be64(((u64)off_high << 32) | (off_low & ~0xfU));
	ret = xrt_md_set_prop(dev, dtb, XRT_MD_NODE_VSEC, NULL,
			      XRT_MD_PROP_OFFSET, &vsec_off, sizeof(vsec_off));
	if (ret) {
		xmgmt_err(xm, "add vsec offset failed, ret %d", ret);
		goto failed;
	}

failed:
	return ret;
}

static int xmgmt_create_root_metadata(struct xmgmt *xm, char **root_dtb)
{
	char *dtb = NULL;
	int ret;

	ret = xrt_md_create(XMGMT_DEV(xm), &dtb);
	if (ret) {
		xmgmt_err(xm, "create metadata failed, ret %d", ret);
		goto failed;
	}

	ret = xmgmt_add_vsec_node(xm, dtb);
	if (ret == -ENOENT) {
		/*
		 * We may be dealing with a MFG board.
		 * Try vsec-golden which will bring up all hard-coded leaves
		 * at hard-coded offsets.
		 */
		ret = xroot_add_simple_node(xm->root, dtb, XRT_MD_NODE_VSEC_GOLDEN);
	} else if (ret == 0) {
		ret = xroot_add_simple_node(xm->root, dtb, XRT_MD_NODE_MGMT_MAIN);
	}
	if (ret)
		goto failed;

	*root_dtb = dtb;
	return 0;

failed:
	vfree(dtb);
	return ret;
}

static ssize_t ready_show(struct device *dev,
			  struct device_attribute *da,
			  char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xmgmt *xm = pci_get_drvdata(pdev);

	return sprintf(buf, "%d\n", xm->ready);
}
static DEVICE_ATTR_RO(ready);

static struct attribute *xmgmt_root_attrs[] = {
	&dev_attr_ready.attr,
	NULL
};

static struct attribute_group xmgmt_root_attr_group = {
	.attrs = xmgmt_root_attrs,
};

static void xmgmt_root_get_id(struct device *dev, struct xrt_root_get_id *rid)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	rid->xpigi_vendor_id = pdev->vendor;
	rid->xpigi_device_id = pdev->device;
	rid->xpigi_sub_vendor_id = pdev->subsystem_vendor;
	rid->xpigi_sub_device_id = pdev->subsystem_device;
}

static int xmgmt_root_get_resource(struct device *dev, struct xrt_root_get_res *res)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xmgmt *xm;

	xm = pci_get_drvdata(pdev);
	if (res->xpigr_region_id > PCI_STD_RESOURCE_END) {
		xmgmt_err(xm, "Invalid bar idx %d", res->xpigr_region_id);
		return -EINVAL;
	}

	res->xpigr_res = &pdev->resource[res->xpigr_region_id];
	return 0;
}

static struct xroot_physical_function_callback xmgmt_xroot_pf_cb = {
	.xpc_get_id = xmgmt_root_get_id,
	.xpc_get_resource = xmgmt_root_get_resource,
	.xpc_hot_reset = xmgmt_root_hot_reset,
};

static int xmgmt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct xmgmt *xm = devm_kzalloc(dev, sizeof(*xm), GFP_KERNEL);
	char *dtb = NULL;

	if (!xm)
		return -ENOMEM;
	xm->pdev = pdev;
	pci_set_drvdata(pdev, xm);

	ret = xmgmt_config_pci(xm);
	if (ret)
		goto failed;

	ret = xroot_probe(&pdev->dev, &xmgmt_xroot_pf_cb, &xm->root);
	if (ret)
		goto failed;

	ret = xmgmt_create_root_metadata(xm, &dtb);
	if (ret)
		goto failed_metadata;

	ret = xroot_create_group(xm->root, dtb);
	vfree(dtb);
	if (ret)
		xmgmt_err(xm, "failed to create root group: %d", ret);

	if (!xroot_wait_for_bringup(xm->root))
		xmgmt_err(xm, "failed to bringup all groups");
	else
		xm->ready = true;

	ret = sysfs_create_group(&pdev->dev.kobj, &xmgmt_root_attr_group);
	if (ret) {
		/* Warning instead of failing the probe. */
		xmgmt_warn(xm, "create xmgmt root attrs failed: %d", ret);
	}

	xroot_broadcast(xm->root, XRT_EVENT_POST_CREATION);
	xmgmt_info(xm, "%s started successfully", XMGMT_MODULE_NAME);
	return 0;

failed_metadata:
	xroot_remove(xm->root);
failed:
	pci_set_drvdata(pdev, NULL);
	return ret;
}

static void xmgmt_remove(struct pci_dev *pdev)
{
	struct xmgmt *xm = pci_get_drvdata(pdev);

	xroot_broadcast(xm->root, XRT_EVENT_PRE_REMOVAL);
	sysfs_remove_group(&pdev->dev.kobj, &xmgmt_root_attr_group);
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

	res = xmgmt_register_leaf();
	if (res)
		return res;

	xmgmt_class = class_create(THIS_MODULE, XMGMT_MODULE_NAME);
	if (IS_ERR(xmgmt_class))
		return PTR_ERR(xmgmt_class);

	res = pci_register_driver(&xmgmt_driver);
	if (res) {
		class_destroy(xmgmt_class);
		return res;
	}

	return 0;
}

static __exit void xmgmt_exit(void)
{
	pci_unregister_driver(&xmgmt_driver);
	class_destroy(xmgmt_class);
	xmgmt_unregister_leaf();
}

module_init(xmgmt_init);
module_exit(xmgmt_exit);

MODULE_DEVICE_TABLE(pci, xmgmt_pci_ids);
MODULE_VERSION(XMGMT_DRIVER_VERSION);
MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo management function driver");
MODULE_LICENSE("GPL v2");
