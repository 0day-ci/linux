// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera xlink
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/device.h>
#include <linux/idr.h>

#include "keembay-cam-xlink.h"

/**
 * kmb_cam_xlink_init - Initialize xlink for VPU camera communication
 * @xlink_cam: Pointer to xlink camera handle
 * @dev: Client device of the xlink
 *
 * Perform initialization and establish connection with xlink VPUIP device
 *
 * Return: 0 if successful, error code otherwise
 */
int kmb_cam_xlink_init(struct kmb_xlink_cam *xlink_cam, struct device *dev)
{
	int ret;

	/* Connect to the device before opening channels */
	memset(&xlink_cam->handle, 0, sizeof(xlink_cam->handle));
	xlink_cam->handle.dev_type = VPUIP_DEVICE;
	ret = xlink_connect(&xlink_cam->handle);
	if (ret) {
		dev_err(xlink_cam->dev, "Failed to connect: %d\n", ret);
		return ret;
	}

	ida_init(&xlink_cam->channel_ids);
	xlink_cam->ctrl_chan_refcnt = 0;

	mutex_init(&xlink_cam->lock);
	xlink_cam->dev = dev;

	return 0;
}

/**
 * kmb_cam_xlink_cleanup - Cleanup xlink camera communication
 * @xlink_cam: Pointer to xlink camera handle
 *
 * Return: 0 if successful, error code otherwise
 */
void kmb_cam_xlink_cleanup(struct kmb_xlink_cam *xlink_cam)
{
	/* Disconnect from the device after closing channels */
	xlink_disconnect(&xlink_cam->handle);
	ida_destroy(&xlink_cam->channel_ids);
}
