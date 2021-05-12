/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_DEVICE_H
#define _NNPDRV_DEVICE_H

#define NNP_MAX_DEVS		256

/**
 * struct nnp_device - structure for NNP-I device info
 * @ops: device operations implemented by the underlying device driver
 * @dev: pointer to struct device representing the NNP-I card.
 * @id: NNP-I device number
 * @cmdq_sched: message scheduler thread which schedules and serializes command
 *              submissions to the device's command queue.
 * @cmdq: input queue to @cmdq_sched used to schedule driver internal commands
 *        to be sent to the device.
 */
struct nnp_device {
	const struct nnp_device_ops *ops;
	struct device               *dev;
	int                         id;

	struct nnp_msched       *cmdq_sched;
	struct nnp_msched_queue *cmdq;
};

/**
 * struct nnp_device_ops - operations implemented by underlying device driver
 * @cmdq_flush: empties the device command queue, discarding all queued
 *              commands.
 * @cmdq_write_mesg: inserts a command message to the card's command queue.
 */
struct nnp_device_ops {
	int (*cmdq_flush)(struct nnp_device *hw_dev);
	int (*cmdq_write_mesg)(struct nnp_device *nnpdev, u64 *msg, u32 size);
};

/*
 * Functions exported by the device framework module which are
 * called by the lower layer NNP-I device driver module
 */
int nnpdev_init(struct nnp_device *nnpdev, struct device *dev,
		const struct nnp_device_ops *ops);
void nnpdev_destroy(struct nnp_device *nnpdev);
void nnpdev_card_doorbell_value_changed(struct nnp_device *nnpdev,
					u32 doorbell_val);
#endif
