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
#include <linux/vfio.h>

/* Per iommufd */
struct iommufd_ctx {
	refcount_t refs;
	struct mutex lock;
	struct xarray ioasid_xa; /* xarray of ioasids */
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

/* Represent an I/O address space */
struct iommufd_ioas {
	int ioasid;
	u32 type;
	u32 addr_width;
	bool enforce_snoop;
	struct iommufd_ctx *ictx;
	refcount_t refs;
	struct mutex lock;
	struct list_head device_list;
	struct iommu_domain *domain;
	struct vfio_iommu *vfio_iommu; /* FIXME: added for reusing vfio_iommu_type1 code */
};

/*
 * An ioas_device_info object is created per each successful attaching
 * request. A list of objects are maintained per ioas when the address
 * space is shared by multiple devices.
 */
struct ioas_device_info {
	struct iommufd_device *idev;
	struct list_head next;
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
	xa_init_flags(&ictx->ioasid_xa, XA_FLAGS_ALLOC);
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

	WARN_ON(!xa_empty(&ictx->ioasid_xa));
	WARN_ON(!xa_empty(&ictx->device_xa));
	kfree(ictx);
}

static struct iommufd_ioas *ioasid_get_ioas(struct iommufd_ctx *ictx, int ioasid)
{
	struct iommufd_ioas *ioas;

	if (ioasid < 0)
		return NULL;

