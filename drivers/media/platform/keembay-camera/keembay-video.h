/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera video node.
 *
 * Copyright (C) 2020 Intel Corporation
 */
#ifndef KEEMBAY_VIDEO_H
#define KEEMBAY_VIDEO_H

#include <media/v4l2-dev.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#include "keembay-cam-xlink.h"

/**
 * struct kmb_frame_buffer - KMB frame buffer structure
 * @vb: Video buffer for v4l2
 * @addr: Array of dma buffer plane address
 * @list: Frame buffer list
 */
struct kmb_frame_buffer {
	struct vb2_v4l2_buffer vb;
	dma_addr_t addr[3];
	struct list_head list;
};

/**
 * struct kmb_video - KMB Video device structure
 * @lock: Mutex serializing kmb video device ops
 * @video_lock: Mutex serializing video operations
 * @video: Pointer to V4L2 sub-device
 * @pad: Media pad graph objects
 * @dma_dev: Pointer to dma device
 * @pipe: Pointer to kmb media pipeline
 * @chan: Pointer to xlink channel
 */
struct kmb_video {
	struct mutex lock; /* Lock protecting kmb video device */
	struct mutex video_lock; /* Lock serializing video device operations */
	struct video_device *video;
	struct media_pad pad;
	struct device *dma_dev;
	struct kmb_pipeline *pipe;
	struct kmb_xlink_cam *xlink_cam;
	unsigned int chan_id;
};

/**
 * struct kmb_video_fh - KMB video file handler
 * @fh: V4L2 file handler
 * @kmb_vid: Pointer to KMB video device
 * @lock: Mutex serializing access to fh
 * @vb2_lock: Mutex serializing access to vb2 queue
 * @vb2_q: Video buffer queue
 * @active_fmt: Active format
     @pix: Mplane active pixel format
     @info: Active kmb format info
 * @contiguous_memory: Flag to enable contiguous memory allocation
 * @dma_queue: DMA buffers queue
 * @thread: Pointer to worker thread data
 */
struct kmb_video_fh {
	struct v4l2_fh fh;
	struct kmb_video *kmb_vid;
	struct mutex lock; /* Lock protecting fh operations */
	struct mutex vb2_lock; /* Lock protecting video buffer queue */
	struct vb2_queue vb2_q;
	struct {
		struct v4l2_pix_format_mplane pix;
		const struct kmb_video_fmt_info *info;
	} active_fmt;
	bool contiguous_memory;
	struct list_head dma_queue;
	struct task_struct *thread;
};

int kmb_video_init(struct kmb_video *kmb_vid, const char *name);
void kmb_video_cleanup(struct kmb_video *kmb_vid);

int kmb_video_register(struct kmb_video *kmb_vid,
		       struct v4l2_device *v4l2_dev);
void kmb_video_unregister(struct kmb_video *kmb_vid);

#endif /* KEEMBAY_VIDEO_H */
