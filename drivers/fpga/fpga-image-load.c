// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Image Load Class Driver
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */

#include <linux/fpga/fpga-image-load.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define IMAGE_LOAD_XA_LIMIT	XA_LIMIT(0, INT_MAX)
static DEFINE_XARRAY_ALLOC(fpga_image_load_xa);

static struct class *fpga_image_load_class;

#define to_image_load(d) container_of(d, struct fpga_image_load, dev)

/**
 * fpga_image_load_register - create and register an FPGA Image Load Device
 *
 * @parent: fpga image load device from pdev
 * @lops:   pointer to a structure of image load callback functions
 * @priv:   fpga image load private data
 *
 * Returns a struct fpga_image_load pointer on success, or ERR_PTR() on
 * error. The caller of this function is responsible for calling
 * fpga_image_load_unregister().
 */
struct fpga_image_load *
fpga_image_load_register(struct device *parent,
			 const struct fpga_image_load_ops *lops, void *priv)
{
	struct fpga_image_load *imgld;
	int id, ret;

	imgld = kzalloc(sizeof(*imgld), GFP_KERNEL);
	if (!imgld)
		return NULL;

	ret = xa_alloc(&fpga_image_load_xa, &imgld->dev.id, imgld, IMAGE_LOAD_XA_LIMIT,
		       GFP_KERNEL);
	if (ret)
		goto error_kfree;

	mutex_init(&imgld->lock);

	imgld->priv = priv;
	imgld->lops = lops;

	imgld->dev.class = fpga_image_load_class;
	imgld->dev.parent = parent;

	ret = dev_set_name(&imgld->dev, "fpga_image%d", id);
	if (ret) {
		dev_err(parent, "Failed to set device name: fpga_image%d\n", id);
		goto error_device;
	}

	ret = device_register(&imgld->dev);
	if (ret) {
		put_device(&imgld->dev);
		return ERR_PTR(ret);
	}

	return imgld;

error_device:
	xa_erase(&fpga_image_load_xa, imgld->dev.id);

error_kfree:
	kfree(imgld);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(fpga_image_load_register);

/**
 * fpga_image_load_unregister - unregister an FPGA image load device
 *
 * @imgld: pointer to struct fpga_image_load
 *
 * This function is intended for use in an FPGA Image Load driver's
 * remove() function.
 */
void fpga_image_load_unregister(struct fpga_image_load *imgld)
{
	device_unregister(&imgld->dev);
}
EXPORT_SYMBOL_GPL(fpga_image_load_unregister);

static void fpga_image_load_dev_release(struct device *dev)
{
	struct fpga_image_load *imgld = to_image_load(dev);

	xa_erase(&fpga_image_load_xa, imgld->dev.id);
	kfree(imgld);
}

static int __init fpga_image_load_class_init(void)
{
	pr_info("FPGA Image Load Driver\n");

	fpga_image_load_class = class_create(THIS_MODULE, "fpga_image_load");
	if (IS_ERR(fpga_image_load_class))
		return PTR_ERR(fpga_image_load_class);

	fpga_image_load_class->dev_release = fpga_image_load_dev_release;

	return 0;
}

static void __exit fpga_image_load_class_exit(void)
{
	class_destroy(fpga_image_load_class);
	WARN_ON(!xa_empty(&fpga_image_load_xa));
}

MODULE_DESCRIPTION("FPGA Image Load Driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_image_load_class_init);
module_exit(fpga_image_load_class_exit)
