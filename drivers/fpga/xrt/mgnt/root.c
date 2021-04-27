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
#include "xmgnt.h"
#include "metadata.h"

#define XMGNT_MODULE_NAME	"xrt-mgnt"
#define XMGNT_DRIVER_VERSION	"4.0.0"

#define XMGNT_PDEV(xm)		((xm)->pdev)
#define XMGNT_DEV(xm)		(&(XMGNT_PDEV(xm)->dev))
#define xmgnt_err(xm, fmt, args...)	\
	dev_err(XMGNT_DEV(xm), "%s: " fmt, __func__, ##args)
#define xmgnt_warn(xm, fmt, args...)	\
	dev_warn(XMGNT_DEV(xm), "%s: " fmt, __func__, ##args)
#define xmgnt_info(xm, fmt, args...)	\
	dev_info(XMGNT_DEV(xm), "%s: " fmt, __func__, ##args)
#define xmgnt_dbg(xm, fmt, args...)	\
	dev_dbg(XMGNT_DEV(xm), "%s: " fmt, __func__, ##args)
#define XMGNT_DEV_ID(_pcidev)			\
	({ typeof(_pcidev) (pcidev) = (_pcidev);	\
	((pci_domain_nr((pcidev)->bus) << 16) |	\
	PCI_DEVID((pcidev)->bus->number, 0)); })
#define XRT_VSEC_ID		0x20
#define XRT_MAX_READRQ		512

static struct class *xmgnt_class;

/* PCI Device IDs */
/*
 * Golden image is preloaded on the device when it is shipped to customer.
 * Then, customer can load other shells (from Xilinx or some other vendor).
 * If something goes wrong with the shell, customer can always go back to
 * golden and start over again.
 */
#define PCI_DEVICE_ID_U50_GOLDEN	0xD020
#define PCI_DEVICE_ID_U50		0x5020
static const struct pci_device_id xmgnt_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_U50_GOLDEN), }, /* Alveo U50 (golden) */
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_U50), }, /* Alveo U50 */
	{ 0, }
};

struct xmgnt {
	struct pci_dev *pdev;
	void *root;

	bool ready;
};

static int xmgnt_config_pci(struct xmgnt *xm)
{
	struct pci_dev *pdev = XMGNT_PDEV(xm);
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc < 0) {
		xmgnt_err(xm, "failed to enable device: %d", rc);
		return rc;
	}

	rc = pci_enable_pcie_error_reporting(pdev);
	if (rc)
		xmgnt_warn(xm, "failed to enable AER: %d", rc);

	pci_set_master(pdev);

	rc = pcie_get_readrq(pdev);
	if (rc > XRT_MAX_READRQ)
		pcie_set_readrq(pdev, XRT_MAX_READRQ);
	return 0;
}

static int xmgnt_match_slot_and_save(struct device *dev, void *data)
{
	struct xmgnt *xm = data;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (XMGNT_DEV_ID(pdev) == XMGNT_DEV_ID(xm->pdev)) {
		pci_cfg_access_lock(pdev);
		pci_save_state(pdev);
	}

	return 0;
}

static void xmgnt_pci_save_config_all(struct xmgnt *xm)
{
	bus_for_each_dev(&pci_bus_type, NULL, xm, xmgnt_match_slot_and_save);
}

static int xmgnt_match_slot_and_restore(struct device *dev, void *data)
{
	struct xmgnt *xm = data;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (XMGNT_DEV_ID(pdev) == XMGNT_DEV_ID(xm->pdev)) {
		pci_restore_state(pdev);
		pci_cfg_access_unlock(pdev);
	}

	return 0;
}

static void xmgnt_pci_restore_config_all(struct xmgnt *xm)
{
	bus_for_each_dev(&pci_bus_type, NULL, xm, xmgnt_match_slot_and_restore);
}

