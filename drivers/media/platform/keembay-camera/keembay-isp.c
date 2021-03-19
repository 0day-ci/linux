// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera ISP driver.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include "keembay-isp.h"

/**
 * kmb_isp_init - Initialize Kmb isp subdevice
 * @kmb_isp: Pointer to kmb isp device
 * @dev: Pointer to camera device for which isp will be associated with
 * @csi2_config: Csi2 configuration
 * @xlink_cam: Xlink camera communication handle
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_isp_init(struct kmb_isp *kmb_isp, struct device *dev,
		 struct kmb_isp_csi2_config *csi2_config,
		 struct kmb_xlink_cam *xlink_cam)
{
	return 0;
}

/**
 * kmb_isp_cleanup - Cleanup kmb isp sub-device resourcess allocated in init
 * @kmb_isp: Pointer to kmb isp sub-device
 */
void kmb_isp_cleanup(struct kmb_isp *kmb_isp)
{ }

/**
 * kmb_isp_register_entities - Register entities
 * @kmb_isp: pointer to kmb isp device
 * @v4l2_dev: pointer to V4L2 device drivers
 *
 * Register all entities in the pipeline and create
 * links between them.
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_isp_register_entities(struct kmb_isp *kmb_isp,
			      struct v4l2_device *v4l2_dev)
{
	return 0;
}

/**
 * kmb_isp_unregister_entities - Unregister this media's entities
 * @kmb_isp: pointer to kmb isp device
 */
void kmb_isp_unregister_entities(struct kmb_isp *kmb_isp)
{ }
