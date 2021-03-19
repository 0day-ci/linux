/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera pipeline.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_PIPELINE_H
#define KEEMBAY_PIPELINE_H

#include "keembay-vpu-pipe.h"

/**
 * enum kmb_pipe_state - KMB pipeline state
 * @KMB_PIPE_STATE_UNCONFIGURED: Pipeline is unconfigured only configure can be
 *                               called in this state
 * @KMB_PIPE_STATE_CONFIGURED: Pipeline is configured. Pipeline can be
 *                             re-configured, build or destroyed
 * @KMB_PIPE_STATE_BUILT: Pipeline is built and ready for streaming.
 *                        Pipeline destroy or start stream can be called
 * @KMB_PIPE_STATE_STREAMING: Pipeline is in streaming state only stop
 *                            stream can be called.
 */
enum kmb_pipe_state {
	KMB_PIPE_STATE_UNCONFIGURED,
	KMB_PIPE_STATE_CONFIGURED,
	KMB_PIPE_STATE_BUILT,
	KMB_PIPE_STATE_STREAMING,
};

/**
 * struct kmb_pipeline - KMB Pipeline
 * @lock: Mutex to serialize access to kmb pipeline object
 * @dev: Pointer to device
 * @media_pipe: Media pipeline
 * @state: Pipeline state
 * @pipe_cfg: VPU pipeline configuration
 * @pipe_cfg_paddr: VPU pipeline configuration physical address
 * @pending: Number of media graph entities expected on streaming
 * @streaming: Number of entities in streaming state
 * @xlink_cam: Pointer to xlink camera communication handler
 */
struct kmb_pipeline {
	struct mutex lock;
	struct device *dev;
	struct media_pipeline media_pipe;

	enum kmb_pipe_state state;

	struct kmb_pipe_config_evs *pipe_cfg;
	dma_addr_t pipe_cfg_paddr;

	unsigned int pending;
	unsigned int streaming;

	struct kmb_xlink_cam *xlink_cam;
};

int kmb_pipe_init(struct kmb_pipeline *pipe,
		  struct device *dev,
		  struct kmb_xlink_cam *xlink_cam);
void kmb_pipe_cleanup(struct kmb_pipeline *pipe);

int kmb_pipe_request(struct kmb_pipeline *pipe);
void kmb_pipe_release(struct kmb_pipeline *pipe);

void kmb_pipe_config_dest(struct kmb_pipeline *pipe, unsigned int output_id,
			  struct kmb_channel_cfg *channel_cfg);
int kmb_pipe_config_src(struct kmb_pipeline *pipe,
			struct kmb_pipe_config_evs *pipe_cfg);

int kmb_pipe_prepare(struct kmb_pipeline *pipe);
int kmb_pipe_run(struct kmb_pipeline *pipe, struct media_entity *entity);
void kmb_pipe_stop(struct kmb_pipeline *pipe, struct media_entity *entity);

#endif /* KEEMBAY_PIPELINE_H */