	mutex_lock(&ictx->lock);
	ioas = xa_load(&ictx->ioasid_xa, ioasid);
	if (ioas)
		refcount_inc(&ioas->refs);
	mutex_unlock(&ictx->lock);
	return ioas;
}

/* Caller should hold ictx->lock */
static void ioas_put_locked(struct iommufd_ioas *ioas)
{
	struct iommufd_ctx *ictx = ioas->ictx;
	int ioasid = ioas->ioasid;

	if (!refcount_dec_and_test(&ioas->refs))
		return;

	WARN_ON(!list_empty(&ioas->device_list));
	vfio_iommu_type1_release(ioas->vfio_iommu); /* FIXME: reused vfio code */
	xa_erase(&ictx->ioasid_xa, ioasid);
	iommufd_ctx_put(ictx);
	kfree(ioas);
}

/*
 * Caller should hold a ictx reference when calling this function
 * otherwise ictx might be freed in ioas_put_locked() then the last
 * unlock becomes problematic. Alternatively we could have a fresh
 * implementation of ioas_put instead of calling the locked function.
 * In this case it can ensure ictx is freed after mutext_unlock().
 */
static void ioas_put(struct iommufd_ioas *ioas)
{
	struct iommufd_ctx *ictx = ioas->ictx;

	mutex_lock(&ictx->lock);
	ioas_put_locked(ioas);
	mutex_unlock(&ictx->lock);
}

static int iommufd_ioasid_alloc(struct iommufd_ctx *ictx, unsigned long arg)
{
	struct iommu_ioasid_alloc req;
	struct iommufd_ioas *ioas;
	unsigned long minsz;
	int ioasid, ret;
	struct vfio_iommu *vfio_iommu;

	minsz = offsetofend(struct iommu_ioasid_alloc, addr_width);

	if (copy_from_user(&req, (void __user *)arg, minsz))
		return -EFAULT;

	if (req.argsz < minsz || !req.addr_width ||
	    req.flags != IOMMU_IOASID_ENFORCE_SNOOP ||
	    req.type != IOMMU_IOASID_TYPE_KERNEL_TYPE1V2)
		return -EINVAL;

	ioas = kzalloc(sizeof(*ioas), GFP_KERNEL);
	if (!ioas)
		return -ENOMEM;

	mutex_lock(&ictx->lock);
	ret = xa_alloc(&ictx->ioasid_xa, &ioasid, ioas,
		       XA_LIMIT(IOMMUFD_IOASID_MIN, IOMMUFD_IOASID_MAX),
		       GFP_KERNEL);
	mutex_unlock(&ictx->lock);
	if (ret) {
		pr_err_ratelimited("Failed to alloc ioasid\n");
		kfree(ioas);
		return ret;
	}

	/* FIXME: get a vfio_iommu object for dma map/unmap management */
	vfio_iommu = vfio_iommu_type1_open(VFIO_TYPE1v2_IOMMU);
	if (IS_ERR(vfio_iommu)) {
		pr_err_ratelimited("Failed to get vfio_iommu object\n");
		mutex_lock(&ictx->lock);
		xa_erase(&ictx->ioasid_xa, ioasid);
		mutex_unlock(&ictx->lock);
		kfree(ioas);
		return PTR_ERR(vfio_iommu);
	}
	ioas->vfio_iommu = vfio_iommu;

	ioas->ioasid = ioasid;

	/* only supports kernel managed I/O page table so far */
	ioas->type = IOMMU_IOASID_TYPE_KERNEL_TYPE1V2;

	ioas->addr_width = req.addr_width;

	/* only supports enforce snoop today */
	ioas->enforce_snoop = true;

	iommufd_ctx_get(ictx);
	ioas->ictx = ictx;

	mutex_init(&ioas->lock);
	INIT_LIST_HEAD(&ioas->device_list);

	refcount_set(&ioas->refs, 1);

	return ioasid;
}

static int iommufd_ioasid_free(struct iommufd_ctx *ictx, unsigned long arg)
{
	struct iommufd_ioas *ioas = NULL;
	int ioasid, ret;

	if (copy_from_user(&ioasid, (void __user *)arg, sizeof(ioasid)))
		return -EFAULT;

	if (ioasid < 0)
		return -EINVAL;

	mutex_lock(&ictx->lock);
	ioas = xa_load(&ictx->ioasid_xa, ioasid);
	if (IS_ERR(ioas)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	/* Disallow free if refcount is not 1 */
	if (refcount_read(&ioas->refs) > 1) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ioas_put_locked(ioas);
out_unlock:
	mutex_unlock(&ictx->lock);
	return ret;
};

static int iommufd_fops_release(struct inode *inode, struct file *filep)
{
	struct iommufd_ctx *ictx = filep->private_data;
	struct iommufd_ioas *ioas;
	unsigned long index;

	filep->private_data = NULL;

	mutex_lock(&ictx->lock);
	xa_for_each(&ictx->ioasid_xa, index, ioas)
		ioas_put_locked(ioas);
	mutex_unlock(&ictx->lock);

	iommufd_ctx_put(ictx);

	return 0;
}

static struct device *
iommu_find_device_from_cookie(struct iommufd_ctx *ictx, u64 dev_cookie)
{
	struct iommufd_device *idev;
	struct device *dev = NULL;
	unsigned long index;

	mutex_lock(&ictx->lock);
	xa_for_each(&ictx->device_xa, index, idev) {
		if (idev->dev_cookie == dev_cookie) {
			dev = idev->dev;
			break;
		}
	}
	mutex_unlock(&ictx->lock);

	return dev;
}

static int iommu_device_add_cap_chain(struct device *dev, unsigned long arg,
				      struct iommu_device_info *info)
{
	struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
	int ret;

	ret = vfio_device_add_iova_cap(dev, &caps);
	if (ret)
		return ret;

	if (caps.size) {
		info->flags |= IOMMU_DEVICE_INFO_CAPS;

		if (info->argsz < sizeof(*info) + caps.size) {
			info->argsz = sizeof(*info) + caps.size;
		} else {
			vfio_info_cap_shift(&caps, sizeof(*info));
			if (copy_to_user((void __user *)arg +
					sizeof(*info), caps.buf,
					caps.size)) {
				kfree(caps.buf);
				info->flags &= ~IOMMU_DEVICE_INFO_CAPS;
				return -EFAULT;
			}
			info->cap_offset = sizeof(*info);
		}

		kfree(caps.buf);
	}
	return 0;
}

static void iommu_device_build_info(struct device *dev,
				    struct iommu_device_info *info)
{
	bool snoop;
	u64 awidth, pgsizes;

	if (!iommu_device_get_info(dev, IOMMU_DEV_INFO_FORCE_SNOOP, &snoop))
		info->flags |= snoop ? IOMMU_DEVICE_INFO_ENFORCE_SNOOP : 0;

	if (!iommu_device_get_info(dev, IOMMU_DEV_INFO_PAGE_SIZE, &pgsizes)) {
		info->pgsize_bitmap = pgsizes;
		info->flags |= IOMMU_DEVICE_INFO_PGSIZES;
	}

	if (!iommu_device_get_info(dev, IOMMU_DEV_INFO_ADDR_WIDTH, &awidth)) {
		info->addr_width = awidth;
		info->flags |= IOMMU_DEVICE_INFO_ADDR_WIDTH;
	}
}

static int iommufd_get_device_info(struct iommufd_ctx *ictx,
				   unsigned long arg)
{
	struct iommu_device_info info;
	unsigned long minsz;
	struct device *dev;
	int ret;

	minsz = offsetofend(struct iommu_device_info, cap_offset);

	if (copy_from_user(&info, (void __user *)arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	info.flags = 0;

	dev = iommu_find_device_from_cookie(ictx, info.dev_cookie);
	if (!dev)
		return -EINVAL;

	iommu_device_build_info(dev, &info);

	info.cap_offset = 0;
	ret = iommu_device_add_cap_chain(dev, arg, &info);
	if (ret)
		pr_info_ratelimited("No cap chain added, error %d\n", ret);

	return copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
}

static int iommufd_process_dma_op(struct iommufd_ctx *ictx,
				  unsigned long arg, bool map)
{
	struct iommu_ioasid_dma_op dma;
	unsigned long minsz;
	struct iommufd_ioas *ioas = NULL;
	int ret;

	minsz = offsetofend(struct iommu_ioasid_dma_op, padding);

	if (copy_from_user(&dma, (void __user *)arg, minsz))
		return -EFAULT;

	if (dma.argsz < minsz || dma.flags || dma.ioasid < 0)
		return -EINVAL;

	ioas = ioasid_get_ioas(ictx, dma.ioasid);
	if (!ioas) {
		pr_err_ratelimited("unkonwn IOASID %u\n", dma.ioasid);
		return -EINVAL;
	}

	mutex_lock(&ioas->lock);

	/*
	 * Needs to block map/unmap request from userspace before IOASID
	 * is attached to any device.
	 */
	if (list_empty(&ioas->device_list)) {
		ret = -EINVAL;
		goto out;
	}

	if (map)
		ret = vfio_iommu_type1_map_dma(ioas->vfio_iommu, arg + minsz);
	else
		ret = vfio_iommu_type1_unmap_dma(ioas->vfio_iommu, arg + minsz);
out:
	mutex_unlock(&ioas->lock);
	ioas_put(ioas);
	return ret;
};

static long iommufd_fops_unl_ioctl(struct file *filep,
				   unsigned int cmd, unsigned long arg)
{
	struct iommufd_ctx *ictx = filep->private_data;
	long ret = -EINVAL;

	if (!ictx)
		return ret;

	switch (cmd) {
	case IOMMU_CHECK_EXTENSION:
		switch (arg) {
		case EXT_MAP_TYPE1V2:
			return 1;
		default:
			return 0;
		}
	case IOMMU_DEVICE_GET_INFO:
		ret = iommufd_get_device_info(ictx, arg);
		break;
	case IOMMU_IOASID_ALLOC:
		ret = iommufd_ioasid_alloc(ictx, arg);
		break;
	case IOMMU_IOASID_FREE:
		ret = iommufd_ioasid_free(ictx, arg);
		break;
	case IOMMU_MAP_DMA:
		ret = iommufd_process_dma_op(ictx, arg, true);
		break;
	case IOMMU_UNMAP_DMA:
		ret = iommufd_process_dma_op(ictx, arg, false);
		break;
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

/* Caller should hold ioas->lock */
static struct ioas_device_info *ioas_find_device(struct iommufd_ioas *ioas,
						 struct iommufd_device *idev)
{
	struct ioas_device_info *ioas_dev;

	list_for_each_entry(ioas_dev, &ioas->device_list, next) {
		if (ioas_dev->idev == idev)
			return ioas_dev;
	}

	return NULL;
}

static void ioas_free_domain_if_empty(struct iommufd_ioas *ioas)
{
	if (list_empty(&ioas->device_list)) {
		iommu_domain_free(ioas->domain);
		ioas->domain = NULL;
	}
}

static int ioas_check_device_compatibility(struct iommufd_ioas *ioas,
					   struct device *dev)
{
	bool snoop = false;
	u32 addr_width;
	int ret;

	/*
	 * currently we only support I/O page table with iommu enforce-snoop
	 * format. Attaching a device which doesn't support this format in its
	 * upstreaming iommu is rejected.
	 */
	ret = iommu_device_get_info(dev, IOMMU_DEV_INFO_FORCE_SNOOP, &snoop);
	if (ret || !snoop)
		return -EINVAL;

	ret = iommu_device_get_info(dev, IOMMU_DEV_INFO_ADDR_WIDTH, &addr_width);
	if (ret || addr_width < ioas->addr_width)
		return -EINVAL;

	/* TODO: also need to check permitted iova ranges and pgsize bitmap */

	return 0;
}

/* HACK:
 * vfio_iommu_add/remove_device() is hacky implementation for
 * this version to add the device/group to vfio iommu type1.
 */
static int vfio_iommu_add_device(struct vfio_iommu *vfio_iommu,
				 struct device *dev,
				 struct iommu_domain *domain)
{
	struct iommu_group *group;
	int ret;

	group = iommu_group_get(dev);
	if (!group)
		return -EINVAL;

	ret = vfio_iommu_add_group(vfio_iommu, group, domain);
	iommu_group_put(group);
	return ret;
}

static void vfio_iommu_remove_device(struct vfio_iommu *vfio_iommu,
				     struct device *dev)
{
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		return;

	vfio_iommu_remove_group(vfio_iommu, group);
	iommu_group_put(group);
}

/**
 * iommufd_device_attach_ioasid - attach device to an ioasid
 * @idev: [in] Pointer to struct iommufd_device.
 * @ioasid: [in] ioasid points to an I/O address space.
 *
 * Returns 0 for successful attach, otherwise returns error.
 *
 */
int iommufd_device_attach_ioasid(struct iommufd_device *idev, int ioasid)
{
	struct iommufd_ioas *ioas;
	struct ioas_device_info *ioas_dev;
	struct iommu_domain *domain;
	int ret;

	ioas = ioasid_get_ioas(idev->ictx, ioasid);
	if (!ioas) {
		pr_err_ratelimited("Trying to attach illegal or unkonwn IOASID %u\n", ioasid);
		return -EINVAL;
	}

	mutex_lock(&ioas->lock);

	/* Check for duplicates */
	if (ioas_find_device(ioas, idev)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = ioas_check_device_compatibility(ioas, idev->dev);
	if (ret)
		goto out_unlock;

	ioas_dev = kzalloc(sizeof(*ioas_dev), GFP_KERNEL);
	if (!ioas_dev) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	/*
	 * Each ioas is backed by an iommu domain, which is allocated
	 * when the ioas is attached for the first time and then shared
	 * by following devices.
	 */
	if (list_empty(&ioas->device_list)) {
		struct iommu_domain *d;

		d = iommu_domain_alloc(idev->dev->bus);
		if (!d) {
			ret = -ENOMEM;
			goto out_free;
		}
		ioas->domain = d;
	}
	domain = ioas->domain;

	/* Install the I/O page table to the iommu for this device */
	ret = iommu_attach_device(domain, idev->dev);
	if (ret)
		goto out_domain;

	ret = vfio_iommu_add_device(ioas->vfio_iommu, idev->dev, domain);
	if (ret)
		goto out_detach;

	ioas_dev->idev = idev;
	list_add(&ioas_dev->next, &ioas->device_list);
	mutex_unlock(&ioas->lock);

	return 0;
out_detach:
	iommu_detach_device(domain, idev->dev);
out_domain:
	ioas_free_domain_if_empty(ioas);
out_free:
	kfree(ioas_dev);
out_unlock:
	mutex_unlock(&ioas->lock);
	ioas_put(ioas);

	return ret;
}
EXPORT_SYMBOL_GPL(iommufd_device_attach_ioasid);

/**
 * iommufd_device_detach_ioasid - Detach an ioasid from a device.
 * @idev: [in] Pointer to struct iommufd_device.
 * @ioasid: [in] ioasid points to an I/O address space.
 *
 */
void iommufd_device_detach_ioasid(struct iommufd_device *idev, int ioasid)
{
	struct iommufd_ioas *ioas;
	struct ioas_device_info *ioas_dev;

	ioas = ioasid_get_ioas(idev->ictx, ioasid);
	if (!ioas)
		return;

	mutex_lock(&ioas->lock);
	ioas_dev = ioas_find_device(ioas, idev);
	if (!ioas_dev) {
		mutex_unlock(&ioas->lock);
		goto out;
	}

	list_del(&ioas_dev->next);
	vfio_iommu_remove_device(ioas->vfio_iommu, idev->dev);
	iommu_detach_device(ioas->domain, idev->dev);
	ioas_free_domain_if_empty(ioas);
	kfree(ioas_dev);
	mutex_unlock(&ioas->lock);

	/* release the reference acquired at the start of this function */
	ioas_put(ioas);
out:
	ioas_put(ioas);
}
EXPORT_SYMBOL_GPL(iommufd_device_detach_ioasid);

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
	struct iommufd_ioas *ioas;
	unsigned long index;

	mutex_lock(&ictx->lock);
	xa_for_each(&ictx->ioasid_xa, index, ioas) {
		struct ioas_device_info *ioas_dev;

		mutex_lock(&ioas->lock);
		ioas_dev = ioas_find_device(ioas, idev);
		if (!ioas_dev) {
			mutex_unlock(&ioas->lock);
			continue;
		}
		list_del(&ioas_dev->next);
		iommu_detach_device(ioas->domain, idev->dev);
		ioas_free_domain_if_empty(ioas);
		kfree(ioas_dev);
		mutex_unlock(&ioas->lock);
		ioas_put_locked(ioas);
	}
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
