// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>

#include "bootimage.h"
#include "device.h"
#include "host_chardev.h"
#include "msg_scheduler.h"
#include "nnp_boot_defs.h"

static DEFINE_IDA(dev_ida);

bool nnpdev_no_devices(void)
{
	return ida_is_empty(&dev_ida);
}

static void send_sysinfo_request_to_bios(struct nnp_device *nnpdev)
{
	u64 cmd[3];

	cmd[0] = FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW0_OP_MASK,
			    NNP_IPC_H2C_OP_BIOS_PROTOCOL);
	cmd[0] |= FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW0_TYPE_MASK,
			     NNP_IPC_H2C_TYPE_SYSTEM_INFO_REQ);
	cmd[0] |= FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW0_SIZE_MASK,
			     2 * sizeof(u64));

	cmd[1] = (u64)nnpdev->bios_system_info_dma_addr;

	cmd[2] = FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW2_SIZE_MASK,
			    NNP_PAGE_SIZE);

	nnpdev->ops->cmdq_flush(nnpdev);

	nnpdev->ops->cmdq_write_mesg(nnpdev, cmd, 3);
}

/**
 * build_bios_version_string() - builds printable string of bios version string
 * @nnpdev: pointer to device structure
 *
 * Initializes nnpdev->bios_version_str with printable string of bios version
 * from bios_system_info page.
 */
static void build_bios_version_string(struct nnp_device *nnpdev)
{
	unsigned int i;
	__le16 *v;

	if (!nnpdev->bios_system_info)
		return;

	/*
	 * The bios version string in the bios's system info page
	 * holds __le16 for each character in the version string.
	 * (see struct nnp_c2h_bios_version)
	 * Here we convert it to string of chars by taking only the
	 * LSB from each 16-bit character
	 */
	v = (__le16 *)&nnpdev->bios_system_info->bios_ver;

	/* check that bios version string is corrected null terminated */
	if (nnpdev->bios_system_info->bios_ver.null_terminator != 0)
		return;

	for (i = 0; i < NNP_BIOS_VERSION_LEN - 1 && v[i] != 0; ++i)
		nnpdev->bios_version_str[i] = v[i];

	nnpdev->bios_version_str[i] = '\0';
}

static int unload_boot_image(struct nnp_device *nnpdev)
{
	nnpdev->boot_image_loaded = false;
	return nnpdev_unload_boot_image(nnpdev);
}

/**
 * nnpdev_set_boot_state() - sets new device state.
 * @nnpdev: pointer to device structure
 * @mask: mask of device state bits defined in device.h
 *
 * This function sets new device status and handles the state machine of
 * device boot flow.
 * It is being called when various device notifications are received or
 * some error conditions are detected.
 *
 * The following flow describes the communication flow with the NNP-I card's
 * BIOS during the device boot flow, this function gets called when device
 * state changes when progressing in this flow:
 * 1) The device report its boot state through the "card doorbell" register,
 *    that signals an interrupt to the host and the "pci" layer in the driver
 *    calls the nnpdev_card_doorbell_value_changed function.
 * 2) When the device signals that it is "Ready to boot", the host driver
 *    sends it through the "command queue" an address of page in host memory.
 * 3) The card BIOS fills the page of memory with card system info and change
 *    the doorbell value to "sysinfo ready"
 * 4) The host driver then initiate the boot image loading.
 * 5) When boot image is ready in memory, the host driver send a
 *    "Boot image ready" message and the card BIOS starts booting and changes
 *    the doorbell value to indicate success or failure.
 * 6) When receiving indication about success/failure the host driver signals
 *    that the device no longer needs the boot image in memory.
 *    When all devices no longer need the image it will be removed.
 */
