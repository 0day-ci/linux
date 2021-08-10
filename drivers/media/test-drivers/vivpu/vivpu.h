/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * A virtual stateless VPU example device for uAPI development purposes.
 *
 * A userspace implementation can use vivpu to run a decoding loop even
 * when no hardware is available or when the kernel uAPI for the codec
 * has not been upstreamed yet. This can reveal bugs at an early stage.
 *
 * Copyright (c) Collabora, Ltd.
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * Based on the vicodec driver, that is:
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * Based on the Cedrus VPU driver, that is:
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#ifndef _VIVPU_H_
#define _VIVPU_H_

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/tpg/v4l2-tpg.h>

#define VIVPU_NAME		"vivpu"
#define VIVPU_M2M_NQUEUES	2

extern const unsigned int vivpu_src_default_w;
extern const unsigned int vivpu_src_default_h;
extern const unsigned int vivpu_src_default_depth;
extern unsigned int vivpu_transtime;

struct vivpu_coded_format_desc {
	u32 pixelformat;
	struct v4l2_frmsize_stepwise frmsize;
	unsigned int num_decoded_fmts;
	const u32 *decoded_fmts;
};

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

extern unsigned int vivpu_debug;
#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, vivpu_debug, &dev->v4l2_dev, "%s: " fmt, __func__, ## arg)

struct vivpu_q_data {
	unsigned int		sequence;
};

struct vivpu_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device	mdev;
#endif

	struct mutex		dev_mutex;

	struct v4l2_m2m_dev	*m2m_dev;
};

enum vivpu_codec {
	VIVPU_CODEC_AV1,
};

struct vivpu_ctx {
	struct v4l2_fh		fh;
	struct vivpu_dev	*dev;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl	**ctrls;

	struct mutex		vb_mutex;

	struct vivpu_q_data	q_data[VIVPU_M2M_NQUEUES];
	enum   vivpu_codec	current_codec;

	const struct vivpu_coded_format_desc *coded_format_desc;

	struct v4l2_format	src_fmt;
	struct v4l2_format	dst_fmt;
};

struct vivpu_control {
	struct v4l2_ctrl_config cfg;
};

static inline struct vivpu_ctx *vivpu_file_to_ctx(struct file *file)
{
	return container_of(file->private_data, struct vivpu_ctx, fh);
}

static inline struct vivpu_ctx *vivpu_v4l2fh_to_ctx(struct v4l2_fh *v4l2_fh)
{
	return container_of(v4l2_fh, struct vivpu_ctx, fh);
}

void *vivpu_find_control_data(struct vivpu_ctx *ctx, u32 id);
struct v4l2_ctrl *vivpu_find_control(struct vivpu_ctx *ctx, u32 id);
u32 vivpu_control_num_elems(struct vivpu_ctx *ctx, u32 id);

#endif /* _VIVPU_H_ */
