// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera Video node.
 *
 * Copyright (C) 2018-2020 Intel Corporation
 */
#include "keembay-video.h"

/**
 * kmb_video_init - Initialize entity
 * @kmb_vid: pointer to kmb video device
 * @name: entity name
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_video_init(struct kmb_video *kmb_vid, const char *name)
{
	return 0;
}

/**
 * kmb_video_cleanup - Free resources associated with entity
 * @kmb_vid: pointer to kmb video device
 */
void kmb_video_cleanup(struct kmb_video *kmb_vid)
{ }

/**
 * kmb_video_register - Register V4L2 device
 * @kmb_vid: pointer to kmb video device
 * @v4l2_dev: pointer to V4L2 device drivers
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_video_register(struct kmb_video *kmb_vid,
		       struct v4l2_device *v4l2_dev)
{
	return 0;
}

/**
 * kmb_video_unregister - Unregister V4L device
 * @kmb_vid: pointer to kmb video device
 */
void kmb_video_unregister(struct kmb_video *kmb_vid)
{ }