static void xmgnt_root_hot_reset(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *bus;
	u16 pci_cmd, devctl;
	struct xmgnt *xm;
	u8 pci_bctl;
	int i, ret;

	xm = pci_get_drvdata(pdev);
	xmgnt_info(xm, "hot reset start");
	xmgnt_pci_save_config_all(xm);
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
		xmgnt_err(xm, "failed to enable device, ret %d", ret);

	for (i = 0; i < 300; i++) {
		pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd != 0xffff)
			break;
		msleep(20);
	}
	if (i == 300)
		xmgnt_err(xm, "timed out waiting for device to be online after reset");

	xmgnt_info(xm, "waiting for %d ms", i * 20);
	xmgnt_pci_restore_config_all(xm);
	xmgnt_config_pci(xm);
}

static int xmgnt_add_vsec_node(struct xmgnt *xm, char *dtb)
{
	u32 off_low, off_high, vsec_bar, header;
	struct pci_dev *pdev = XMGNT_PDEV(xm);
	struct xrt_md_endpoint ep = { 0 };
	struct device *dev = DEV(pdev);
	int cap = 0, ret = 0;
	u64 vsec_off;

	while ((cap = pci_find_next_ext_capability(pdev, cap, PCI_EXT_CAP_ID_VNDR))) {
		pci_read_config_dword(pdev, cap + PCI_VNDR_HEADER, &header);
		if (PCI_VNDR_HEADER_ID(header) == XRT_VSEC_ID)
			break;
	}
	if (!cap) {
		xmgnt_info(xm, "No Vendor Specific Capability.");
		return -ENOENT;
	}

	if (pci_read_config_dword(pdev, cap + 8, &off_low) ||
	    pci_read_config_dword(pdev, cap + 12, &off_high)) {
		xmgnt_err(xm, "pci_read vendor specific failed.");
		return -EINVAL;
	}

	ep.ep_name = XRT_MD_NODE_VSEC;
	ret = xrt_md_add_endpoint(dev, dtb, &ep);
	if (ret) {
		xmgnt_err(xm, "add vsec metadata failed, ret %d", ret);
		goto failed;
	}

	vsec_bar = cpu_to_be32(off_low & 0xf);
	ret = xrt_md_set_prop(dev, dtb, XRT_MD_NODE_VSEC, NULL,
			      XRT_MD_PROP_BAR_IDX, &vsec_bar, sizeof(vsec_bar));
	if (ret) {
		xmgnt_err(xm, "add vsec bar idx failed, ret %d", ret);
		goto failed;
	}

	vsec_off = cpu_to_be64(((u64)off_high << 32) | (off_low & ~0xfU));
	ret = xrt_md_set_prop(dev, dtb, XRT_MD_NODE_VSEC, NULL,
			      XRT_MD_PROP_OFFSET, &vsec_off, sizeof(vsec_off));
	if (ret) {
		xmgnt_err(xm, "add vsec offset failed, ret %d", ret);
		goto failed;
	}

failed:
	return ret;
}

static int xmgnt_create_root_metadata(struct xmgnt *xm, char **root_dtb)
{
	char *dtb = NULL;
	int ret;

	ret = xrt_md_create(XMGNT_DEV(xm), &dtb);
	if (ret) {
		xmgnt_err(xm, "create metadata failed, ret %d", ret);
		goto failed;
	}

	ret = xmgnt_add_vsec_node(xm, dtb);
	if (ret == -ENOENT) {
		/*
		 * We may be dealing with a MFG board.
		 * Try vsec-golden which will bring up all hard-coded leaves
		 * at hard-coded offsets.
		 */
		ret = xroot_add_simple_node(xm->root, dtb, XRT_MD_NODE_VSEC_GOLDEN);
	} else if (ret == 0) {
		ret = xroot_add_simple_node(xm->root, dtb, XRT_MD_NODE_MGNT_MAIN);
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
	struct xmgnt *xm = pci_get_drvdata(pdev);

	return sprintf(buf, "%d\n", xm->ready);
}
static DEVICE_ATTR_RO(ready);

static struct attribute *xmgnt_root_attrs[] = {
	&dev_attr_ready.attr,
	NULL
};

static struct attribute_group xmgnt_root_attr_group = {
	.attrs = xmgnt_root_attrs,
};

static void xmgnt_root_get_id(struct device *dev, struct xrt_root_get_id *rid)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	rid->xpigi_vendor_id = pdev->vendor;
	rid->xpigi_device_id = pdev->device;
	rid->xpigi_sub_vendor_id = pdev->subsystem_vendor;
	rid->xpigi_sub_device_id = pdev->subsystem_device;
}

