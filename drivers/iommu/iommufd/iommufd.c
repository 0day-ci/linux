// SPDX-License-Identifier: GPL-2.0-only
/*
 * I/O Address Space Management for passthrough devices
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Liu Yi L <yi.l.liu@intel.com>
 */

#define pr_fmt(fmt)    "iommufd: " fmt

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/iommu.h>

/* Per iommufd */
struct iommufd_ctx {
	refcount_t refs;
};

static int iommufd_fops_open(struct inode *inode, struct file *filep)
{
	struct iommufd_ctx *ictx;
	int ret = 0;

	ictx = kzalloc(sizeof(*ictx), GFP_KERNEL);
	if (!ictx)
		return -ENOMEM;

	refcount_set(&ictx->refs, 1);
	filep->private_data = ictx;

	return ret;
}

static void iommufd_ctx_put(struct iommufd_ctx *ictx)
{
	if (refcount_dec_and_test(&ictx->refs))
		kfree(ictx);
}

static int iommufd_fops_release(struct inode *inode, struct file *filep)
{
	struct iommufd_ctx *ictx = filep->private_data;

	filep->private_data = NULL;

	iommufd_ctx_put(ictx);

	return 0;
}

static long iommufd_fops_unl_ioctl(struct file *filep,
				   unsigned int cmd, unsigned long arg)
{
	struct iommufd_ctx *ictx = filep->private_data;
	long ret = -EINVAL;

	if (!ictx)
		return ret;

	switch (cmd) {
	default:
		pr_err_ratelimited("unsupported cmd %u\n", cmd);
		break;
	}
	return ret;
}

static const struct file_operations iommufd_fops = {
	.owner		= THIS_MODULE,
	.open		= iommufd_fops_open,
	.release	= iommufd_fops_release,
	.unlocked_ioctl	= iommufd_fops_unl_ioctl,
};

static struct miscdevice iommu_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "iommu",
	.fops = &iommufd_fops,
	.nodename = "iommu",
	.mode = 0666,
};

static int __init iommufd_init(void)
{
	int ret;

	ret = misc_register(&iommu_misc_dev);
	if (ret) {
		pr_err("failed to register misc device\n");
		return ret;
	}

	return 0;
}

static void __exit iommufd_exit(void)
{
	misc_deregister(&iommu_misc_dev);
}

module_init(iommufd_init);
module_exit(iommufd_exit);

MODULE_AUTHOR("Liu Yi L <yi.l.liu@intel.com>");
MODULE_DESCRIPTION("I/O Address Space Management for passthrough devices");
MODULE_LICENSE("GPL v2");
