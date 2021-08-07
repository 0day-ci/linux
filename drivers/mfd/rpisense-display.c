// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Raspberry Pi Sense HAT 8x8 LED matrix display driver
 * http://raspberrypi.org
 *
 * Copyright (C) 2015 Raspberry Pi
 * Copyright (C) 2021 Charles Mirabile, Mwesigwa Guma, Joel Savitz
 *
 * Original Author: Serge Schneider
 * Revised for upstream Linux by: Charles Mirabile, Mwesigwa Guma, Joel Savitz
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <linux/mfd/rpisense.h>

#define GAMMA_SIZE sizeof_field(struct rpisense_display, gamma)
#define VMEM_SIZE sizeof_field(struct rpisense_display, vmem)

static bool lowlight;
module_param(lowlight, bool, 0);
MODULE_PARM_DESC(lowlight, "Reduce LED matrix brightness to one third");

static const u8 gamma_presets[][GAMMA_SIZE] = {
	[GAMMA_DEFAULT] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
		0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x0F, 0x11,
		0x12, 0x14, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F,
	},
	[GAMMA_LOWLIGHT] = {
		0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
		0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06,
		0x06, 0x07, 0x07, 0x08, 0x08, 0x09, 0x0A, 0x0A,
	},
};

static const struct file_operations rpisense_display_fops;

static int rpisense_display_probe(struct platform_device *pdev)
{
	int ret;

	struct rpisense *rpisense = dev_get_drvdata(&pdev->dev);
	struct rpisense_display *rpisense_display = &rpisense->display;

	memcpy(rpisense_display->gamma, gamma_presets[lowlight], GAMMA_SIZE);

	memset(rpisense_display->vmem, 0, VMEM_SIZE);

	mutex_init(&rpisense_display->rw_mtx);

	rpisense_display->mdev = (struct miscdevice) {
		.minor	= MISC_DYNAMIC_MINOR,
		.name	= "sense-hat",
		.mode	= 0666,
		.fops	= &rpisense_display_fops,
	};

	ret = misc_register(&rpisense_display->mdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register 8x8 LED matrix display.\n");
		return ret;
	}

	dev_info(&pdev->dev, "8x8 LED matrix display registered with minor number %i",
		rpisense_display->mdev.minor);

	rpisense_update_display(rpisense);
	return 0;
}

static int rpisense_display_remove(struct platform_device *pdev)
{
	struct rpisense *rpisense = dev_get_drvdata(&pdev->dev);
	struct rpisense_display *rpisense_display = &rpisense->display;

	misc_deregister(&rpisense_display->mdev);
	return 0;
}

static loff_t rpisense_display_llseek(struct file *filp, loff_t pos, int whence)
{
	loff_t base;

	switch (whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = filp->f_pos;
		break;
	case SEEK_END:
		base = VMEM_SIZE;
		break;
	default:
		return -EINVAL;
	}
	base += pos;
	if (base < 0 || base >= VMEM_SIZE)
		return -EINVAL;
	filp->f_pos = base;
	return base;
}

static ssize_t
rpisense_display_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct rpisense *rpisense = container_of(filp->private_data, struct rpisense, display.mdev);
	struct rpisense_display *rpisense_display = &rpisense->display;
	ssize_t retval = -EFAULT;

	if (*f_pos >= VMEM_SIZE)
		return 0;
	if (*f_pos + count > VMEM_SIZE)
		count = VMEM_SIZE - *f_pos;
	if (mutex_lock_interruptible(&rpisense_display->rw_mtx))
		return -ERESTARTSYS;
	if (copy_to_user(buf, rpisense_display->vmem + *f_pos, count))
		goto out;
	*f_pos += count;
	retval = count;
out:
	mutex_unlock(&rpisense_display->rw_mtx);
	return retval;
}

static ssize_t
rpisense_display_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct rpisense *rpisense = container_of(filp->private_data, struct rpisense, display.mdev);
	struct rpisense_display *rpisense_display = &rpisense->display;
	u8 temp[VMEM_SIZE];

	if (*f_pos >= VMEM_SIZE)
		return -EFBIG;
	if (*f_pos + count > VMEM_SIZE)
		count = VMEM_SIZE - *f_pos;
	if (copy_from_user(temp, buf, count))
		return -EFAULT;
	if (mutex_lock_interruptible(&rpisense_display->rw_mtx))
		return -ERESTARTSYS;
	memcpy(rpisense_display->vmem + *f_pos, temp, count);
	rpisense_update_display(rpisense);
	*f_pos += count;
	mutex_unlock(&rpisense_display->rw_mtx);
	return count;
}

static long rpisense_display_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	struct rpisense *rpisense = container_of(filp->private_data, struct rpisense, display.mdev);
	struct rpisense_display *rpisense_display = &rpisense->display;
	void __user *user_ptr = (void __user *)arg;
	u8 temp[GAMMA_SIZE];
	int ret;

	if (mutex_lock_interruptible(&rpisense_display->rw_mtx))
		return -ERESTARTSYS;
	switch (cmd) {
	case SENSEDISP_IOGET_GAMMA:
		if (copy_to_user(user_ptr, rpisense_display->gamma, GAMMA_SIZE)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		ret = 0;
		goto out_unlock;
	case SENSEDISP_IOSET_GAMMA:
		if (copy_from_user(temp, user_ptr, GAMMA_SIZE)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		ret = 0;
		goto out_update;
	case SENSEDISP_IORESET_GAMMA:
		if (arg < GAMMA_DEFAULT || arg >= GAMMA_PRESET_COUNT) {
			ret = -EINVAL;
			goto out_unlock;
		}
		memcpy(temp, gamma_presets[arg], GAMMA_SIZE);
		ret = 0;
		goto out_update;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}
out_update:
	memcpy(rpisense_display->gamma, temp, GAMMA_SIZE);
	rpisense_update_display(rpisense);
out_unlock:
	mutex_unlock(&rpisense_display->rw_mtx);
	return ret;
}

static const struct file_operations rpisense_display_fops = {
	.owner		= THIS_MODULE,
	.llseek		= rpisense_display_llseek,
	.read		= rpisense_display_read,
	.write		= rpisense_display_write,
	.unlocked_ioctl	= rpisense_display_ioctl,
};

#ifdef CONFIG_OF
static const struct of_device_id rpisense_display_id[] = {
	{ .compatible = "rpi,rpi-sense-fb" },
	{ },
};
MODULE_DEVICE_TABLE(of, rpisense_display_id);
#endif

static struct platform_device_id rpisense_display_device_id[] = {
	{ .name = "rpi-sense-fb" },
	{ },
};
MODULE_DEVICE_TABLE(platform, rpisense_display_device_id);

static struct platform_driver rpisense_display_driver = {
	.probe = rpisense_display_probe,
	.remove = rpisense_display_remove,
	.driver = {
		.name = "rpi-sense-fb",
	},
};

module_platform_driver(rpisense_display_driver);

MODULE_DESCRIPTION("Raspberry Pi Sense HAT 8x8 LED matrix display driver");
MODULE_AUTHOR("Serge Schneider <serge@raspberrypi.org>");
MODULE_LICENSE("GPL");

