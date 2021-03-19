/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera ISP driver.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_ISP_H
#define KEEMBAY_ISP_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "keembay-metadata.h"
#include "keembay-pipeline.h"
#include "keembay-video.h"

#define KMB_ISP_SINK_PAD_SENSOR	0
#define KMB_ISP_SINK_PAD_PARAM	1
#define KMB_ISP_SRC_PAD_STATS	2
#define KMB_ISP_SRC_PAD_VID	3
#define KMB_ISP_PADS_NUM	4

#define KMB_ISP_MAX_DEST_FMTS	5

/**
 * struct kmb_isp_csi2_config - Isp csi2 configuration
 * @rx_id: Source port id
 * @num_lanes: Number of physical lanes
 */
struct kmb_isp_csi2_config {
	u32 rx_id;
	u32 num_lanes;
};

/**
 * struct kmb_isp_source_format - Isp source format
 * @code: V4l2 media bus code for the format
 * @bayer_pattern: Bayer format
 * @bpp: Bits per pixel
 * @rx_data_type: Receiver data type
 * @dest_fmts: Supported destination formats
 */
struct kmb_isp_source_format {
	u32 code;
	u32 bayer_pattern;
	u32 bpp;
	enum kmb_ic_mipi_rx_data_type rx_data_type;
	u32 dest_fmts[KMB_ISP_MAX_DEST_FMTS];
};

/**
 * struct kmb_isp - Keem Bay camera ISP device structure
 * @dev: Pointer to basic device structure
 * @lock: Mutex serilizing access to ISP device
 * @thread: Pointer to worker thread data
 * @xlink_cam: Pointer to xlink camera communication handler
 * @msg_phy_addr: ISP channel physical CMA address
 * @msg_vaddr: ISP channel virtual CMA address
 * @meta_q_lock: Mutex to protect metadata buffers queues
 * @meta_params_pending_q: Metadata params pending queue
 * @meta_params_process_q: Metadata params processing queue
 * @meta_stats_pending_q: Metadata statistics pending queue
 * @meta_stats_process_q: Metadata statistics processing queue
 * @isp_streaming: Flag to indicate ISP state
 * @source_streaming: Flag to indicate source state
 * @source_stopped: Completion to wait until VPU source is stopped
 * @subdev: V4L2 sub-device
 * @pads: Array of supported isp pads
 * @active_pipe: VPU pipeline instance used for active format and streaming
 * @active_pad_fmt: Array holding active pad formats
 * @try_pipe: VPU pipeline instance used for try format
 * @try_pad_fmt: Array holding try pad formats
 * @csi2_config: CSI2 configuration
 * @source_fmt: Pointer to isp source format
 * @pipe_cfg: VPU pipeline configuration structure
 * @config_chan_id: Isp config channel id
 * @stats: Statistics video node
 * @params: Params video node
 * @capture_chan_id: Capture xlink channel id
 * @capture: Isp capture video node
 * @sequence: Frame sequence number
 */
struct kmb_isp {
	struct device *dev;
	struct mutex lock;
	struct task_struct *thread;

	struct kmb_xlink_cam *xlink_cam;

	dma_addr_t msg_phy_addr;
	void *msg_vaddr;

	struct mutex meta_q_lock;
	struct list_head meta_params_pending_q;
	struct list_head meta_params_process_q;
	struct list_head meta_stats_pending_q;
	struct list_head meta_stats_process_q;

	bool isp_streaming;
	bool source_streaming;
	struct completion source_stopped;

	struct v4l2_subdev subdev;
	struct media_pad pads[KMB_ISP_PADS_NUM];

	struct kmb_pipeline active_pipe;
	struct v4l2_subdev_format active_pad_fmt[KMB_ISP_PADS_NUM];

	struct kmb_pipeline try_pipe;
	struct v4l2_subdev_format try_pad_fmt[KMB_ISP_PADS_NUM];

	struct kmb_isp_csi2_config csi2_config;
	const struct kmb_isp_source_format *source_fmt;

	struct kmb_pipe_config_evs pipe_cfg;

	unsigned int config_chan_id;
	struct kmb_metadata stats;
	struct kmb_metadata params;

	unsigned int capture_chan_id;
	struct kmb_video capture;

	u32 sequence;
};

int kmb_isp_init(struct kmb_isp *kmb_isp, struct device *dev,
		 struct kmb_isp_csi2_config *csi2_config,
		 struct kmb_xlink_cam *xlink_cam);
void kmb_isp_cleanup(struct kmb_isp *kmb_isp);

int kmb_isp_register_entities(struct kmb_isp *kmb_isp,
			      struct v4l2_device *v4l2_dev);
void kmb_isp_unregister_entities(struct kmb_isp *kmb_isp);

#endif /* KEEMBAY_ISP_H */
