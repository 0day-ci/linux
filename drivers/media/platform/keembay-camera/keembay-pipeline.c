// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera pipeline.
 *
 * Copyright (C) 2020 Intel Corporation
 */
#include <media/v4l2-device.h>

#include "keembay-pipeline.h"
#include "keembay-vpu-cmd.h"

/**
 * kmb_pipe_init - Initialize KMB Pipeline
 * @pipe: pointer to pipeline object
 * @dev: pointer to device
 * @xlink_cam: pointer to xlink cam handle
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_pipe_init(struct kmb_pipeline *pipe, struct device *dev,
		  struct kmb_xlink_cam *xlink_cam)
{
	return 0;
}

/**
 * kmb_pipe_cleanup - Cleanup KMB Pipeline
 * @pipe: pointer to pipeline object
 */
void kmb_pipe_cleanup(struct kmb_pipeline *pipe)
{ }

/**
 * kmb_pipe_request - Request a pipeline
 * @pipe: pointer to pipeline object
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_pipe_request(struct kmb_pipeline *pipe)
{
	return 0;
}

/**
 * kmb_pipe_release - Release a pipeline
 * @pipe: pointer to pipeline object
 */
void kmb_pipe_release(struct kmb_pipeline *pipe)
{ }

/**
 * kmb_pipe_config_dest - Configure pipeline destination information
 * @pipe: pointer to pipeline object
 * @output_id: destination id
 * @channel_cfg: pointer to channel configuration
 */
void kmb_pipe_config_dest(struct kmb_pipeline *pipe, unsigned int output_id,
			  struct kmb_channel_cfg *channel_cfg)
{ }

/**
 * kmb_pipe_config_src - Configure pipeline source information
 * @pipe: pointer to pipeline object
 * @pipe_cfg: pointer to pipeline configuration
 *
 * Configure pipeline source information. Sending source configuration and
 * getting destination restrictions. After this call all destination data is
 * initialized. Changing state to CONFIGURED.
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_pipe_config_src(struct kmb_pipeline *pipe,
			struct kmb_pipe_config_evs *pipe_cfg)
{
	return 0;
}
