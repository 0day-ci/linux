// SPDX-License-Identifier: GPL-2.0+
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
 * Based on the cedrus VPU driver, that is:
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

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-vmalloc.h>

#include "vivpu-video.h"
#include "vivpu.h"

static const u32 vivpu_decoded_formats[] = {
	V4L2_PIX_FMT_NV12,
};

static const struct vivpu_coded_format_desc coded_formats[] = {
	{
	.pixelformat = V4L2_PIX_FMT_AV1_FRAME,
	/* simulated frame sizes for AV1 */
	.frmsize = {
		.min_width = 48,
		.max_width = 4096,
		.step_width = 16,
		.min_height = 48,
		.max_height = 2304,
		.step_height = 16,
	},
	.num_decoded_fmts = ARRAY_SIZE(vivpu_decoded_formats),
	/* simulate that the AV1 coded format decodes to raw NV12 */
	.decoded_fmts = vivpu_decoded_formats,
	}
};

static const struct vivpu_coded_format_desc*
vivpu_find_coded_fmt_desc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(coded_formats); i++) {
		if (coded_formats[i].pixelformat == fourcc)
			return &coded_formats[i];
	}

	return NULL;
}

void vivpu_set_default_format(struct vivpu_ctx *ctx)
{
	struct v4l2_format src_fmt = {
		.fmt.pix = {
			.width = vivpu_src_default_w,
			.height = vivpu_src_default_h,
			/* Zero bytes per line for encoded source. */
			.bytesperline = 0,
			/* Choose some minimum size since this can't be 0 */
			.sizeimage = SZ_1K,
		},
	};

	ctx->coded_format_desc = &coded_formats[0];
	ctx->src_fmt = src_fmt;

	v4l2_fill_pixfmt_mp(&ctx->dst_fmt.fmt.pix_mp,
			    V4L2_PIX_FMT_NV12,
			    vivpu_src_default_w, vivpu_src_default_h);

	/* Always apply the frmsize constraint of the coded end. */
	v4l2_apply_frmsize_constraints(&ctx->dst_fmt.fmt.pix.width,
				       &ctx->dst_fmt.fmt.pix.height,
				       &ctx->coded_format_desc->frmsize);

	ctx->src_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ctx->dst_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
}

static const char *q_name(enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return "Output";
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return "Capture";
	default:
		return "Invalid";
	}
}

static struct vivpu_q_data *get_q_data(struct vivpu_ctx *ctx,
				       enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return &ctx->q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return &ctx->q_data[V4L2_M2M_DST];
	default:
		break;
	}
	return NULL;
}

static int vivpu_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	strscpy(cap->driver, VIVPU_NAME, sizeof(cap->driver));
	strscpy(cap->card, VIVPU_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", VIVPU_NAME);

	return 0;
}

static int vivpu_enum_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);

	if (f->index >= ctx->coded_format_desc->num_decoded_fmts)
		return -EINVAL;

	f->pixelformat = ctx->coded_format_desc->decoded_fmts[f->index];
	return 0;
}

static int vivpu_enum_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(coded_formats))
		return -EINVAL;

	f->pixelformat = coded_formats[f->index].pixelformat;
	return 0;
}

static int vivpu_g_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);
	*f = ctx->dst_fmt;

	return 0;
}

static int vivpu_g_fmt_vid_out(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);

	*f = ctx->src_fmt;
	return 0;
}

static int vivpu_try_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);
	const struct vivpu_coded_format_desc *coded_desc;
	unsigned int i;

	coded_desc = ctx->coded_format_desc;
	if (WARN_ON(!coded_desc))
		return -EINVAL;

	for (i = 0; i < coded_desc->num_decoded_fmts; i++) {
		if (coded_desc->decoded_fmts[i] == pix_mp->pixelformat)
			break;
	}

	if (i == coded_desc->num_decoded_fmts)
		return -EINVAL;

	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	pix_mp->field = V4L2_FIELD_NONE;

	return 0;
}

static int vivpu_try_fmt_vid_out(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct vivpu_coded_format_desc *coded_desc;

	coded_desc = vivpu_find_coded_fmt_desc(pix_mp->pixelformat);
	if (!coded_desc)
		return -EINVAL;

	/* apply the (simulated) hw constraints */
	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	pix_mp->field = V4L2_FIELD_NONE;
	/* All coded formats are considered single planar for now. */
	pix_mp->num_planes = 1;

	return 0;
}

