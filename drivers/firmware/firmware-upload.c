// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware Upload Framework
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */

#include <linux/delay.h>
#include <linux/firmware/firmware-upload.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#define FW_UPLOAD_XA_LIMIT	XA_LIMIT(0, INT_MAX)
static DEFINE_XARRAY_ALLOC(fw_upload_xa);

static struct class *fw_upload_class;
static dev_t fw_upload_devt;

#define to_fw_upload(d) container_of(d, struct fw_upload, dev)

static void fw_upload_prog_complete(struct fw_upload *fwl)
{
	mutex_lock(&fwl->lock);
	fwl->progress = FW_UPLOAD_PROG_IDLE;
	mutex_unlock(&fwl->lock);
}

static void fw_upload_do_load(struct work_struct *work)
{
	struct fw_upload *fwl;
	s32 ret, offset = 0;

	fwl = container_of(work, struct fw_upload, work);

	if (fwl->driver_unload) {
		fwl->err_code = FW_UPLOAD_ERR_CANCELED;
		goto idle_exit;
	}

	get_device(&fwl->dev);
	if (!try_module_get(fwl->dev.parent->driver->owner)) {
		fwl->err_code = FW_UPLOAD_ERR_BUSY;
		goto putdev_exit;
	}

	fwl->progress = FW_UPLOAD_PROG_PREPARING;
	ret = fwl->ops->prepare(fwl, fwl->data, fwl->remaining_size);
	if (ret) {
		fwl->err_code = ret;
		goto modput_exit;
	}

	fwl->progress = FW_UPLOAD_PROG_WRITING;
	while (fwl->remaining_size) {
		ret = fwl->ops->write(fwl, fwl->data, offset,
					fwl->remaining_size);
		if (ret <= 0) {
			if (!ret) {
				dev_warn(&fwl->dev,
					 "write-op wrote zero data\n");
				ret = -FW_UPLOAD_ERR_RW_ERROR;
			}
			fwl->err_code = -ret;
			goto done;
		}

		fwl->remaining_size -= ret;
		offset += ret;
	}

	fwl->progress = FW_UPLOAD_PROG_PROGRAMMING;
	ret = fwl->ops->poll_complete(fwl);
	if (ret)
		fwl->err_code = ret;

done:
	if (fwl->ops->cleanup)
		fwl->ops->cleanup(fwl);

modput_exit:
	module_put(fwl->dev.parent->driver->owner);

putdev_exit:
	put_device(&fwl->dev);

idle_exit:
	/*
	 * Note: fwl->remaining_size is left unmodified here to provide
	 * additional information on errors. It will be reinitialized when
	 * the next firmeware upload begins.
	 */
	vfree(fwl->data);
	fwl->data = NULL;
	fw_upload_prog_complete(fwl);
}

static int fw_upload_ioctl_write(struct fw_upload *fwl, unsigned long arg)
{
	struct fw_upload_write wb;
	unsigned long minsz;
	u8 *buf;

	if (fwl->driver_unload || fwl->progress != FW_UPLOAD_PROG_IDLE)
		return -EBUSY;

	minsz = offsetofend(struct fw_upload_write, buf);
	if (copy_from_user(&wb, (void __user *)arg, minsz))
		return -EFAULT;

	if (wb.flags)
		return -EINVAL;

	buf = vzalloc(wb.size);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, u64_to_user_ptr(wb.buf), wb.size)) {
		vfree(buf);
		return -EFAULT;
	}

	fwl->data = buf;
	fwl->remaining_size = wb.size;
	fwl->err_code = 0;
	fwl->progress = FW_UPLOAD_PROG_STARTING;
	queue_work(system_long_wq, &fwl->work);

	return 0;
}

static long fw_upload_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	struct fw_upload *fwl = filp->private_data;
	int ret = -ENOTTY;

	switch (cmd) {
	case FW_UPLOAD_WRITE:
		mutex_lock(&fwl->lock);
		ret = fw_upload_ioctl_write(fwl, arg);
		mutex_unlock(&fwl->lock);
		break;
	}

	return ret;
}

static int fw_upload_open(struct inode *inode, struct file *filp)
{
	struct fw_upload *fwl = container_of(inode->i_cdev,
						     struct fw_upload, cdev);

	if (atomic_cmpxchg(&fwl->opened, 0, 1))
		return -EBUSY;

	filp->private_data = fwl;

	return 0;
}

static int fw_upload_release(struct inode *inode, struct file *filp)
{
	struct fw_upload *fwl = filp->private_data;

	mutex_lock(&fwl->lock);
	if (fwl->progress == FW_UPLOAD_PROG_IDLE) {
		mutex_unlock(&fwl->lock);
		goto close_exit;
	}

	mutex_unlock(&fwl->lock);
	flush_work(&fwl->work);

close_exit:
	atomic_set(&fwl->opened, 0);

	return 0;
}

static const struct file_operations fw_upload_fops = {
	.owner = THIS_MODULE,
	.open = fw_upload_open,
	.release = fw_upload_release,
	.unlocked_ioctl = fw_upload_ioctl,
};

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

	if (!ops || !ops->prepare || !ops->write || !ops->poll_complete) {
		dev_err(parent, "Attempt to register without all required ops\n");
		return ERR_PTR(-ENOMEM);
	}

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
	fwl->err_code = 0;
	fwl->progress = FW_UPLOAD_PROG_IDLE;
	INIT_WORK(&fwl->work, fw_upload_do_load);

	fwl->dev.class = fw_upload_class;
	fwl->dev.parent = parent;
	fwl->dev.devt = MKDEV(MAJOR(fw_upload_devt), fwl->dev.id);

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

	cdev_init(&fwl->cdev, &fw_upload_fops);
	fwl->cdev.owner = parent->driver->owner;
	cdev_set_parent(&fwl->cdev, &fwl->dev.kobj);

	ret = cdev_add(&fwl->cdev, fwl->dev.devt, 1);
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
 * function. The driver_unload flag prevents new updates from starting
 * once the unregister process has begun.
 */
void fw_upload_unregister(struct fw_upload *fwl)
{
	mutex_lock(&fwl->lock);
	fwl->driver_unload = true;
	if (fwl->progress == FW_UPLOAD_PROG_IDLE) {
		mutex_unlock(&fwl->lock);
		goto unregister;
	}

	mutex_unlock(&fwl->lock);
	flush_work(&fwl->work);

unregister:
	cdev_del(&fwl->cdev);
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
	int ret;
	pr_info("Firmware Upload Framework\n");

	fw_upload_class = class_create(THIS_MODULE, "fw_upload");
	if (IS_ERR(fw_upload_class))
		return PTR_ERR(fw_upload_class);

	ret = alloc_chrdev_region(&fw_upload_devt, 0, MINORMASK,
				  "fw_upload");
	if (ret)
		goto exit_destroy_class;

	fw_upload_class->dev_release = fw_upload_dev_release;

	return 0;

exit_destroy_class:
	class_destroy(fw_upload_class);
	return ret;
}

static void __exit fw_upload_class_exit(void)
{
	unregister_chrdev_region(fw_upload_devt, MINORMASK);
	class_destroy(fw_upload_class);
	WARN_ON(!xa_empty(&fw_upload_xa));
}

MODULE_DESCRIPTION("Firmware Upload Framework");
MODULE_LICENSE("GPL v2");

subsys_initcall(fw_upload_class_init);
module_exit(fw_upload_class_exit)
