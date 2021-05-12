/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_DEVICE_H
#define _NNPDRV_DEVICE_H

#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "bootimage.h"
#include "ipc_protocol.h"
#include "msg_scheduler.h"

#define NNP_MAX_DEVS		256

#define NNP_FIRMWARE_NAME "intel/nnpi/disk.img"

/* device state bits */
#define NNP_DEVICE_BOOT_BIOS_READY        BIT(1)
#define NNP_DEVICE_BOOT_RECOVERY_BIOS_READY BIT(2)
#define NNP_DEVICE_BOOT_SYSINFO_READY     BIT(3)
#define NNP_DEVICE_BOOT_STARTED           BIT(4)
#define NNP_DEVICE_BIOS_UPDATE_READY      BIT(5)
#define NNP_DEVICE_BIOS_UPDATE_STARTED    BIT(6)
#define NNP_DEVICE_BIOS_UPDATE_DONE       BIT(7)
#define NNP_DEVICE_CARD_DRIVER_READY      BIT(8)
#define NNP_DEVICE_CARD_READY             BIT(9)
#define NNP_DEVICE_CARD_ENABLED           BIT(10)

#define NNP_DEVICE_CARD_BOOT_STATE_MASK   GENMASK(9, 1)

#define NNP_DEVICE_ACTIVE_MASK       (NNP_DEVICE_CARD_READY | \
				      NNP_DEVICE_CARD_ENABLED)

#define NNP_DEVICE_FAILED_VERSION    BIT(16)
#define NNP_DEVICE_BOOT_FAILED       BIT(17)
#define NNP_DEVICE_HOST_DRIVER_ERROR BIT(18)
#define NNP_DEVICE_KERNEL_CRASH	     BIT(20)
#define NNP_DEVICE_PCI_ERROR         BIT(21)
#define NNP_DEVICE_CARD_IN_RESET     BIT(22)
#define NNP_DEVICE_FATAL_MCE_ERROR   BIT(23)
#define NNP_DEVICE_FATAL_DRAM_ECC_ERROR   BIT(24)
#define NNP_DEVICE_FATAL_ICE_ERROR   BIT(25)
#define NNP_DEVICE_HANG              BIT(26)
#define NNP_DEVICE_PROTOCOL_ERROR    BIT(27)
#define NNP_DEVICE_CAPSULE_EXPECTED  BIT(28)
#define NNP_DEVICE_CAPSULE_FAILED    BIT(29)
#define NNP_DEVICE_CORRUPTED_BOOT_IMAGE BIT(30)
#define NNP_DEVICE_ERROR_MASK        GENMASK(31, 16)

#define NNP_DEVICE_RESPONSE_FIFO_LEN    16
#define NNP_DEVICE_RESPONSE_BUFFER_LEN  (NNP_DEVICE_RESPONSE_FIFO_LEN * 2)

/**
 * struct nnp_device - structure for NNP-I device info
 * @ops: device operations implemented by the underlying device driver
 * @dev: pointer to struct device representing the NNP-I card.
 * @id: NNP-I device number
 * @cmdq_sched: message scheduler thread which schedules and serializes command
 *              submissions to the device's command queue.
 * @cmdq: input queue to @cmdq_sched used to schedule driver internal commands
 *        to be sent to the device.
 * @wq: singlethread workqueue for processing device's response messages.
 * @lock: protects accesses to @state
 * @is_recovery_bios: true if device has booted from the recovery bios flash
 * @boot_image_loaded: true if boot image load has started
 * @response_buf: buffer of device response messages arrived from "pci" layer.
 * @response_num_msgs: number of qwords available in @response_buf
 * @bios_system_info_dma_addr: dma page allocated for bios system info.
 * @bios_system_info: virtual pointer to bios system info page
 * @bios_version_str: the device's started bios version string
 * @bios_system_info_valid: true if @bios_system_info has been filled and valid
 * @state: current device boot state mask (see device state bits above)
 * @curr_boot_state: last boot state field received from device doorbell reg
 * @card_doorbell_val: last received device doorbell register value.
 * @boot_image: boot image object used to boot the card
 */
struct nnp_device {
	const struct nnp_device_ops *ops;
	struct device               *dev;
	int                         id;

	struct nnp_msched       *cmdq_sched;
	struct nnp_msched_queue *cmdq;

	struct workqueue_struct *wq;
	spinlock_t     lock;
	bool           is_recovery_bios;
	bool           boot_image_loaded;

	u64            response_buf[NNP_DEVICE_RESPONSE_BUFFER_LEN];
	unsigned int   response_num_msgs;

	dma_addr_t                  bios_system_info_dma_addr;
	struct nnp_c2h_system_info  *bios_system_info;
	char                        bios_version_str[NNP_BIOS_VERSION_LEN];
	bool                        bios_system_info_valid;

	u32            state;
	u32            curr_boot_state;
	u32            card_doorbell_val;
	struct image_info boot_image;
};

/**
 * struct nnp_device_ops - operations implemented by underlying device driver
 * @cmdq_flush: empties the device command queue, discarding all queued
 *              commands.
 * @cmdq_write_mesg: inserts a command message to the card's command queue.
 * @set_host_doorbell_value: change the host doorbell value on device.
 */
struct nnp_device_ops {
	int (*cmdq_flush)(struct nnp_device *hw_dev);
	int (*cmdq_write_mesg)(struct nnp_device *nnpdev, u64 *msg, u32 size);
	int (*set_host_doorbell_value)(struct nnp_device *nnpdev, u32 value);
};

bool nnpdev_no_devices(void);

/*
 * Functions exported by the device framework module which are
 * called by the lower layer NNP-I device driver module
 */
int nnpdev_init(struct nnp_device *nnpdev, struct device *dev,
		const struct nnp_device_ops *ops);
void nnpdev_destroy(struct nnp_device *nnpdev);
void nnpdev_card_doorbell_value_changed(struct nnp_device *nnpdev,
					u32 doorbell_val);
void nnpdev_process_messages(struct nnp_device *nnpdev, u64 *hw_msg,
			     unsigned int hw_nof_msg);

/*
 * Framework internal functions (not exported)
 */
void nnpdev_set_boot_state(struct nnp_device *nnpdev, u32 mask);

#endif