static int vivpu_s_fmt_vid_out(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct v4l2_format *cap_fmt = &ctx->dst_fmt;
	const struct vivpu_coded_format_desc *desc;
	struct vb2_queue *peer_vq;
	int ret;

	peer_vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(peer_vq))
		return -EBUSY;

	dprintk(ctx->dev,
		"Current OUTPUT queue format: width %d, height %d, pixfmt %d\n",
		ctx->src_fmt.fmt.pix_mp.width, ctx->src_fmt.fmt.pix_mp.height,
		ctx->src_fmt.fmt.pix_mp.pixelformat);

	dprintk(ctx->dev,
		"Current CAPTURE queue format: width %d, height %d, pixfmt %d\n",
		ctx->dst_fmt.fmt.pix_mp.width, ctx->dst_fmt.fmt.pix_mp.height,
		ctx->dst_fmt.fmt.pix_mp.pixelformat);

	ret = vivpu_try_fmt_vid_out(file, priv, f);
	if (ret) {
		dprintk(ctx->dev,
			"Unsupported format for the OUTPUT queue: %d\n",
			f->fmt.pix_mp.pixelformat);
		return ret;
	}

	desc = vivpu_find_coded_fmt_desc(f->fmt.pix_mp.pixelformat);
	if (!desc) {
		dprintk(ctx->dev,
			"Unsupported format for the OUTPUT queue: %d\n",
			f->fmt.pix_mp.pixelformat);
		return -EINVAL;
	}

	ctx->coded_format_desc = desc;

	ctx->src_fmt = *f;

	v4l2_fill_pixfmt_mp(&ctx->dst_fmt.fmt.pix_mp,
			    ctx->coded_format_desc->decoded_fmts[0],
			    ctx->src_fmt.fmt.pix_mp.width,
			    ctx->src_fmt.fmt.pix_mp.height);
	cap_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	cap_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	cap_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	cap_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	dprintk(ctx->dev,
		"Current OUTPUT queue format: width %d, height %d, pixfmt %d\n",
		ctx->src_fmt.fmt.pix_mp.width, ctx->src_fmt.fmt.pix_mp.height,
		ctx->src_fmt.fmt.pix_mp.pixelformat);

	dprintk(ctx->dev,
		"Current CAPTURE queue format: width %d, height %d, pixfmt %d\n",
		ctx->dst_fmt.fmt.pix_mp.width, ctx->dst_fmt.fmt.pix_mp.height,
		ctx->dst_fmt.fmt.pix_mp.pixelformat);

	return 0;
}

static int vivpu_s_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);
	int ret;

	dprintk(ctx->dev,
		"Current CAPTURE queue format: width %d, height %d, pixfmt %d\n",
		ctx->dst_fmt.fmt.pix_mp.width, ctx->dst_fmt.fmt.pix_mp.height,
		ctx->dst_fmt.fmt.pix_mp.pixelformat);

	ret = vivpu_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	ctx->dst_fmt = *f;

	dprintk(ctx->dev,
		"Current CAPTURE queue format: width %d, height %d, pixfmt %d\n",
		ctx->dst_fmt.fmt.pix_mp.width, ctx->dst_fmt.fmt.pix_mp.height,
		ctx->dst_fmt.fmt.pix_mp.pixelformat);

	return 0;
}

static int vivpu_enum_framesizes(struct file *file, void *priv,
				 struct v4l2_frmsizeenum *fsize)
{
	const struct vivpu_coded_format_desc *fmt;
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);

	if (fsize->index != 0)
		return -EINVAL;

	fmt = vivpu_find_coded_fmt_desc(fsize->pixel_format);
	if (!fmt) {
		dprintk(ctx->dev,
			"Unsupported format for the OUTPUT queue: %d\n",
			fsize->pixel_format);

		return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->frmsize;
	return 0;
}