static int xmgnt_root_get_resource(struct device *dev, struct xrt_root_get_res *res)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xmgnt *xm;

	xm = pci_get_drvdata(pdev);
	if (res->xpigr_region_id > PCI_STD_RESOURCE_END) {
		xmgnt_err(xm, "Invalid bar idx %d", res->xpigr_region_id);
		return -EINVAL;
	}

	res->xpigr_res = &pdev->resource[res->xpigr_region_id];
	return 0;
}

static struct xroot_physical_function_callback xmgnt_xroot_pf_cb = {
	.xpc_get_id = xmgnt_root_get_id,
	.xpc_get_resource = xmgnt_root_get_resource,
	.xpc_hot_reset = xmgnt_root_hot_reset,
};

static int xmgnt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct xmgnt *xm = devm_kzalloc(dev, sizeof(*xm), GFP_KERNEL);
	char *dtb = NULL;

	if (!xm)
		return -ENOMEM;
	xm->pdev = pdev;
	pci_set_drvdata(pdev, xm);

	ret = xmgnt_config_pci(xm);
	if (ret)
		goto failed;

	ret = xroot_probe(&pdev->dev, &xmgnt_xroot_pf_cb, &xm->root);
	if (ret)
		goto failed;

	ret = xmgnt_create_root_metadata(xm, &dtb);
	if (ret)
		goto failed_metadata;

	ret = xroot_create_group(xm->root, dtb);
	vfree(dtb);
	if (ret)
		xmgnt_err(xm, "failed to create root group: %d", ret);

	if (!xroot_wait_for_bringup(xm->root))
		xmgnt_err(xm, "failed to bringup all groups");
	else
		xm->ready = true;

	ret = sysfs_create_group(&pdev->dev.kobj, &xmgnt_root_attr_group);
	if (ret) {
		/* Warning instead of failing the probe. */
		xmgnt_warn(xm, "create xmgnt root attrs failed: %d", ret);
	}

	xroot_broadcast(xm->root, XRT_EVENT_POST_CREATION);
	xmgnt_info(xm, "%s started successfully", XMGNT_MODULE_NAME);
	return 0;

failed_metadata:
	xroot_remove(xm->root);
failed:
	pci_set_drvdata(pdev, NULL);
	return ret;
}

static void xmgnt_remove(struct pci_dev *pdev)
{
	struct xmgnt *xm = pci_get_drvdata(pdev);

	xroot_broadcast(xm->root, XRT_EVENT_PRE_REMOVAL);
	sysfs_remove_group(&pdev->dev.kobj, &xmgnt_root_attr_group);
	xroot_remove(xm->root);
	pci_disable_pcie_error_reporting(xm->pdev);
	xmgnt_info(xm, "%s cleaned up successfully", XMGNT_MODULE_NAME);
}

static struct pci_driver xmgnt_driver = {
	.name = XMGNT_MODULE_NAME,
	.id_table = xmgnt_pci_ids,
	.probe = xmgnt_probe,
	.remove = xmgnt_remove,
};

static int __init xmgnt_init(void)
{
	int res = 0;

	res = xmgnt_register_leaf();
	if (res)
		return res;

	xmgnt_class = class_create(THIS_MODULE, XMGNT_MODULE_NAME);
	if (IS_ERR(xmgnt_class))
		return PTR_ERR(xmgnt_class);

	res = pci_register_driver(&xmgnt_driver);
	if (res) {
		class_destroy(xmgnt_class);
		return res;
	}

	return 0;
}

static __exit void xmgnt_exit(void)
{
	pci_unregister_driver(&xmgnt_driver);
	class_destroy(xmgnt_class);
	xmgnt_unregister_leaf();
}

module_init(xmgnt_init);
module_exit(xmgnt_exit);

MODULE_DEVICE_TABLE(pci, xmgnt_pci_ids);
MODULE_VERSION(XMGNT_DRIVER_VERSION);
MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo management function driver");
MODULE_LICENSE("GPL v2");
