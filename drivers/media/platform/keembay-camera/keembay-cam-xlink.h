/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera xlink
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_CAM_XLINK_H
#define KEEMBAY_CAM_XLINK_H

#include <linux/xlink.h>

/**
 * struct kmb_xlink_cam - KMB Camera xlink communication
 * @dev: Device client of the xlink
 * @lock: Mutex to serialize access to kmb xlink communication channels
 * @handle: Xlink handle
 * @ctrl_chan_refcnt: Main control channel reference count
 * @channel_ids: Channel IDs. Each channel should have unique ID
 */
struct kmb_xlink_cam {
	struct device *dev;
	struct mutex lock;
	struct xlink_handle handle;
	unsigned int ctrl_chan_refcnt;
	struct ida channel_ids;
};

int kmb_cam_xlink_init(struct kmb_xlink_cam *xlink_cam, struct device *dev);
void kmb_cam_xlink_cleanup(struct kmb_xlink_cam *xlink_cam);

int kmb_cam_xlink_alloc_channel(struct kmb_xlink_cam *xlink_cam);
void kmb_cam_xlink_free_channel(struct kmb_xlink_cam *xlink_cam, int chan_id);

int kmb_cam_xlink_open_channel(struct kmb_xlink_cam *xlink_cam, int chan_id);
int kmb_cam_xlink_close_channel(struct kmb_xlink_cam *xlink_cam, int chan_id);

int kmb_cam_xlink_write_msg(struct kmb_xlink_cam *xlink_cam, int chan_id,
			    u8 *message, u32 msg_size);
int kmb_cam_xlink_read_msg(struct kmb_xlink_cam *xlink_cam, int chan_id,
			   u8 *message, u32 msg_size);

#endif /* KEEMBAY_CAM_XLINK_H */
