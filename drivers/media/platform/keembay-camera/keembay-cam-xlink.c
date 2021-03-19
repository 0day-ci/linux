// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera xlink
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/xlink.h>

#include "keembay-cam-xlink.h"
#include "keembay-vpu-cmd.h"

/* Do not change: it is negotiated on both sides */
#define KMB_CAM_XLINK_CTRL_CHAN_ID	27
#define KMB_CAM_XLINK_CHAN_ID_BASE	28

/* Control channel */
#define KMB_CAM_XLINK_CH_MAX_DATA_SIZE	1024
#define KMB_CAM_XLINK_CH_TIMEOUT_MS	1000

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

/**
 * kmb_cam_xlink_alloc_channel - Allocate xlink camera channel id
 * @xlink_cam: Pointer to xlink camera handle
 *
 * Each xlink channel (except main control) should have unieque id
 *
 * Return: Channel id, negative error otherwise
 */
int kmb_cam_xlink_alloc_channel(struct kmb_xlink_cam *xlink_cam)
{
	int chan_id;

	chan_id = ida_alloc_range(&xlink_cam->channel_ids,
				  KMB_CAM_XLINK_CHAN_ID_BASE,
				  U16_MAX, GFP_KERNEL);

	return chan_id;
}

/**
 * kmb_cam_xlink_free_channel - Free xlink camera channel id
 * @xlink_cam: Pointer to xlink camera handle
 * @chan_id: XLink channel ID
 */
void kmb_cam_xlink_free_channel(struct kmb_xlink_cam *xlink_cam, int chan_id)
{
	ida_free(&xlink_cam->channel_ids, chan_id);
}

/**
 * kmb_cam_xlink_open_channel - Open xlink channel for communication
 * @xlink_cam: Pointer to xlink camera handle
 * @chan_id: Xlink channel id to be opened
 *
 * Each xlink channel should be open first, to establish communication channel.
 *
 * Return: 0 if successful, error code otherwise
 */
int kmb_cam_xlink_open_channel(struct kmb_xlink_cam *xlink_cam, int chan_id)
{
	int ret;

	ret = xlink_open_channel(&xlink_cam->handle,
				 chan_id, RXB_TXB,
				 KMB_CAM_XLINK_CH_MAX_DATA_SIZE,
				 KMB_CAM_XLINK_CH_TIMEOUT_MS);
	if (ret) {
		dev_err(xlink_cam->dev, "Failed to close xlink channel %d", ret);
		return -ENODEV;
	}

	return 0;
}

/**
 * kmb_cam_xlink_close_channel - Close xlink channel
 * @xlink_cam: Pointer to xlink camera handle
 * @chan_id: Xlink channel id to be closed
 *
 * Return: 0 if successful, error code otherwise
 */
int kmb_cam_xlink_close_channel(struct kmb_xlink_cam *xlink_cam, int chan_id)
{
	int ret;

	ret = xlink_close_channel(&xlink_cam->handle, chan_id);
	if (ret) {
		dev_err(xlink_cam->dev, "Failed to close xlink channel %d", ret);
		return -ENODEV;
	}

	return 0;
}

/**
 * kmb_cam_xlink_write_msg - Write xlink message
 * @xlink_cam: Pointer to xlink camera handle
 * @chan_id: Xlink channel id where message should be writted
 * @message: Pointer to message to be written in to xlink channel
 * @msg_size: Size of the message
 *
 * Return: 0 if successful, error code otherwise
 */
int kmb_cam_xlink_write_msg(struct kmb_xlink_cam *xlink_cam, int chan_id,
			    u8 *message, u32 msg_size)
{
	int ret;

	if (msg_size > KMB_CAM_XLINK_CH_MAX_DATA_SIZE)
		return -EINVAL;

	ret = xlink_write_volatile(&xlink_cam->handle, chan_id,
				   message, msg_size);
	if (ret) {
		dev_err(xlink_cam->dev, "Fail to write xlink message %d", ret);
		return -ENODEV;
	}

	return 0;
}

/**
 * kmb_cam_xlink_read_msg - Read xlink message
 * @xlink_cam: Pointer to xlink camera handle
 * @chan_id: Xlink channel id from where message will be read
 * @message: Pointer to data where read message will be placed
 * @msg_size: Mas size of data to be read
 *
 * Return: Actual size read, negative error code otherwise
 */
int kmb_cam_xlink_read_msg(struct kmb_xlink_cam *xlink_cam, int chan_id,
			   u8 *message, u32 msg_size)
{
	u32 written_size = msg_size;
	int ret;

	if (msg_size > KMB_CAM_XLINK_CH_MAX_DATA_SIZE)
		return -EINVAL;

	ret = xlink_read_data_to_buffer(&xlink_cam->handle, chan_id,
					message, &written_size);
	if (ret) {
		dev_err(xlink_cam->dev, "Fail to read xlink message %d", ret);
		return -ENODEV;
	}

	return written_size;
}
