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

#ifndef _VIVPU_VIDEO_H_
#define _VIVPU_VIDEO_H_
#include <media/v4l2-mem2mem.h>

#include "vivpu.h"

extern const struct v4l2_ioctl_ops vivpu_ioctl_ops;
int vivpu_queue_init(void *priv, struct vb2_queue *src_vq,
		     struct vb2_queue *dst_vq);

void vivpu_set_default_format(struct vivpu_ctx *ctx);
int vivpu_request_validate(struct media_request *req);

#endif /* _VIVPU_VIDEO_H_ */