void nnpdev_set_boot_state(struct nnp_device *nnpdev, u32 mask)
{
	u32 state, prev_state;
	bool becomes_ready = false;
	int ret;

	/*
	 * Save previous state and modify current state
	 * with the changed state mask
	 */
	spin_lock(&nnpdev->lock);
	prev_state = nnpdev->state;
	if ((mask & NNP_DEVICE_CARD_BOOT_STATE_MASK) != 0) {
		/*
		 * When boot state changes previous boot states are reset.
		 * also, device error conditions is cleared.
		 */
		nnpdev->state &= ~(NNP_DEVICE_CARD_BOOT_STATE_MASK);
		nnpdev->state &= ~(NNP_DEVICE_ERROR_MASK);
	}
	nnpdev->state |= mask;
	state = nnpdev->state;
	spin_unlock(&nnpdev->lock);

	dev_dbg(nnpdev->dev,
		"device state change 0x%x --> 0x%x\n", prev_state, state);

	/* Unload boot image if boot started or failed */
	if (nnpdev->boot_image_loaded &&
	    (((state & NNP_DEVICE_BOOT_STARTED) &&
	      !(prev_state & NNP_DEVICE_BOOT_STARTED)) ||
	     (state & NNP_DEVICE_BOOT_FAILED))) {
		ret = unload_boot_image(nnpdev);
		/* This should never fail */
		if (ret)
			dev_dbg(nnpdev->dev,
				"Unexpected error while unloading boot image. rc=%d\n",
				ret);
	}

	/* if in error state - no need to check rest of the states */
	if (state & NNP_DEVICE_ERROR_MASK)
		return;

	if ((state & NNP_DEVICE_BOOT_BIOS_READY) &&
	    !(prev_state & NNP_DEVICE_BOOT_BIOS_READY)) {
		becomes_ready = true;
		nnpdev->is_recovery_bios = false;
	}

	if ((state & NNP_DEVICE_BOOT_RECOVERY_BIOS_READY) &&
	    !(prev_state & NNP_DEVICE_BOOT_RECOVERY_BIOS_READY)) {
		becomes_ready = true;
		nnpdev->is_recovery_bios = true;
	}

	if (becomes_ready ||
	    mask == NNP_DEVICE_BOOT_BIOS_READY ||
	    mask == NNP_DEVICE_BOOT_RECOVERY_BIOS_READY) {
		if (!becomes_ready)
			dev_dbg(nnpdev->dev, "Re-sending sysinfo page to bios!!\n");

		/* Send request to fill system_info buffer */
		send_sysinfo_request_to_bios(nnpdev);
		return;
	}

	/* Handle boot image request */
	if ((state & NNP_DEVICE_BOOT_SYSINFO_READY) &&
	    !(prev_state & NNP_DEVICE_BOOT_SYSINFO_READY) &&
	    !nnpdev->boot_image_loaded) {
		build_bios_version_string(nnpdev);
		nnpdev->bios_system_info_valid = true;
		nnpdev->boot_image_loaded = true;
		ret = nnpdev_load_boot_image(nnpdev);

		if (ret)
			dev_err(nnpdev->dev,
				"Unexpected error while loading boot image. rc=%d\n",
				ret);
	}
}

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

	nnpdev->wq = create_singlethread_workqueue("nnpdev_wq");
	if (!nnpdev->wq) {
		ret = -ENOMEM;
		goto err_cmdq;
	}

	/* setup memory for bios system info */
	nnpdev->bios_system_info =
		dma_alloc_coherent(nnpdev->dev, NNP_PAGE_SIZE,
				   &nnpdev->bios_system_info_dma_addr, GFP_KERNEL);
	if (!nnpdev->bios_system_info) {
		ret = -ENOMEM;
		goto err_wq;
	}

	/* set host driver state to "Not ready" */
	nnpdev->ops->set_host_doorbell_value(nnpdev, 0);

	spin_lock_init(&nnpdev->lock);
	nnpdev_boot_image_init(&nnpdev->boot_image);

	return 0;

err_wq:
	destroy_workqueue(nnpdev->wq);
err_cmdq:
	nnp_msched_queue_destroy(nnpdev->cmdq);
err_msg_sched:
	nnp_msched_destroy(nnpdev->cmdq_sched);
