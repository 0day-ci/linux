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
#include <linux/regmap.h>

#include "sensehat.h"

#define GAMMA_SIZE sizeof_field(struct sensehat_display, gamma)
#define VMEM_SIZE sizeof_field(struct sensehat_display, vmem)

static int sensehat_update_display(struct sensehat *sensehat);

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

static const struct file_operations sensehat_display_fops;

static int sensehat_display_probe(struct platform_device *pdev)
{
	int ret;

	struct sensehat *sensehat = dev_get_drvdata(&pdev->dev);
	struct sensehat_display *sensehat_display = &sensehat->display;

	memcpy(sensehat_display->gamma, gamma_presets[lowlight], GAMMA_SIZE);

	memset(sensehat_display->vmem, 0, VMEM_SIZE);

	mutex_init(&sensehat_display->rw_mtx);

	sensehat_display->mdev = (struct miscdevice) {
		.minor	= MISC_DYNAMIC_MINOR,
		.name	= "sense-hat",
		.mode	= 0666,
		.fops	= &sensehat_display_fops,
	};

	ret = misc_register(&sensehat_display->mdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register 8x8 LED matrix display.\n");
		return ret;
	}

	dev_info(&pdev->dev, "8x8 LED matrix display registered with minor number %i",
		sensehat_display->mdev.minor);

	sensehat_update_display(sensehat);
	return 0;
}

static int sensehat_display_remove(struct platform_device *pdev)
{
	struct sensehat *sensehat = dev_get_drvdata(&pdev->dev);
	struct sensehat_display *sensehat_display = &sensehat->display;

	misc_deregister(&sensehat_display->mdev);
	return 0;
}

static loff_t sensehat_display_llseek(struct file *filp, loff_t pos, int whence)
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
sensehat_display_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct sensehat *sensehat = container_of(filp->private_data, struct sensehat, display.mdev);
	struct sensehat_display *sensehat_display = &sensehat->display;
	ssize_t retval = -EFAULT;

	if (*f_pos >= VMEM_SIZE)
		return 0;
	if (*f_pos + count > VMEM_SIZE)
		count = VMEM_SIZE - *f_pos;
	if (mutex_lock_interruptible(&sensehat_display->rw_mtx))
		return -ERESTARTSYS;
	if (copy_to_user(buf, sensehat_display->vmem + *f_pos, count))
		goto out;
	*f_pos += count;
	retval = count;
out:
	mutex_unlock(&sensehat_display->rw_mtx);
	return retval;
}

static ssize_t
sensehat_display_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct sensehat *sensehat = container_of(filp->private_data, struct sensehat, display.mdev);
	struct sensehat_display *sensehat_display = &sensehat->display;
	u8 temp[VMEM_SIZE];

	if (*f_pos >= VMEM_SIZE)
		return -EFBIG;
	if (*f_pos + count > VMEM_SIZE)
		count = VMEM_SIZE - *f_pos;
	if (copy_from_user(temp, buf, count))
		return -EFAULT;
	if (mutex_lock_interruptible(&sensehat_display->rw_mtx))
		return -ERESTARTSYS;
	memcpy(sensehat_display->vmem + *f_pos, temp, count);
	sensehat_update_display(sensehat);
	*f_pos += count;
	mutex_unlock(&sensehat_display->rw_mtx);
	return count;
}

static long sensehat_display_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	struct sensehat *sensehat = container_of(filp->private_data, struct sensehat, display.mdev);
	struct sensehat_display *sensehat_display = &sensehat->display;
	void __user *user_ptr = (void __user *)arg;
	u8 temp[GAMMA_SIZE];
	int ret;

	if (mutex_lock_interruptible(&sensehat_display->rw_mtx))
		return -ERESTARTSYS;
	switch (cmd) {
	case SENSEDISP_IOGET_GAMMA:
		if (copy_to_user(user_ptr, sensehat_display->gamma, GAMMA_SIZE)) {
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
	memcpy(sensehat_display->gamma, temp, GAMMA_SIZE);
	sensehat_update_display(sensehat);
out_unlock:
	mutex_unlock(&sensehat_display->rw_mtx);
	return ret;
}

int sensehat_update_display(struct sensehat *sensehat)
{
	int i, j, ret;
	struct sensehat_display *display = &sensehat->display;
	sensehat_fb_t pixel_data;

	for (i = 0; i < 8; ++i) {
		for (j = 0; j < 8; ++j) {
			pixel_data[i][0][j] = display->gamma[display->vmem[i][j].r];
			pixel_data[i][1][j] = display->gamma[display->vmem[i][j].g];
			pixel_data[i][2][j] = display->gamma[display->vmem[i][j].b];
		}
	}

	ret = regmap_bulk_write(sensehat->regmap, SENSEHAT_DISPLAY,
				pixel_data, sizeof(pixel_data));
	if (ret < 0)
		dev_err(sensehat->dev, "Update to 8x8 LED matrix display failed");
	return ret;
}

static const struct file_operations sensehat_display_fops = {
	.owner		= THIS_MODULE,
	.llseek		= sensehat_display_llseek,
	.read		= sensehat_display_read,
	.write		= sensehat_display_write,
	.unlocked_ioctl	= sensehat_display_ioctl,
};

static struct platform_device_id sensehat_display_device_id[] = {
	{ .name = "sensehat-display" },
	{ },
};
MODULE_DEVICE_TABLE(platform, sensehat_display_device_id);

static struct platform_driver sensehat_display_driver = {
	.probe = sensehat_display_probe,
	.remove = sensehat_display_remove,
	.driver = {
		.name = "sensehat-display",
	},
};

module_platform_driver(sensehat_display_driver);

MODULE_DESCRIPTION("Raspberry Pi Sense HAT 8x8 LED matrix display driver");
MODULE_AUTHOR("Serge Schneider <serge@raspberrypi.org>");
MODULE_LICENSE("GPL");
