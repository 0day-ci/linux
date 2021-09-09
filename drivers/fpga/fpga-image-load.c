// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Image Load Class Driver
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */

#include <linux/delay.h>
#include <linux/fpga/fpga-image-load.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#define IMAGE_LOAD_XA_LIMIT	XA_LIMIT(0, INT_MAX)
static DEFINE_XARRAY_ALLOC(fpga_image_load_xa);

static struct class *fpga_image_load_class;
static dev_t fpga_image_devt;

#define to_image_load(d) container_of(d, struct fpga_image_load, dev)

static void fpga_image_update_progress(struct fpga_image_load *imgld,
				       enum fpga_image_prog new_progress)
{
	mutex_lock(&imgld->lock);
	imgld->progress = new_progress;
	mutex_unlock(&imgld->lock);
}

static void fpga_image_set_error(struct fpga_image_load *imgld,
				 enum fpga_image_err err_code)
{
	mutex_lock(&imgld->lock);
	imgld->err_progress = imgld->progress;
	imgld->err_code = err_code;
	mutex_unlock(&imgld->lock);
}

static void fpga_image_dev_error(struct fpga_image_load *imgld,
				 enum fpga_image_err err_code)
{
	fpga_image_set_error(imgld, err_code);
	imgld->lops->cancel(imgld);
}

static void fpga_image_prog_complete(struct fpga_image_load *imgld)
{
	mutex_lock(&imgld->lock);
	imgld->progress = FPGA_IMAGE_PROG_IDLE;
	eventfd_signal(imgld->finished, 1);
	complete_all(&imgld->update_done);
	mutex_unlock(&imgld->lock);
}

static void fpga_image_do_load(struct work_struct *work)
{
	struct fpga_image_load *imgld;
	enum fpga_image_err ret;
	u32 size, offset = 0;

	imgld = container_of(work, struct fpga_image_load, work);
	size = imgld->remaining_size;

	get_device(&imgld->dev);
	if (!try_module_get(imgld->dev.parent->driver->owner)) {
		fpga_image_set_error(imgld, FPGA_IMAGE_ERR_BUSY);
		goto idle_exit;
	}

	fpga_image_update_progress(imgld, FPGA_IMAGE_PROG_PREPARING);
	ret = imgld->lops->prepare(imgld);
	if (ret != FPGA_IMAGE_ERR_NONE) {
		fpga_image_dev_error(imgld, ret);
		goto modput_exit;
	}

	fpga_image_update_progress(imgld, FPGA_IMAGE_PROG_WRITING);
	while (imgld->remaining_size) {
		ret = imgld->lops->write_blk(imgld, offset);
		if (ret != FPGA_IMAGE_ERR_NONE) {
			fpga_image_dev_error(imgld, ret);
			goto done;
		}

		offset = size - imgld->remaining_size;
	}

	fpga_image_update_progress(imgld, FPGA_IMAGE_PROG_PROGRAMMING);
	ret = imgld->lops->poll_complete(imgld);
	if (ret != FPGA_IMAGE_ERR_NONE)
		fpga_image_dev_error(imgld, ret);

done:
	if (imgld->lops->cleanup)
		imgld->lops->cleanup(imgld);

modput_exit:
	module_put(imgld->dev.parent->driver->owner);

idle_exit:
	/*
	 * Note: imgld->remaining_size is left unmodified here to provide
	 * additional information on errors. It will be reinitialized when
	 * the next image load begins.
	 */
	vfree(imgld->data);
	imgld->data = NULL;
	put_device(&imgld->dev);
	fpga_image_prog_complete(imgld);
	eventfd_ctx_put(imgld->finished);
	imgld->finished = NULL;
}

static int fpga_image_load_ioctl_write(struct fpga_image_load *imgld,
				       unsigned long arg)
{
	struct fpga_image_write wb;
	unsigned long minsz;
	int ret;
	u8 *buf;

	if (imgld->driver_unload || imgld->progress != FPGA_IMAGE_PROG_IDLE)
		return -EBUSY;

	minsz = offsetofend(struct fpga_image_write, buf);
	if (copy_from_user(&wb, (void __user *)arg, minsz))
		return -EFAULT;

	if (wb.flags)
		return -EINVAL;

	/* Enforce 32-bit alignment on the write data */
	if (wb.size & 0x3)
		return -EINVAL;

	if (wb.evtfd < 0)
		return -EINVAL;

	buf = vzalloc(wb.size);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, u64_to_user_ptr(wb.buf), wb.size)) {
		ret = -EFAULT;
		goto exit_free;
	}

	imgld->finished = eventfd_ctx_fdget(wb.evtfd);
	if (IS_ERR(imgld->finished)) {
		ret = PTR_ERR(imgld->finished);
		imgld->finished = NULL;
		goto exit_free;
	}

	imgld->data = buf;
	imgld->remaining_size = wb.size;
	imgld->err_code = FPGA_IMAGE_ERR_NONE;
	imgld->progress = FPGA_IMAGE_PROG_STARTING;
	reinit_completion(&imgld->update_done);
	schedule_work(&imgld->work);
	return 0;

exit_free:
	vfree(buf);
	return ret;
}

