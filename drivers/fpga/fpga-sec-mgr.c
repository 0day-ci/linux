// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Security Manager
 *
 * Copyright (C) 2019-2020 Intel Corporation, Inc.
 */

#include <linux/fpga/fpga-sec-mgr.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

static DEFINE_IDA(fpga_sec_mgr_ida);
static struct class *fpga_sec_mgr_class;

struct fpga_sec_mgr_devres {
	struct fpga_sec_mgr *smgr;
};

#define to_sec_mgr(d) container_of(d, struct fpga_sec_mgr, dev)

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fpga_sec_mgr *smgr = to_sec_mgr(dev);

	return sysfs_emit(buf, "%s\n", smgr->name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *sec_mgr_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static struct attribute_group sec_mgr_attr_group = {
	.attrs = sec_mgr_attrs,
};

static const struct attribute_group *fpga_sec_mgr_attr_groups[] = {
	&sec_mgr_attr_group,
	NULL,
};

/**
 * fpga_sec_mgr_create - create and initialize an FPGA
 *			  security manager struct
 *
 * @dev:  fpga security manager device from pdev
 * @name: fpga security manager name
 * @sops: pointer to a structure of fpga callback functions
 * @priv: fpga security manager private data
 *
 * The caller of this function is responsible for freeing the struct
 * with ifpg_sec_mgr_free(). Using devm_fpga_sec_mgr_create() instead
 * is recommended.
 *
 * Return: pointer to struct fpga_sec_mgr or NULL
 */
struct fpga_sec_mgr *
fpga_sec_mgr_create(struct device *dev, const char *name,
		    const struct fpga_sec_mgr_ops *sops, void *priv)
{
	struct fpga_sec_mgr *smgr;
	int id, ret;

	if (!name || !strlen(name)) {
		dev_err(dev, "Attempt to register with no name!\n");
		return NULL;
	}

	smgr = kzalloc(sizeof(*smgr), GFP_KERNEL);
	if (!smgr)
		return NULL;

	id = ida_simple_get(&fpga_sec_mgr_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		goto error_kfree;

	mutex_init(&smgr->lock);

	smgr->name = name;
	smgr->priv = priv;
	smgr->sops = sops;

	device_initialize(&smgr->dev);
	smgr->dev.class = fpga_sec_mgr_class;
	smgr->dev.parent = dev;
	smgr->dev.id = id;

	ret = dev_set_name(&smgr->dev, "fpga_sec%d", id);
	if (ret) {
		dev_err(dev, "Failed to set device name: fpga_sec%d\n", id);
		goto error_device;
	}

	return smgr;

error_device:
	ida_simple_remove(&fpga_sec_mgr_ida, id);

error_kfree:
	kfree(smgr);

	return NULL;
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_create);

/**
 * fpga_sec_mgr_free - free an FPGA security manager created
 *			with fpga_sec_mgr_create()
 *
 * @smgr:	FPGA security manager structure
 */
void fpga_sec_mgr_free(struct fpga_sec_mgr *smgr)
{
	ida_simple_remove(&fpga_sec_mgr_ida, smgr->dev.id);
	kfree(smgr);
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_free);

static void devm_fpga_sec_mgr_release(struct device *dev, void *res)
{
	struct fpga_sec_mgr_devres *dr = res;

	fpga_sec_mgr_free(dr->smgr);
}

/**
 * devm_fpga_sec_mgr_create - create and initialize an FPGA
 *			       security manager struct
 *
 * @dev:  fpga security manager device from pdev
 * @name: fpga security manager name
 * @sops: pointer to a structure of fpga callback functions
 * @priv: fpga security manager private data
 *
 * This function is intended for use in a FPGA Security manager
 * driver's probe function.  After the security manager driver creates
 * the fpga_sec_mgr struct with devm_fpga_sec_mgr_create(), it should
 * register it with devm_fpga_sec_mgr_register().
 * The fpga_sec_mgr struct allocated with this function will be freed
 * automatically on driver detach.
 *
 * Return: pointer to struct fpga_sec_mgr or NULL
 */
struct fpga_sec_mgr *
devm_fpga_sec_mgr_create(struct device *dev, const char *name,
			 const struct fpga_sec_mgr_ops *sops, void *priv)
{
	struct fpga_sec_mgr_devres *dr;

	dr = devres_alloc(devm_fpga_sec_mgr_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return NULL;

	dr->smgr = fpga_sec_mgr_create(dev, name, sops, priv);
	if (!dr->smgr) {
		devres_free(dr);
		return NULL;
	}

	devres_add(dev, dr);

	return dr->smgr;
}
EXPORT_SYMBOL_GPL(devm_fpga_sec_mgr_create);

/**
 * fpga_sec_mgr_register - register an FPGA security manager
 *
 * @smgr: fpga security manager struct
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_sec_mgr_register(struct fpga_sec_mgr *smgr)
{
	int ret;

	ret = device_add(&smgr->dev);
	if (ret)
		goto error_device;

	dev_info(&smgr->dev, "%s registered\n", smgr->name);

	return 0;

error_device:
	ida_simple_remove(&fpga_sec_mgr_ida, smgr->dev.id);

	return ret;
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
	dev_info(&smgr->dev, "%s %s\n", __func__, smgr->name);

	device_unregister(&smgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_sec_mgr_unregister);

static int fpga_sec_mgr_devres_match(struct device *dev, void *res,
				     void *match_data)
{
	struct fpga_sec_mgr_devres *dr = res;

	return match_data == dr->smgr;
}

static void devm_fpga_sec_mgr_unregister(struct device *dev, void *res)
{
	struct fpga_sec_mgr_devres *dr = res;

	fpga_sec_mgr_unregister(dr->smgr);
}

/**
 * devm_fpga_sec_mgr_register - resource managed variant of
 *				fpga_sec_mgr_register()
 *
 * @dev: managing device for this FPGA security manager
 * @smgr: fpga security manager struct
 *
 * This is the devres variant of fpga_sec_mgr_register() for which the
 * unregister function will be called automatically when the managing
 * device is detached.
 */
int devm_fpga_sec_mgr_register(struct device *dev, struct fpga_sec_mgr *smgr)
{
	struct fpga_sec_mgr_devres *dr;
	int ret;

	/*
	 * Make sure that the struct fpga_sec_mgr * that is passed in is
	 * managed itself.
	 */
	if (WARN_ON(!devres_find(dev, devm_fpga_sec_mgr_release,
				 fpga_sec_mgr_devres_match, smgr)))
		return -EINVAL;

	dr = devres_alloc(devm_fpga_sec_mgr_unregister, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = fpga_sec_mgr_register(smgr);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	dr->smgr = smgr;
	devres_add(dev, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_fpga_sec_mgr_register);

static void fpga_sec_mgr_dev_release(struct device *dev)
{
}

static int __init fpga_sec_mgr_class_init(void)
{
	pr_info("FPGA Security Manager\n");

	fpga_sec_mgr_class = class_create(THIS_MODULE, "fpga_sec_mgr");
	if (IS_ERR(fpga_sec_mgr_class))
		return PTR_ERR(fpga_sec_mgr_class);

	fpga_sec_mgr_class->dev_groups = fpga_sec_mgr_attr_groups;
	fpga_sec_mgr_class->dev_release = fpga_sec_mgr_dev_release;

	return 0;
}

static void __exit fpga_sec_mgr_class_exit(void)
{
	class_destroy(fpga_sec_mgr_class);
	ida_destroy(&fpga_sec_mgr_ida);
}

MODULE_DESCRIPTION("FPGA Security Manager Driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_sec_mgr_class_init);
module_exit(fpga_sec_mgr_class_exit)
