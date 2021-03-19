/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera driver.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_CAMERA_H
#define KEEMBAY_CAMERA_H

#include <media/v4l2-device.h>

#include "keembay-cam-xlink.h"
#include "keembay-isp.h"

/**
 * struct kmb_camera_receiver - Keem Bay camera receiver
 * @asd: V4L2 asynchronous sub-device
 * @csi2_config: CSI-2 configuration
 * @isp: ISP device
 */
struct kmb_camera_receiver {
	struct v4l2_async_subdev asd;
	struct kmb_isp_csi2_config csi2_config;
	struct kmb_isp isp;
};

/**
 * struct kmb_cam - Keem Bay camera media device
 * @dev: Pointer to basic device structure
 * @media_dev: Media device
 * @v4l2_dev: V4L2 device
 * @v4l2_notifier: V4L2 async notifier
 * @xlink_cam: Xlink camera communication handler
 */
struct kmb_camera {
	struct device *dev;
	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier v4l2_notifier;
	struct kmb_xlink_cam xlink_cam;
};

#endif /* KEEMBAY_CAMERA_H */
