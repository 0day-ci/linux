// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware Upload Framework
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */

#include <linux/firmware/firmware-upload.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define FW_UPLOAD_XA_LIMIT	XA_LIMIT(0, INT_MAX)
static DEFINE_XARRAY_ALLOC(fw_upload_xa);

static struct class *fw_upload_class;

#define to_fw_upload(d) container_of(d, struct fw_upload, dev)

/**
 * fw_upload_register - create and register a Firmware Upload Device
 *
 * @parent: Firmware Upload device from pdev
 * @ops:    Pointer to a structure of firmware upload callback functions
 * @priv:   Firmware Upload private data
 *
 * Returns a struct fw_upload pointer on success, or ERR_PTR() on
 * error. The caller of this function is responsible for calling
 * fw_upload_unregister().
 */
struct fw_upload *
fw_upload_register(struct device *parent, const struct fw_upload_ops *ops,
		   void *priv)
{
	struct fw_upload *fwl;
	int ret;

	fwl = kzalloc(sizeof(*fwl), GFP_KERNEL);
	if (!fwl)
		return ERR_PTR(-ENOMEM);

	ret = xa_alloc(&fw_upload_xa, &fwl->dev.id, fwl, FW_UPLOAD_XA_LIMIT,
		       GFP_KERNEL);
	if (ret)
		goto error_kfree;

	mutex_init(&fwl->lock);

	fwl->priv = priv;
	fwl->ops = ops;

	fwl->dev.class = fw_upload_class;
	fwl->dev.parent = parent;

	ret = dev_set_name(&fwl->dev, "fw_upload%d", fwl->dev.id);
	if (ret) {
		dev_err(parent, "Failed to set device name: fw_upload%d\n",
			fwl->dev.id);
		goto error_device;
	}

	ret = device_register(&fwl->dev);
	if (ret) {
		put_device(&fwl->dev);
		return ERR_PTR(ret);
	}

	return fwl;

error_device:
	xa_erase(&fw_upload_xa, fwl->dev.id);

error_kfree:
	kfree(fwl);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(fw_upload_register);

/**
 * fw_upload_unregister - unregister a Firmware Upload device
 *
 * @fwl: pointer to struct fw_upload
 *
 * This function is intended for use in the parent driver's remove()
 * function.
 */
void fw_upload_unregister(struct fw_upload *fwl)
{
	device_unregister(&fwl->dev);
}
EXPORT_SYMBOL_GPL(fw_upload_unregister);

static void fw_upload_dev_release(struct device *dev)
{
	struct fw_upload *fwl = to_fw_upload(dev);

	xa_erase(&fw_upload_xa, fwl->dev.id);
	kfree(fwl);
}

static int __init fw_upload_class_init(void)
{
	pr_info("Firmware Upload Framework\n");

	fw_upload_class = class_create(THIS_MODULE, "fw_upload");
	if (IS_ERR(fw_upload_class))
		return PTR_ERR(fw_upload_class);

	fw_upload_class->dev_release = fw_upload_dev_release;

	return 0;
}

static void __exit fw_upload_class_exit(void)
{
	class_destroy(fw_upload_class);
	WARN_ON(!xa_empty(&fw_upload_xa));
}

MODULE_DESCRIPTION("Firmware Upload Framework");
MODULE_LICENSE("GPL v2");

subsys_initcall(fw_upload_class_init);
module_exit(fw_upload_class_exit)
