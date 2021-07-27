// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Security Manager
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */

#include <linux/fpga/fpga-sec-mgr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define SEC_MGR_XA_LIMIT	XA_LIMIT(0, INT_MAX)
static DEFINE_XARRAY_ALLOC(fpga_sec_mgr_xa);

static struct class *fpga_sec_mgr_class;

#define to_sec_mgr(d) container_of(d, struct fpga_sec_mgr, dev)

/**
 * fpga_sec_mgr_register - create and register an FPGA
 *			   Security Manager device
 *
 * @dev:  fpga security manager device from pdev
 * @sops: pointer to a structure of fpga callback functions
 * @priv: fpga security manager private data
 *
 * Returns a struct fpga_sec_mgr pointer on success, or ERR_PTR() on
 * error. The caller of this function is responsible for calling
 * fpga_sec_mgr_unregister().
 */
struct fpga_sec_mgr *
fpga_sec_mgr_register(struct device *parent,
		      const struct fpga_sec_mgr_ops *sops, void *priv)
{
	struct fpga_sec_mgr *smgr;
	int id, ret;

	smgr = kzalloc(sizeof(*smgr), GFP_KERNEL);
	if (!smgr)
		return NULL;

	ret = xa_alloc(&fpga_sec_mgr_xa, &smgr->dev.id, smgr, SEC_MGR_XA_LIMIT,
		       GFP_KERNEL);
	if (ret)
		goto error_kfree;

	mutex_init(&smgr->lock);

	smgr->priv = priv;
	smgr->sops = sops;

	smgr->dev.class = fpga_sec_mgr_class;
	smgr->dev.parent = parent;

	ret = dev_set_name(&smgr->dev, "fpga_sec%d", id);
	if (ret) {
		dev_err(parent, "Failed to set device name: fpga_sec%d\n", id);
		goto error_device;
	}

	ret = device_register(&smgr->dev);
	if (ret) {
		put_device(&smgr->dev);
		return ERR_PTR(ret);
	}

	return smgr;

error_device:
	xa_erase(&fpga_sec_mgr_xa, smgr->dev.id);

error_kfree:
	kfree(smgr);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_register);

/**
 * fpga_sec_mgr_unregister - unregister an FPGA security manager
 *
 * @mgr: fpga manager struct
 *
 * This function is intended for use in an FPGA security manager
 * driver's remove() function.
 */
void fpga_sec_mgr_unregister(struct fpga_sec_mgr *smgr)
{
	device_unregister(&smgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_unregister);

static void fpga_sec_mgr_dev_release(struct device *dev)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);

	xa_erase(&fpga_sec_mgr_xa, smgr->dev.id);
	kfree(smgr);
}

static int __init fpga_sec_mgr_class_init(void)
{
	pr_info("FPGA Security Manager\n");

	fpga_sec_mgr_class = class_create(THIS_MODULE, "fpga_sec_mgr");
	if (IS_ERR(fpga_sec_mgr_class))
		return PTR_ERR(fpga_sec_mgr_class);

	fpga_sec_mgr_class->dev_release = fpga_sec_mgr_dev_release;

	return 0;
}

static void __exit fpga_sec_mgr_class_exit(void)
{
	class_destroy(fpga_sec_mgr_class);
	WARN_ON(!xa_empty(&fpga_sec_mgr_xa));
}

MODULE_DESCRIPTION("FPGA Security Manager Driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_sec_mgr_class_init);
module_exit(fpga_sec_mgr_class_exit)
