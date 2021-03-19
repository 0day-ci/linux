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

#define KMB_ISP_DRV_NAME	"keembay-camera-isp"

#define KMB_ISP_SINK_PAD_SENSOR	0
#define KMB_ISP_SINK_PAD_CFG	1
#define KMB_ISP_SRC_PAD_VID	2
#define KMB_ISP_PADS_NUM	3

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
 * struct kmb_isp - Keem Bay camera ISP device structure
 * @dev: Pointer to basic device structure
 * @lock: Mutex serilizing access to ISP device
 * @thread: Pointer to worker thread data
 * @xlink_cam: Xlink camera communication handler
 * @msg_phy_addr: ISP channel physical CMA address
 * @msg_vaddr: ISP channel virtual CMA address
 * @cfg_q_lock: Mutex to serialize access to isp cfg bufferss queue
 * @isp_cfgs_queue: Isp cfg buffers queue
 * @isp_streaming: Flag to indicate ISP state
 * @source_streaming: Flag to indicate source state
 * @source_stopped: Completion to wait until VPU source is stopped
 * @subdev: V4L2 sub-device
 * @pads: Array of supported isp pads
 * @active_pad_fmt: Array holding active pad formats
 * @try_pad_fmt: Array holding try pad formats
 * @csi2_config: CSI2 configuration
 * @source_fmt: Pointer to isp source format
 * @sequence: frame sequence number
 */
struct kmb_isp {
	struct device *dev;
	struct mutex lock;
	struct task_struct *thread;

	struct kmb_xlink_cam *xlink_cam;

	dma_addr_t msg_phy_addr;
	void *msg_vaddr;

	struct mutex cfg_q_lock;
	struct list_head isp_cfgs_queue;

	bool isp_streaming;
	bool source_streaming;
	struct completion source_stopped;

	struct v4l2_subdev subdev;
	struct media_pad pads[KMB_ISP_PADS_NUM];

	struct v4l2_subdev_format active_pad_fmt[KMB_ISP_PADS_NUM];

	struct v4l2_subdev_format try_pad_fmt[KMB_ISP_PADS_NUM];

	struct kmb_isp_csi2_config csi2_config;
	const struct kmb_isp_source_format *source_fmt;

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
