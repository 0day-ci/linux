// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/idr.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <uapi/misc/intel_nnpi.h>

#include "device.h"
#include "host_chardev.h"
#include "ipc_protocol.h"
#include "nnp_user.h"

static struct cdev cdev;
static dev_t       devnum;
static struct class *class;
static struct device *dev;

static inline int is_host_file(struct file *f);

static enum dma_data_direction to_dma_dir(unsigned int nnp_dir)
{
	/* Ignore IOCTL_INF_RES_NETWORK */
	switch (nnp_dir & (IOCTL_INF_RES_INPUT | IOCTL_INF_RES_OUTPUT)) {
	case (IOCTL_INF_RES_INPUT | IOCTL_INF_RES_OUTPUT):
		return DMA_BIDIRECTIONAL;
	case IOCTL_INF_RES_INPUT:
		return DMA_TO_DEVICE;
	case IOCTL_INF_RES_OUTPUT:
		return DMA_FROM_DEVICE;
	default:
		break;
	}

	return DMA_NONE;
}

static long create_hostres(struct nnp_user_info *user_info, void __user *arg,
			   unsigned int size)
{
	int ret;
	struct nnpdrv_ioctl_create_hostres req;
	struct host_resource *hostres;
	struct user_hostres *user_hostres_entry;
	void __user *uptr;
	unsigned int io_size = sizeof(req);

	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&req, arg, io_size))
		return -EFAULT;

	if (req.usage_flags & ~IOCTL_RES_USAGE_VALID_MASK)
		return -EINVAL;

	uptr = u64_to_user_ptr(req.user_ptr);
	hostres = nnp_hostres_from_usermem(uptr, req.size,
					   to_dma_dir(req.usage_flags));

	if (IS_ERR(hostres))
		return PTR_ERR(hostres);

	ret = nnp_user_add_hostres(user_info, hostres, &user_hostres_entry);
	if (ret < 0) {
		nnp_hostres_put(hostres);
		return ret;
	}

	req.size = nnp_hostres_size(hostres);

	/*
	 * The created user_hostres_entry holds refcount to the resource,
	 * no need to keep another one here.
	 */
	nnp_hostres_put(hostres);

	req.user_handle = user_hostres_entry->user_handle;
	if (copy_to_user(arg, &req, io_size)) {
		ret = -EFAULT;
		goto destroy_hostres_entry;
	}

	return 0;

destroy_hostres_entry:
	nnp_user_remove_hostres(user_hostres_entry);

	return ret;
}

static long destroy_hostres(struct nnp_user_info *user_info, void __user *arg,
			    unsigned int size)
{
	struct nnpdrv_ioctl_destroy_hostres destroy_args;
	struct user_hostres *user_hostres_entry;
	unsigned int io_size = sizeof(destroy_args);
	int ret = 0;

	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&destroy_args, arg, io_size))
		return -EFAULT;

	/* errno must be cleared on entry */
	if (destroy_args.o_errno)
		return -EINVAL;

	mutex_lock(&user_info->mutex);
	user_hostres_entry = idr_find(&user_info->idr, destroy_args.user_handle);
	if (user_hostres_entry) {
		nnp_user_remove_hostres_locked(user_hostres_entry);
	} else {
		destroy_args.o_errno = NNPER_NO_SUCH_RESOURCE;
		if (copy_to_user(arg, &destroy_args, io_size))
			ret = -EFAULT;
	}

	mutex_unlock(&user_info->mutex);
	return ret;
}

static long lock_hostres(struct nnp_user_info *user_info, void __user *arg,
			 unsigned int size)
{
	int ret = 0;
	struct nnpdrv_ioctl_lock_hostres lock_args;
	struct user_hostres *user_hostres_entry;
	unsigned int io_size = sizeof(lock_args);

	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&lock_args, arg, io_size))
		return -EFAULT;

	/* errno must be cleared on entry */
	if (lock_args.o_errno)
		return -EINVAL;

	mutex_lock(&user_info->mutex);
	user_hostres_entry = idr_find(&user_info->idr, lock_args.user_handle);
	if (user_hostres_entry) {
		ret = nnp_hostres_user_lock(user_hostres_entry->hostres);
	} else {
		lock_args.o_errno = NNPER_NO_SUCH_RESOURCE;
		if (copy_to_user(arg, &lock_args, io_size))
			ret = -EFAULT;
	}

	mutex_unlock(&user_info->mutex);
	return ret;
}