static int fpga_image_load_ioctl_status(struct fpga_image_load *imgld,
					unsigned long arg)
{
	struct fpga_image_status status;

	memset(&status, 0, sizeof(status));
	status.progress = imgld->progress;
	status.remaining_size = imgld->remaining_size;
	status.err_progress = imgld->err_progress;
	status.err_code = imgld->err_code;

	if (copy_to_user((void __user *)arg, &status, sizeof(status)))
		return -EFAULT;

	return 0;
}

static long fpga_image_load_ioctl(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	struct fpga_image_load *imgld = filp->private_data;
	int ret = -ENOTTY;

	mutex_lock(&imgld->lock);

	switch (cmd) {
	case FPGA_IMAGE_LOAD_WRITE:
		ret = fpga_image_load_ioctl_write(imgld, arg);
		break;
	case FPGA_IMAGE_LOAD_STATUS:
		ret = fpga_image_load_ioctl_status(imgld, arg);
		break;
	}

	mutex_unlock(&imgld->lock);

	return ret;
}

static int fpga_image_load_open(struct inode *inode, struct file *filp)
{
	struct fpga_image_load *imgld = container_of(inode->i_cdev,
						     struct fpga_image_load, cdev);

	if (test_and_set_bit(0, &imgld->opened))
		return -EBUSY;

	filp->private_data = imgld;

	return 0;
}

static int fpga_image_load_release(struct inode *inode, struct file *filp)
{
	struct fpga_image_load *imgld = filp->private_data;

	clear_bit(0, &imgld->opened);

	return 0;
}

static const struct file_operations fpga_image_load_fops = {
	.owner = THIS_MODULE,
	.open = fpga_image_load_open,
	.release = fpga_image_load_release,
	.unlocked_ioctl = fpga_image_load_ioctl,
};

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
	int ret;

	if (!lops || !lops->cancel || !lops->prepare ||
	    !lops->write_blk || !lops->poll_complete) {
		dev_err(parent, "Attempt to register without all required ops\n");
		return ERR_PTR(-ENOMEM);
	}

	imgld = kzalloc(sizeof(*imgld), GFP_KERNEL);
	if (!imgld)
		return ERR_PTR(-ENOMEM);

	ret = xa_alloc(&fpga_image_load_xa, &imgld->dev.id, imgld, IMAGE_LOAD_XA_LIMIT,
		       GFP_KERNEL);
	if (ret)
		goto error_kfree;

	mutex_init(&imgld->lock);

	imgld->priv = priv;
	imgld->lops = lops;
	imgld->err_code = FPGA_IMAGE_ERR_NONE;
	imgld->progress = FPGA_IMAGE_PROG_IDLE;
	init_completion(&imgld->update_done);
	INIT_WORK(&imgld->work, fpga_image_do_load);

	imgld->dev.class = fpga_image_load_class;
	imgld->dev.parent = parent;
	imgld->dev.devt = MKDEV(MAJOR(fpga_image_devt), imgld->dev.id);

	ret = dev_set_name(&imgld->dev, "fpga_image%d", imgld->dev.id);
	if (ret) {
		dev_err(parent, "Failed to set device name: fpga_image%d\n",
			imgld->dev.id);
		goto error_device;
	}

	ret = device_register(&imgld->dev);
	if (ret) {
		put_device(&imgld->dev);
		return ERR_PTR(ret);
	}

	cdev_init(&imgld->cdev, &fpga_image_load_fops);
	imgld->cdev.owner = parent->driver->owner;
	imgld->cdev.kobj.parent = &imgld->dev.kobj;

	ret = cdev_add(&imgld->cdev, imgld->dev.devt, 1);
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
 *
 * For some devices, once authentication of the uploaded image has begun,
 * the hardware cannot be signaled to stop, and the driver will not exit
 * until the hardware signals completion.  This could be 30+ minutes of
 * waiting. The driver_unload flag enables a force-unload of the driver
 * (e.g. modprobe -r) by signaling the parent driver to exit even if the
 * hardware update is incomplete. The driver_unload flag also prevents
 * new updates from starting once the unregister process has begun.
 */
void fpga_image_load_unregister(struct fpga_image_load *imgld)
{
	mutex_lock(&imgld->lock);
	imgld->driver_unload = true;
	if (imgld->progress == FPGA_IMAGE_PROG_IDLE) {
		mutex_unlock(&imgld->lock);
		goto unregister;
	}

	mutex_unlock(&imgld->lock);
	wait_for_completion(&imgld->update_done);

unregister:
	cdev_del(&imgld->cdev);
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
	int ret;
	pr_info("FPGA Image Load Driver\n");

	fpga_image_load_class = class_create(THIS_MODULE, "fpga_image_load");
	if (IS_ERR(fpga_image_load_class))
		return PTR_ERR(fpga_image_load_class);

	ret = alloc_chrdev_region(&fpga_image_devt, 0, MINORMASK,
				  "fpga_image_load");
	if (ret)
		goto exit_destroy_class;

	fpga_image_load_class->dev_release = fpga_image_load_dev_release;

	return 0;

exit_destroy_class:
	class_destroy(fpga_image_load_class);
	return ret;
}

static void __exit fpga_image_load_class_exit(void)
{
	unregister_chrdev_region(fpga_image_devt, MINORMASK);
	class_destroy(fpga_image_load_class);
	WARN_ON(!xa_empty(&fpga_image_load_xa));
}

MODULE_DESCRIPTION("FPGA Image Load Driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_image_load_class_init);
module_exit(fpga_image_load_class_exit)
