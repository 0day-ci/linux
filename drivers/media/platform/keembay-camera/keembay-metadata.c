// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera ISP metadata video node.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include "keembay-metadata.h"

/**
 * kmb_video_init - Initialize entity
 * @kmb_meta: pointer to kmb isp config device
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_metadata_init(struct kmb_metadata *kmb_meta)
{
	return 0;
}

/**
 * kmb_metadata_cleanup - Free resources associated with entity
 * @kmb_meta: pointer to kmb isp config device
 */
void kmb_metadata_cleanup(struct kmb_metadata *kmb_meta)
{ }

/**
 * kmb_metadata_register - Register V4L2 device
 * @kmb_meta: pointer to kmb isp config device
 * @v4l2_dev: pointer to V4L2 device drivers
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_metadata_register(struct kmb_metadata *kmb_meta,
			  struct v4l2_device *v4l2_dev)
{
	return 0;
}

/**
 * kmb_metadata_unregister - Unregister V4L device
 * @kmb_meta: pointer to kmb isp config device
 */
void kmb_metadata_unregister(struct kmb_metadata *kmb_meta)
{ }