static long unlock_hostres(struct nnp_user_info *user_info, void __user *arg,
			   unsigned int size)
{
	int ret = 0;
	struct user_hostres *user_hostres_entry;
	struct nnpdrv_ioctl_lock_hostres lock_args;
	unsigned int io_size = sizeof(lock_args);

	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&lock_args, arg, io_size))
		return -EFAULT;

	/* errno must be cleared on entry */
	if (lock_args.o_errno)
		return -EINVAL;

	mutex_lock(&user_info->mutex);
	user_hostres_entry = idr_find(&user_info->idr, lock_args.user_handle);
	if (user_hostres_entry) {
		ret = nnp_hostres_user_unlock(user_hostres_entry->hostres);
	} else {
		lock_args.o_errno = NNPER_NO_SUCH_RESOURCE;
		if (copy_to_user(arg, &lock_args, sizeof(lock_args)))
			ret = -EFAULT;
	}

	mutex_unlock(&user_info->mutex);
	return ret;
}

struct file *nnp_host_file_get(int host_fd)
{
	struct file *host_file;

	host_file = fget(host_fd);
	if (is_host_file(host_file))
		return host_file;

	if (host_file)
		fput(host_file);

	return NULL;
}

/*
 * Inference host cdev (/dev/nnpi_host) file operation functions
 */

static int host_open(struct inode *inode, struct file *f)
{
	struct nnp_user_info *user_info;

	if (!is_host_file(f))
		return -EINVAL;

	user_info = kzalloc(sizeof(*user_info), GFP_KERNEL);
	if (!user_info)
		return -ENOMEM;

	nnp_user_init(user_info);

	f->private_data = user_info;

	return 0;
}

static int host_release(struct inode *inode, struct file *f)
{
	struct nnp_user_info *user_info;

	if (!is_host_file(f))
		return -EINVAL;

	user_info = f->private_data;

	nnp_user_destroy_all(user_info);
	f->private_data = NULL;

	return 0;
}

static long host_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct nnp_user_info *user_info = f->private_data;
	unsigned int ioc_nr, size;

	if (!is_host_file(f))
		return -ENOTTY;

	if (_IOC_TYPE(cmd) != 'h')
		return -EINVAL;

	ioc_nr = _IOC_NR(cmd);
	size = _IOC_SIZE(cmd);

	switch (ioc_nr) {
	case _IOC_NR(IOCTL_INF_CREATE_HOST_RESOURCE):
		ret = create_hostres(user_info, (void __user *)arg, size);
		break;
	case _IOC_NR(IOCTL_INF_DESTROY_HOST_RESOURCE):
		ret = destroy_hostres(user_info, (void __user *)arg, size);
		break;
	case _IOC_NR(IOCTL_INF_UNLOCK_HOST_RESOURCE):
		ret = unlock_hostres(user_info, (void __user *)arg, size);
		break;
	case _IOC_NR(IOCTL_INF_LOCK_HOST_RESOURCE):
		ret = lock_hostres(user_info, (void __user *)arg, size);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct file_operations host_fops = {
	.owner = THIS_MODULE,
	.open = host_open,
	.release = host_release,
	.unlocked_ioctl = host_ioctl,
	.compat_ioctl = host_ioctl,
};

static inline int is_host_file(struct file *f)
{
	return f && f->f_op == &host_fops;
}

int nnp_init_host_interface(void)
{
	int ret;

	ret = alloc_chrdev_region(&devnum, 0, 1, NNPDRV_INF_HOST_DEV_NAME);
	if (ret < 0)
		return ret;

	cdev_init(&cdev, &host_fops);
	cdev.owner = THIS_MODULE;

	ret = cdev_add(&cdev, devnum, 1);
	if (ret < 0)
		goto err_region;

	class = class_create(THIS_MODULE, NNPDRV_INF_HOST_DEV_NAME);
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		goto err_cdev;
	}

	dev = device_create(class, NULL, devnum, NULL, NNPDRV_INF_HOST_DEV_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_class;
	}

	ret = nnp_hostres_init_sysfs(dev);
	if (ret < 0)
		goto err_device;

	return 0;

err_device:
	device_destroy(class, devnum);
err_class:
	class_destroy(class);
err_cdev:
	cdev_del(&cdev);
err_region:
	unregister_chrdev_region(devnum, 1);

	return ret;
}

void nnp_release_host_interface(void)
{
	nnp_hostres_fini_sysfs(dev);
	device_destroy(class, devnum);
	class_destroy(class);
	cdev_del(&cdev);
	unregister_chrdev_region(devnum, 1);
}