const struct v4l2_ioctl_ops vivpu_ioctl_ops = {
	.vidioc_querycap		= vivpu_querycap,
	.vidioc_enum_framesizes		= vivpu_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= vivpu_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vivpu_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vivpu_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vivpu_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= vivpu_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= vivpu_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= vivpu_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= vivpu_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_try_decoder_cmd		= v4l2_m2m_ioctl_stateless_try_decoder_cmd,
	.vidioc_decoder_cmd		= v4l2_m2m_ioctl_stateless_decoder_cmd,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int vivpu_queue_setup(struct vb2_queue *vq,
			     unsigned int *nbuffers,
			     unsigned int *nplanes,
			     unsigned int sizes[],
			     struct device *alloc_devs[])
{
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt.fmt.pix;
	else
		pix_fmt = &ctx->dst_fmt.fmt.pix;

	if (*nplanes) {
		if (sizes[0] < pix_fmt->sizeimage) {
			v4l2_err(&ctx->dev->v4l2_dev, "sizes[0] is %d, sizeimage is %d\n",
				 sizes[0], pix_fmt->sizeimage);
			return -EINVAL;
		}
	} else {
		sizes[0] = pix_fmt->sizeimage;
		*nplanes = 1;
	}

	dprintk(ctx->dev, "%s: get %d buffer(s) of size %d each.\n",
		q_name(vq->type), *nbuffers, sizes[0]);

	return 0;
}

static void vivpu_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	dprintk(ctx->dev, "Cleaning up queues\n");
	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			break;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->hdl);
		dprintk(ctx->dev, "Marked request %p as complete\n",
			vbuf->vb2_buf.req_obj.req);

		v4l2_m2m_buf_done(vbuf, state);
		dprintk(ctx->dev,
			"Marked buffer %llu as done, state is %d\n",
			vbuf->vb2_buf.timestamp,
			state);
	}
}

static int vivpu_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static int vivpu_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vq);
	u32 plane_sz = vb2_plane_size(vb, 0);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt.fmt.pix;
	else
		pix_fmt = &ctx->dst_fmt.fmt.pix;

	if (plane_sz < pix_fmt->sizeimage) {
		v4l2_err(&ctx->dev->v4l2_dev, "plane[0] size is %d, sizeimage is %d\n",
			 plane_sz, pix_fmt->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);

	return 0;
}

static int vivpu_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vq);
	struct vivpu_q_data *q_data = get_q_data(ctx, vq->type);
	int ret = 0;

	if (!q_data)
		return -EINVAL;

	q_data->sequence = 0;

	switch (ctx->src_fmt.fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_AV1_FRAME:
		dprintk(ctx->dev, "Pixfmt is AV1F\n");
		ctx->current_codec = VIVPU_CODEC_AV1;
		break;
	default:
		v4l2_err(&ctx->dev->v4l2_dev, "Unsupported src format %d\n",
			 ctx->src_fmt.fmt.pix.pixelformat);
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	vivpu_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void vivpu_stop_streaming(struct vb2_queue *vq)
{
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vq);

	dprintk(ctx->dev, "Stop streaming\n");
	vivpu_queue_cleanup(vq, VB2_BUF_STATE_ERROR);
}

static void vivpu_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void vivpu_buf_request_complete(struct vb2_buffer *vb)
{
	struct vivpu_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->hdl);
}

const struct vb2_ops vivpu_qops = {
	.queue_setup          = vivpu_queue_setup,
	.buf_out_validate     = vivpu_buf_out_validate,
	.buf_prepare          = vivpu_buf_prepare,
	.buf_queue            = vivpu_buf_queue,
	.start_streaming      = vivpu_start_streaming,
	.stop_streaming       = vivpu_stop_streaming,
	.wait_prepare         = vb2_ops_wait_prepare,
	.wait_finish          = vb2_ops_wait_finish,
	.buf_request_complete = vivpu_buf_request_complete,
};

int vivpu_queue_init(void *priv, struct vb2_queue *src_vq,
		     struct vb2_queue *dst_vq)
{
	struct vivpu_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &vivpu_qops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->vb_mutex;
	src_vq->supports_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &vivpu_qops;
	dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->vb_mutex;

	return vb2_queue_init(dst_vq);
}

int vivpu_request_validate(struct media_request *req)
{
	struct media_request_object *obj;
	struct vivpu_ctx *ctx = NULL;
	unsigned int count;

	list_for_each_entry(obj, &req->objects, list) {
		struct vb2_buffer *vb;

		if (vb2_request_object_is_buffer(obj)) {
			vb = container_of(obj, struct vb2_buffer, req_obj);
			ctx = vb2_get_drv_priv(vb->vb2_queue);

			break;
		}
	}

	if (!ctx)
		return -ENOENT;

	count = vb2_request_buffer_cnt(req);
	if (!count) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "No buffer was provided with the request\n");
		return -ENOENT;
	} else if (count > 1) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "More than one buffer was provided with the request\n");
		return -EINVAL;
	}

	return vb2_request_validate(req);
}