err_ida:
	ida_simple_remove(&dev_ida, nnpdev->id);
	return ret;
}
EXPORT_SYMBOL(nnpdev_init);

struct doorbell_work {
	struct work_struct work;
	struct nnp_device  *nnpdev;
	u32                val;
};

static void doorbell_changed_handler(struct work_struct *work)
{
	struct doorbell_work *req = container_of(work, struct doorbell_work,
						 work);
	u32 boot_state, state = 0;
	u32 error_state;
	u32 doorbell_val = req->val;
	struct nnp_device *nnpdev = req->nnpdev;

	nnpdev->card_doorbell_val = doorbell_val;

	error_state = FIELD_GET(NNP_CARD_ERROR_MASK, doorbell_val);
	boot_state = FIELD_GET(NNP_CARD_BOOT_STATE_MASK, doorbell_val);

	if (error_state) {
		state = NNP_DEVICE_BOOT_FAILED;

		switch (error_state) {
		case NNP_CARD_ERROR_NOT_CAPSULE:
			state |= NNP_DEVICE_CAPSULE_EXPECTED;
			break;
		case NNP_CARD_ERROR_CORRUPTED_IMAGE:
			state |= NNP_DEVICE_CORRUPTED_BOOT_IMAGE;
			break;
		case NNP_CARD_ERROR_CAPSULE_FAILED:
			state |= NNP_DEVICE_CAPSULE_FAILED;
			break;
		default:
			break;
		}
	} else if (boot_state != nnpdev->curr_boot_state) {
		nnpdev->curr_boot_state = boot_state;
		switch (boot_state) {
		case NNP_CARD_BOOT_STATE_BIOS_READY:
			state = NNP_DEVICE_BOOT_BIOS_READY;
			break;
		case NNP_CARD_BOOT_STATE_RECOVERY_BIOS_READY:
			state = NNP_DEVICE_BOOT_RECOVERY_BIOS_READY;
			break;
		case NNP_CARD_BOOT_STATE_BIOS_SYSINFO_READY:
			state = NNP_DEVICE_BOOT_SYSINFO_READY;
			break;
		case NNP_CARD_BOOT_STATE_BOOT_STARTED:
			state = NNP_DEVICE_BOOT_STARTED;
			break;
		case NNP_CARD_BOOT_STATE_BIOS_FLASH_STARTED:
			state = NNP_DEVICE_BIOS_UPDATE_STARTED;
			break;
		default:
			break;
		}
	}

	if (state)
		nnpdev_set_boot_state(nnpdev, state);

	kfree(req);
}

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
	struct doorbell_work *req;

	dev_dbg(nnpdev->dev, "Got card doorbell value 0x%x\n", doorbell_val);

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return;

	req->nnpdev = nnpdev;
	req->val = doorbell_val;
	INIT_WORK(&req->work, doorbell_changed_handler);
	queue_work(nnpdev->wq, &req->work);
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

	/*
	 * If device is removed while boot image load is in-flight,
	 * stop the image load and flag it is not needed.
	 */
	if (nnpdev->boot_image_loaded)
		unload_boot_image(nnpdev);

	destroy_workqueue(nnpdev->wq);

	dma_free_coherent(nnpdev->dev, NNP_PAGE_SIZE, nnpdev->bios_system_info,
			  nnpdev->bios_system_info_dma_addr);

	nnp_msched_destroy(nnpdev->cmdq_sched);
	ida_simple_remove(&dev_ida, nnpdev->id);
}
EXPORT_SYMBOL(nnpdev_destroy);

static int __init nnp_init(void)
{
	return nnp_init_host_interface();
}
subsys_initcall(nnp_init);

static void __exit nnp_cleanup(void)
{
	nnp_release_host_interface();
	/* dev_ida is already empty here - no point calling ida_destroy */
}
module_exit(nnp_cleanup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel(R) NNPI Framework");
MODULE_AUTHOR("Intel Corporation");
MODULE_FIRMWARE(NNP_FIRMWARE_NAME);
