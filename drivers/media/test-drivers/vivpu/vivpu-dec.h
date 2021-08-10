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

#ifndef _VIVPU_DEC_H_
#define _VIVPU_DEC_H_

#include "vivpu.h"

struct vivpu_av1_run {
	const struct v4l2_ctrl_av1_sequence *sequence;
	const struct v4l2_ctrl_av1_frame_header *frame_header;
	const struct v4l2_ctrl_av1_tile_group *tile_group;
	const struct v4l2_ctrl_av1_tile_group_entry *tg_entries;
	const struct v4l2_ctrl_av1_tile_list *tile_list;
	const struct v4l2_ctrl_av1_tile_list_entry *tl_entries;
};

struct vivpu_run {
	struct vb2_v4l2_buffer	*src;
	struct vb2_v4l2_buffer	*dst;

	union {
		struct vivpu_av1_run	av1;
	};
};

int vivpu_dec_start(struct vivpu_ctx *ctx);
int vivpu_dec_stop(struct vivpu_ctx *ctx);
int vivpu_job_ready(void *priv);
void vivpu_device_run(void *priv);

#endif /* _VIVPU_DEC_H_ */
