// SPDX-License-Identifier: GPL-2.0
/*
 * USB Power Delivery /dev entries
 *
 * Copyright (C) 2021 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/usb/pd_dev.h>

#include "class.h"

#define PD_DEV_MAX (MINORMASK + 1)

dev_t usbpd_devt;
static struct cdev usb_pd_cdev;

struct pddev {
	struct device *dev;
	struct typec_port *port;
	const struct pd_dev *pd_dev;
};

static ssize_t usbpd_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	/* FIXME TODO XXX */

	/* Alert and Attention handling here (with poll) ? */

	return 0;
}

static long usbpd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pddev *pd = file->private_data;
	void __user *p = (void __user *)arg;
	unsigned int pwr_role;
	struct pd_message msg;
	u32 configuration;
	int ret = 0;

	switch (cmd) {
	case USBPDDEV_INFO:
		if (copy_to_user(p, pd->pd_dev->info, sizeof(*pd->pd_dev->info)))
			return -EFAULT;
		break;
	case USBPDDEV_CONFIGURE:
		if (!pd->pd_dev->ops->configure)
			return -ENOTTY;

		if (copy_from_user(&configuration, p, sizeof(configuration)))
			return -EFAULT;

		ret = pd->pd_dev->ops->configure(pd->pd_dev, configuration);
		if (ret)
			return ret;
		break;
	case USBPDDEV_PWR_ROLE:
		if (is_typec_plug(pd->dev))
			return -ENOTTY;

		if (is_typec_partner(pd->dev)) {
			if (pd->port->pwr_role == TYPEC_SINK)
				pwr_role = TYPEC_SOURCE;
			else
				pwr_role = TYPEC_SINK;
		} else {
			pwr_role = pd->port->pwr_role;
		}

		if (copy_to_user(p, &pwr_role, sizeof(unsigned int)))
			return -EFAULT;
		break;
	case USBPDDEV_GET_MESSAGE:
		if (!pd->pd_dev->ops->get_message)
			return -ENOTTY;

		if (copy_from_user(&msg, p, sizeof(msg)))
			return -EFAULT;

		ret = pd->pd_dev->ops->get_message(pd->pd_dev, &msg);
		if (ret)
			return ret;

		if (copy_to_user(p, &msg, sizeof(msg)))
			return -EFAULT;
		break;
	case USBPDDEV_SET_MESSAGE:
		if (!pd->pd_dev->ops->set_message)
			return -ENOTTY;

		ret = pd->pd_dev->ops->set_message(pd->pd_dev, &msg);
		if (ret)
			return ret;

		if (copy_to_user(p, &msg, sizeof(msg)))
			return -EFAULT;
		break;
	case USBPDDEV_SUBMIT_MESSAGE:
		if (!pd->pd_dev->ops->submit)
			return -ENOTTY;

		if (copy_from_user(&msg, p, sizeof(msg)))
			return -EFAULT;

		ret = pd->pd_dev->ops->submit(pd->pd_dev, &msg);
		if (ret)
			return ret;

		if (copy_to_user(p, &msg, sizeof(msg)))
			return -EFAULT;
		break;
	default:
		return -ENOTTY;
	}

	return 0;
}

static int usbpd_open(struct inode *inode, struct file *file)
{
	struct device *dev;
	struct pddev *pd;

	dev = class_find_device_by_devt(&typec_class, inode->i_rdev);
	if (!dev)
		return -ENODEV;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		put_device(dev);
		return -ENOMEM;
	}

	if (is_typec_partner(dev)) {
		if (!to_typec_partner(dev)->usb_pd) {
			put_device(dev);
			kfree(pd);
			return -ENODEV;
		}
		pd->port = to_typec_port(dev->parent);
		pd->pd_dev = to_typec_partner(dev)->pd_dev;
	} else if (is_typec_plug(dev)) {
		pd->port = to_typec_port(dev->parent->parent);
		pd->pd_dev = to_typec_plug(dev)->pd_dev;
	} else {
		pd->port = to_typec_port(dev);
		pd->pd_dev = to_typec_port(dev)->pd_dev;
	}

	pd->dev = dev;
	file->private_data = pd;

	return 0;
}

static int usbpd_release(struct inode *inode, struct file *file)
{
	struct pddev *pd = file->private_data;

	put_device(pd->dev);
	kfree(pd);

	return 0;
}

const struct file_operations usbpd_file_operations = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= usbpd_read,
	.unlocked_ioctl = usbpd_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= usbpd_open,
	.release	= usbpd_release,
};

int __init usbpd_dev_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&usbpd_devt, 0, PD_DEV_MAX, "usb_pd");
	if (ret)
		return ret;

	/*
	 * FIXME!
	 *
	 * Now the cdev is always created, even when the device does not support
	 * USB PD.
	 */

	cdev_init(&usb_pd_cdev, &usbpd_file_operations);

	ret = cdev_add(&usb_pd_cdev, usbpd_devt, PD_DEV_MAX);
	if (ret) {
		unregister_chrdev_region(usbpd_devt, PD_DEV_MAX);
		return ret;
	}

	return 0;
}

void __exit usbpd_dev_exit(void)
{
	cdev_del(&usb_pd_cdev);
	unregister_chrdev_region(usbpd_devt, PD_DEV_MAX);
}
