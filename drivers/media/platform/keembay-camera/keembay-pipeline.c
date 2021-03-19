// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera pipeline.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/xlink.h>

#include <media/v4l2-device.h>

#include "keembay-cam-xlink.h"
#include "keembay-pipeline.h"
#include "keembay-vpu-cmd.h"

static void kmb_pipe_print_config(struct kmb_pipeline *pipe)
{
	struct kmb_pipe_config_evs *cfg = pipe->pipe_cfg;
	struct device *dev = pipe->dev;
	unsigned int i;

	dev_dbg(dev, "\tpipe_id %u\n", cfg->pipe_id);
	dev_dbg(dev, "\tpipe_type %u\n", cfg->pipe_type);
	dev_dbg(dev, "\tsrc_type %u\n", cfg->src_type);
	dev_dbg(dev, "\tpipe_trans_hub %u\n", cfg->pipe_trans_hub);
	dev_dbg(dev, "\tin_isp_res %ux%u\n",
		cfg->in_isp_res.w, cfg->in_isp_res.h);
	dev_dbg(dev, "\tout_isp_res %ux%u\n",
		cfg->out_isp_res.w, cfg->out_isp_res.h);
	dev_dbg(dev, "\tin_isp_stride %u\n", cfg->in_isp_stride);
	dev_dbg(dev, "\tin_exp_offsets[0] %u\n\tin_exp_offsets[1] %u\n"
		"\tin_exp_offsets[2] %u\n",
		cfg->in_exp_offsets[0], cfg->in_exp_offsets[1],
		cfg->in_exp_offsets[2]);

	for (i = 0; i < PIPE_OUTPUT_ID_MAX; i++) {
		dev_dbg(dev, "\tOUTPUT ID: %d\n", i);
		dev_dbg(dev, "\t\tout_min_res %ux%u\n",
			cfg->out_min_res[i].w, cfg->out_min_res[i].h);
		dev_dbg(dev, "\t\tout_max_res %ux%u\n",
			cfg->out_max_res[i].w, cfg->out_max_res[i].h);
	}

	for (i = 0; i < PIPE_OUTPUT_ID_MAX; i++) {
		dev_dbg(dev, "\tpipe_xlink_chann: %d\n", i);
		dev_dbg(dev, "\t\tid: %u %ux%u\n",
			cfg->pipe_xlink_chann[i].id,
			cfg->pipe_xlink_chann[i].frm_res.w,
			cfg->pipe_xlink_chann[i].frm_res.h);
	}

	dev_dbg(dev, "\tkeep_aspect_ratio %u\n", cfg->keep_aspect_ratio);
	dev_dbg(dev, "\tin_data_width %u\n", cfg->in_data_width);
	dev_dbg(dev, "\tin_data_packed %u\n", cfg->in_data_packed);
	dev_dbg(dev, "\tout_data_width %u\n", cfg->out_data_width);
	dev_dbg(dev, "\tinternal_memory_addr 0x%llx\n",
		cfg->internal_memory_addr);
	dev_dbg(dev, "\tinternal_memory_size %u\n", cfg->internal_memory_size);
}

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
	pipe->pipe_cfg = dma_alloc_coherent(dev,
					    sizeof(*pipe->pipe_cfg),
					    &pipe->pipe_cfg_paddr, 0);
	if (!pipe->pipe_cfg)
		return -ENOMEM;

	mutex_init(&pipe->lock);
	pipe->pending = 0;
	pipe->streaming = 0;
	pipe->state = KMB_PIPE_STATE_UNCONFIGURED;

	pipe->dev = dev;
	pipe->xlink_cam = xlink_cam;

	return 0;
}

/**
 * kmb_pipe_cleanup - Cleanup KMB Pipeline
 * @pipe: pointer to pipeline object
 */
void kmb_pipe_cleanup(struct kmb_pipeline *pipe)
{
	dma_free_coherent(pipe->dev, sizeof(struct kmb_pipe_config_evs),
			  pipe->pipe_cfg, pipe->pipe_cfg_paddr);
}

/**
 * kmb_pipe_request - Request a pipeline
 * @pipe: pointer to pipeline object
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_pipe_request(struct kmb_pipeline *pipe)
{
	int ret;

	ret = kmb_cam_xlink_open_ctrl_channel(pipe->xlink_cam);
	if (ret < 0)
		dev_err(pipe->dev, "Failed to request control channel");

	return ret;
}

/**
 * kmb_pipe_release - Release a pipeline
 * @pipe: pointer to pipeline object
 */
void kmb_pipe_release(struct kmb_pipeline *pipe)
{
	kmb_cam_xlink_close_ctrl_channel(pipe->xlink_cam);
}

/**
 * kmb_pipe_config_dest - Configure pipeline destination information
 * @pipe: pointer to pipeline object
 * @output_id: destination id
 * @channel_cfg: pointer to channel configuration
 */
void kmb_pipe_config_dest(struct kmb_pipeline *pipe, unsigned int output_id,
			  struct kmb_channel_cfg *channel_cfg)
{
	mutex_lock(&pipe->lock);

	channel_cfg->frm_res.w =
		clamp_val(channel_cfg->frm_res.w,
			  pipe->pipe_cfg->out_min_res[output_id].w,
			  pipe->pipe_cfg->out_max_res[output_id].w);

	channel_cfg->frm_res.h =
		clamp_val(channel_cfg->frm_res.h,
			  pipe->pipe_cfg->out_min_res[output_id].h,
			  pipe->pipe_cfg->out_max_res[output_id].h);

	pipe->pipe_cfg->pipe_xlink_chann[output_id] = *channel_cfg;

	mutex_unlock(&pipe->lock);
}

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
	int ret = 0;

	mutex_lock(&pipe->lock);

	switch (pipe->state) {
	case KMB_PIPE_STATE_CONFIGURED:
	case KMB_PIPE_STATE_UNCONFIGURED:
		/* Initialize pipeline configuration and counters */
		pipe->pending = 0;
		pipe->streaming = 0;

		/* Store pipeline configuration */
		*pipe->pipe_cfg = *pipe_cfg;

		/*
		 * For some reason vpu firmware is returning config pipe as
		 * result for config pipe control.
		 */
		ret = kmb_cam_xlink_write_ctrl_msg(pipe->xlink_cam,
						   pipe->pipe_cfg_paddr,
						   KMB_IC_EVENT_TYPE_CONFIG_ISP_PIPE,
						   KMB_IC_EVENT_TYPE_CONFIG_ISP_PIPE);
		if (ret < 0) {
			dev_err(pipe->dev, "Failed to reconfigure pipeline!");
			break;
		}
		kmb_pipe_print_config(pipe);

		pipe->state = KMB_PIPE_STATE_CONFIGURED;
		break;
	case KMB_PIPE_STATE_BUILT:
		dev_err(pipe->dev, "Invalid state transition, already built");
		break;
	default:
		dev_err(pipe->dev,
			"Config pipe in invalid state %d", pipe->state);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&pipe->lock);
	return ret;
}
