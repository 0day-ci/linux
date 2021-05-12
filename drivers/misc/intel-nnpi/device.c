// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>

#include "device.h"
#include "msg_scheduler.h"

static DEFINE_IDA(dev_ida);

/**
 * nnpdev_init() - initialize NNP-I device structure.
 * @nnpdev: device to be initialized
 * @dev: device structure representing the card device
 * @ops: NNP-I device driver operations
 *
 * This function is called by the device driver module when a new NNP-I device
 * is created. The function initialize NNP-I framework's device structure.
 * The device driver must call nnpdev_destroy before the underlying device is
 * removed and before the driver module get unloaded.
 * The device driver must also make sure that when nnpdev_destroy is called the
 * device is quiesced. Meaning, the physical device does no longer throw events
 * and no operations on the nnpdev will be requested.
 *
 * Return: zero on success, error value otherwise.
 */
int nnpdev_init(struct nnp_device *nnpdev, struct device *dev,
		const struct nnp_device_ops *ops)
{
	int ret;

	ret = ida_simple_get(&dev_ida, 0, NNP_MAX_DEVS, GFP_KERNEL);
	if (ret < 0)
		return ret;

	nnpdev->id = ret;
	/*
	 * It is fine to keep pointers to the underlying device and driver
	 * ops since driver must call nnpdev_destroy before the device is
	 * removed or module gets unloaded.
	 */
	nnpdev->dev = dev;
	nnpdev->ops = ops;

	nnpdev->cmdq_sched = nnp_msched_create(nnpdev);
	if (!nnpdev->cmdq_sched) {
		ret = -ENOMEM;
		goto err_ida;
	}

	nnpdev->cmdq = nnp_msched_queue_create(nnpdev->cmdq_sched);
	if (!nnpdev->cmdq) {
		ret = -ENOMEM;
		goto err_msg_sched;
	}

	return 0;

err_msg_sched:
	nnp_msched_destroy(nnpdev->cmdq_sched);
err_ida:
	ida_simple_remove(&dev_ida, nnpdev->id);
	return ret;
}
EXPORT_SYMBOL(nnpdev_init);

/**
 * nnpdev_card_doorbell_value_changed() - card doorbell changed notification
 * @nnpdev: The nnp device
 * @doorbell_val: The new value of the doorbell register
 *
 * This function is called from the NNP-I device driver when the card's doorbell
 * register is changed.
 */
void nnpdev_card_doorbell_value_changed(struct nnp_device *nnpdev,
					u32 doorbell_val)
{
	dev_dbg(nnpdev->dev, "Got card doorbell value 0x%x\n", doorbell_val);
}
EXPORT_SYMBOL(nnpdev_card_doorbell_value_changed);

/**
 * nnpdev_destroy() - destroy nnp device object
 * @nnpdev: The nnp device to be destroyed.
 *
 * This function must be called by the device driver module when NNP-I device
 * is removed or the device driver get unloaded.
 */
void nnpdev_destroy(struct nnp_device *nnpdev)
{
	dev_dbg(nnpdev->dev, "Destroying NNP-I device\n");

	nnp_msched_destroy(nnpdev->cmdq_sched);
	ida_simple_remove(&dev_ida, nnpdev->id);
}
EXPORT_SYMBOL(nnpdev_destroy);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel(R) NNPI Framework");
MODULE_AUTHOR("Intel Corporation");
