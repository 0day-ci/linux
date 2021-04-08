// SPDX-License-Identifier: GPL-2.0+
/*
 * This driver is developed for the IDT ClockMatrix(TM) and 82P33xxx families
 * of timing and synchronization devices. It will be used by Renesas PTP Clock
 * Manager for Linux (pcm4l) software to provide support to GNSS assisted
 * partial timing support (APTS) and other networking timing functions.
 *
 * Please note it must work with Renesas MFD driver to access device through
 * I2C/SPI.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mfd/rsmu.h>
#include <uapi/linux/rsmu.h>
#include "rsmu_cdev.h"

static struct rsmu_ops *ops_array[] = {
	[RSMU_CM] = &cm_ops,
	[RSMU_SABRE] = &sabre_ops,
};

static int
rsmu_set_combomode(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_combomode mode;
	int err;

	if (copy_from_user(&mode, arg, sizeof(mode)))
		return -EFAULT;

	if (ops->set_combomode == NULL)
		return -ENOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->set_combomode(rsmu, mode.dpll, mode.mode);
	mutex_unlock(rsmu->lock);

	return err;
}

static int
rsmu_get_dpll_state(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_get_state state_request;
	u8 state;
	int err;

	if (copy_from_user(&state_request, arg, sizeof(state_request)))
		return -EFAULT;

	if (ops->get_dpll_state == NULL)
		return -ENOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->get_dpll_state(rsmu, state_request.dpll, &state);
	mutex_unlock(rsmu->lock);

	state_request.state = state;
	if (copy_to_user(arg, &state_request, sizeof(state_request)))
		return -EFAULT;

	return err;
}

static int
rsmu_get_dpll_ffo(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_get_ffo ffo_request;
	int err;

	if (copy_from_user(&ffo_request, arg, sizeof(ffo_request)))
		return -EFAULT;

	if (ops->get_dpll_ffo == NULL)
		return -ENOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->get_dpll_ffo(rsmu, ffo_request.dpll, &ffo_request);
	mutex_unlock(rsmu->lock);

	if (copy_to_user(arg, &ffo_request, sizeof(ffo_request)))
		return -EFAULT;

	return err;
}

static struct rsmu_cdev *file2rsmu(struct file *file)
{
	return container_of(file->private_data, struct rsmu_cdev, miscdev);
}

static long
rsmu_ioctl(struct file *fptr, unsigned int cmd, unsigned long data)
{
	struct rsmu_cdev *rsmu = file2rsmu(fptr);
	void __user *arg = (void __user *)data;
	int err = 0;

	switch (cmd) {
	case RSMU_SET_COMBOMODE:
		err = rsmu_set_combomode(rsmu, arg);
		break;
	case RSMU_GET_STATE:
		err = rsmu_get_dpll_state(rsmu, arg);
		break;
	case RSMU_GET_FFO:
		err = rsmu_get_dpll_ffo(rsmu, arg);
		break;
	default:
		/* Should not get here */
		dev_err(rsmu->dev, "Undefined RSMU IOCTL");
		err = -EINVAL;
		break;
	}

	return err;
}

static long rsmu_compat_ioctl(struct file *fptr, unsigned int cmd,
			      unsigned long data)
{
	return rsmu_ioctl(fptr, cmd, data);
}

static const struct file_operations rsmu_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rsmu_ioctl,
	.compat_ioctl =	rsmu_compat_ioctl,
};

static int rsmu_init_ops(struct rsmu_cdev *rsmu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ops_array); i++)
		if (ops_array[i]->type == rsmu->type)
			break;

	if (i == ARRAY_SIZE(ops_array))
		return -EINVAL;

	rsmu->ops = ops_array[i];
	return 0;
}

static int
rsmu_probe(struct platform_device *pdev)
{
	struct rsmu_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct rsmu_cdev *rsmu;
	int err;

	rsmu = devm_kzalloc(&pdev->dev, sizeof(*rsmu), GFP_KERNEL);
	if (!rsmu)
		return -ENOMEM;

	rsmu->dev = &pdev->dev;
	rsmu->mfd = pdev->dev.parent;
	rsmu->type = pdata->type;
	rsmu->lock = pdata->lock;
	rsmu->index = pdata->index;

	/* Save driver private data */
	platform_set_drvdata(pdev, rsmu);

	/* Initialize and register the miscdev */
	rsmu->miscdev.minor = MISC_DYNAMIC_MINOR;
	rsmu->miscdev.fops = &rsmu_fops;
	snprintf(rsmu->name, sizeof(rsmu->name), "rsmu%d", rsmu->index);
	rsmu->miscdev.name = rsmu->name;
	err = misc_register(&rsmu->miscdev);
	if (err) {
		dev_err(rsmu->dev, "Unable to register device\n");
		return -ENODEV;
	}

	err = rsmu_init_ops(rsmu);
	if (err) {
		dev_err(rsmu->dev, "Unknown SMU type %d", rsmu->type);
		return err;
	}

	dev_info(rsmu->dev, "Probe %s successful\n", rsmu->name);
	return 0;
}

static int
rsmu_remove(struct platform_device *pdev)
{
	struct rsmu_cdev *rsmu = platform_get_drvdata(pdev);

	misc_deregister(&rsmu->miscdev);

	return 0;
}

static const struct platform_device_id rsmu_id_table[] = {
	{ "rsmu-cdev0", },
	{ "rsmu-cdev1", },
	{ "rsmu-cdev2", },
	{ "rsmu-cdev3", },
	{}
};
MODULE_DEVICE_TABLE(platform, rsmu_id_table);

static struct platform_driver rsmu_driver = {
	.driver = {
		.name = "rsmu",
	},
	.probe = rsmu_probe,
	.remove =  rsmu_remove,
	.id_table = rsmu_id_table,
};

module_platform_driver(rsmu_driver);

MODULE_DESCRIPTION("Renesas SMU character device driver");
MODULE_LICENSE("GPL");
