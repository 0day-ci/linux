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
#include <linux/iommufd.h>
#include <linux/xarray.h>
#include <asm-generic/bug.h>

/* Per iommufd */
struct iommufd_ctx {
	refcount_t refs;
	struct mutex lock;
	struct xarray device_xa; /* xarray of bound devices */
};

/*
 * A iommufd_device object represents the binding relationship
 * between iommufd and device. It is created per a successful
 * binding request from device driver. The bound device must be
 * a physical device so far. Subdevice will be supported later
 * (with additional PASID information). An user-assigned cookie
 * is also recorded to mark the device in the /dev/iommu uAPI.
 */
struct iommufd_device {
	unsigned int id;
	struct iommufd_ctx *ictx;
	struct device *dev; /* always be the physical device */
	u64 dev_cookie;
};

static int iommufd_fops_open(struct inode *inode, struct file *filep)
{
	struct iommufd_ctx *ictx;
	int ret = 0;

	ictx = kzalloc(sizeof(*ictx), GFP_KERNEL);
	if (!ictx)
		return -ENOMEM;

	refcount_set(&ictx->refs, 1);
	mutex_init(&ictx->lock);
	xa_init_flags(&ictx->device_xa, XA_FLAGS_ALLOC);
	filep->private_data = ictx;

	return ret;
}

static void iommufd_ctx_get(struct iommufd_ctx *ictx)
{
	refcount_inc(&ictx->refs);
}

static const struct file_operations iommufd_fops;

/**
 * iommufd_ctx_fdget - Acquires a reference to the internal iommufd context.
 * @fd: [in] iommufd file descriptor.
 *
 * Returns a pointer to the iommufd context, otherwise NULL;
 *
 */
static struct iommufd_ctx *iommufd_ctx_fdget(int fd)
{
	struct fd f = fdget(fd);
	struct file *file = f.file;
	struct iommufd_ctx *ictx;

	if (!file)
		return NULL;

	if (file->f_op != &iommufd_fops)
		return NULL;

	ictx = file->private_data;
	if (ictx)
		iommufd_ctx_get(ictx);
	fdput(f);
	return ictx;
}

/**
 * iommufd_ctx_put - Releases a reference to the internal iommufd context.
 * @ictx: [in] Pointer to iommufd context.
 *
 */
static void iommufd_ctx_put(struct iommufd_ctx *ictx)
{
	if (!refcount_dec_and_test(&ictx->refs))
		return;

	WARN_ON(!xa_empty(&ictx->device_xa));
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

/**
 * iommufd_bind_device - Bind a physical device marked by a device
 *			 cookie to an iommu fd.
 * @fd:		[in] iommufd file descriptor.
 * @dev:	[in] Pointer to a physical device struct.
 * @dev_cookie:	[in] A cookie to mark the device in /dev/iommu uAPI.
 *
 * A successful bind establishes a security context for the device
 * and returns struct iommufd_device pointer. Otherwise returns
 * error pointer.
 *
 */
struct iommufd_device *iommufd_bind_device(int fd, struct device *dev,
					   u64 dev_cookie)
{
	struct iommufd_ctx *ictx;
	struct iommufd_device *idev;
	unsigned long index;
	unsigned int id;
	int ret;

	ictx = iommufd_ctx_fdget(fd);
	if (!ictx)
		return ERR_PTR(-EINVAL);

	mutex_lock(&ictx->lock);

	/* check duplicate registration */
	xa_for_each(&ictx->device_xa, index, idev) {
		if (idev->dev == dev || idev->dev_cookie == dev_cookie) {
			idev = ERR_PTR(-EBUSY);
			goto out_unlock;
		}
	}

	idev = kzalloc(sizeof(*idev), GFP_KERNEL);
	if (!idev) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* Establish the security context */
	ret = iommu_device_init_user_dma(dev, (unsigned long)ictx);
	if (ret)
		goto out_free;

	ret = xa_alloc(&ictx->device_xa, &id, idev,
		       XA_LIMIT(IOMMUFD_DEVID_MIN, IOMMUFD_DEVID_MAX),
		       GFP_KERNEL);
	if (ret) {
		idev = ERR_PTR(ret);
		goto out_user_dma;
	}

	idev->ictx = ictx;
	idev->dev = dev;
	idev->dev_cookie = dev_cookie;
	idev->id = id;
	mutex_unlock(&ictx->lock);

	return idev;
out_user_dma:
	iommu_device_exit_user_dma(idev->dev);
out_free:
	kfree(idev);
out_unlock:
	mutex_unlock(&ictx->lock);
	iommufd_ctx_put(ictx);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(iommufd_bind_device);

/**
 * iommufd_unbind_device - Unbind a physical device from iommufd
 *
 * @idev: [in] Pointer to the internal iommufd_device struct.
 *
 */
void iommufd_unbind_device(struct iommufd_device *idev)
{
	struct iommufd_ctx *ictx = idev->ictx;

	mutex_lock(&ictx->lock);
	xa_erase(&ictx->device_xa, idev->id);
	mutex_unlock(&ictx->lock);
	/* Exit the security context */
	iommu_device_exit_user_dma(idev->dev);
	kfree(idev);
	iommufd_ctx_put(ictx);
}
EXPORT_SYMBOL_GPL(iommufd_unbind_device);

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
