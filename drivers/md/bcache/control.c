// SPDX-License-Identifier: GPL-2.0
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>

#include "control.h"

struct bch_ctrl_device {
	struct cdev cdev;
	struct class *class;
	dev_t dev;
};

static struct bch_ctrl_device _control_device;

/* this handles IOCTL for /dev/bcache_ctrl */
/*********************************************/
long bch_service_ioctl_ctrl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int retval = 0;

	if (_IOC_TYPE(cmd) != BCH_IOCTL_MAGIC)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN)) {
		/* Must be root to issue ioctls */
		return -EPERM;
	}

	switch (cmd) {
	case BCH_IOCTL_REGISTER_DEVICE: {
		struct bch_register_device *cmd_info;

		cmd_info = vmalloc(sizeof(struct bch_register_device));
		if (!cmd_info)
			return -ENOMEM;

		if (copy_from_user(cmd_info, (void __user *)arg,
				sizeof(struct bch_register_device))) {
			pr_err("Cannot copy cmd info from user space\n");
			vfree(cmd_info);
			return -EINVAL;
		}

		retval = register_bcache_ioctl(cmd_info);

		vfree(cmd_info);
		return retval;
	}

	default:
		return -EINVAL;
	}
}

static const struct file_operations _ctrl_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = bch_service_ioctl_ctrl
};

int __init bch_ctrl_device_init(void)
{
	struct bch_ctrl_device *ctrl = &_control_device;
	struct device *device;
	int result = 0;

	result = alloc_chrdev_region(&ctrl->dev, 0, 1, "bcache");
	if (result) {
		pr_err("Cannot allocate control chrdev number.\n");
		goto error_alloc_chrdev_region;
	}

	cdev_init(&ctrl->cdev, &_ctrl_dev_fops);

	result = cdev_add(&ctrl->cdev, ctrl->dev, 1);
	if (result) {
		pr_err("Cannot add control chrdev.\n");
		goto error_cdev_add;
	}

	ctrl->class = class_create(THIS_MODULE, "bcache");
	if (IS_ERR(ctrl->class)) {
		pr_err("Cannot create control chrdev class.\n");
		result = PTR_ERR(ctrl->class);
		goto error_class_create;
	}

	device = device_create(ctrl->class, NULL, ctrl->dev, NULL,
			"bcache_ctrl");
	if (IS_ERR(device)) {
		pr_err("Cannot create control chrdev.\n");
		result = PTR_ERR(device);
		goto error_device_create;
	}

	return result;

error_device_create:
	class_destroy(ctrl->class);
error_class_create:
	cdev_del(&ctrl->cdev);
error_cdev_add:
	unregister_chrdev_region(ctrl->dev, 1);
error_alloc_chrdev_region:
	return result;
}

void bch_ctrl_device_deinit(void)
{
	struct bch_ctrl_device *ctrl = &_control_device;

	device_destroy(ctrl->class, ctrl->dev);
	class_destroy(ctrl->class);
	cdev_del(&ctrl->cdev);
	unregister_chrdev_region(ctrl->dev, 1);
}
