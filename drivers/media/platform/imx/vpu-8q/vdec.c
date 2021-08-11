// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#define TAG		"DEC"
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/imx_vpu.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_core.h"
#include "vpu_helpers.h"
#include "vpu_v4l2.h"
#include "vpu_cmds.h"
#include "vpu_rpc.h"
#include "vpu_log.h"

#define VDEC_FRAME_DEPTH		256

struct vdec_fs_info {
	char name[8];
	u32 type;
	u32 max_count;
	u32 req_count;
	u32 count;
	u32 index;
	u32 size;
	struct vpu_buffer buffer[32];
	u32 tag;
};

struct vdec_t {
	u32 seq_hdr_found;
	struct vpu_buffer udata;
	struct vpu_decode_params params;
	struct vpu_dec_codec_info codec_info;
	enum vpu_codec_state state;

	struct vpu_vb2_buffer *slots[VB2_MAX_FRAME];
	u32 req_frame_count;
	struct vdec_fs_info mbi;
	struct vdec_fs_info dcp;
	u32 seq_tag;

	u32 decoded_frame_count;
	u32 display_frame_count;
	u32 eos_received;
	u32 eos_subscribed;
	bool is_source_changed;
	u32 source_change;
	u32 drain;
	u32 ts_pre_count;
	u32 frame_depth;
	s64 ts_start;
	s64 ts_input;
	s64 timestamp;
};

static const struct vpu_format vdec_formats[] = {
	{
		.pixfmt = V4L2_PIX_FMT_NV12_8L128,
		.num_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	{
		.pixfmt = V4L2_PIX_FMT_NV12_10BE_8L128,
		.num_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	{
		.pixfmt = V4L2_PIX_FMT_H264,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_H264_MVC,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_VC1_ANNEX_G,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_VC1_ANNEX_L,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_MPEG2,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_XVID,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_VP8,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{
		.pixfmt = V4L2_PIX_FMT_H263,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION
	},
	{0, 0, 0, 0},
};

static const struct v4l2_ctrl_ops vdec_ctrl_ops = {
	.g_volatile_ctrl = vpu_helper_g_volatile_ctrl,
};

static int vdec_ctrl_init(struct vpu_inst *inst)
{
	struct v4l2_ctrl *ctrl;
	int ret;

	ret = v4l2_ctrl_handler_init(&inst->ctrl_handler, 20);
	if (ret)
		return ret;

	ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,
			V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 2);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,
			V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 32, 1, 2);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	ret = v4l2_ctrl_handler_setup(&inst->ctrl_handler);
	if (ret) {
		inst_err(inst, "setup ctrls fail, ret = %d\n", ret);
		v4l2_ctrl_handler_free(&inst->ctrl_handler);
		return ret;
	}

	return 0;
}

static int vdec_handle_eos(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_queue *dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);

	if (!vdec->eos_received)
		return 0;

	if (!vdec->eos_subscribed)
		return 0;

	if (!list_empty(&dst_q->done_list))
		return 0;

	dst_q->last_buffer_dequeued = true;
	vpu_notify_eos(inst);
	vdec->eos_received--;

	return 0;
}

static void vdec_handle_resolution_change(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_queue *q;

	if (inst->state != VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		return;
	if (!vdec->source_change)
		return;

	q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	if (!list_empty(&q->done_list))
		return;

	vdec->source_change--;
	vpu_notify_source_change(inst);
}

static int vdec_update_state(struct vpu_inst *inst,
		enum vpu_codec_state state, u32 force)
{
	struct vdec_t *vdec = inst->priv;
	enum vpu_codec_state pre_state = inst->state;

	if (state == VPU_CODEC_STATE_SEEK) {
		if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
			vdec->state = inst->state;
		else
			vdec->state = VPU_CODEC_STATE_ACTIVE;
	}
	if (inst->state != VPU_CODEC_STATE_SEEK || force)
		inst->state = state;
	else if (state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		vdec->state = VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE;

	inst_dbg(inst, LVL_FLOW, "state : %d -> %d\n", pre_state, inst->state);

	if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		vdec_handle_resolution_change(inst);

	return 0;
}

static int vdec_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "vpu B0", sizeof(cap->driver));
	strscpy(cap->card, "imx vpu decoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform: imx8q-vpu", sizeof(cap->bus_info));

	return 0;
}

static int vdec_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec = inst->priv;
	const struct vpu_format *fmt;
	int ret = -EINVAL;

	vpu_inst_lock(inst);
	if (!V4L2_TYPE_IS_OUTPUT(f->type) && vdec->seq_hdr_found) {
		if (f->index == 0) {
			f->pixelformat = inst->cap_format.pixfmt;
			f->flags = inst->cap_format.flags;
			ret = 0;
		}
	} else {
		fmt = vpu_helper_enum_format(inst, f->type, f->index);
		memset(f->reserved, 0, sizeof(f->reserved));
		if (!fmt)
			goto exit;

		f->pixelformat = fmt->pixfmt;
		f->flags = fmt->flags;
		ret = 0;
	}

exit:
	vpu_inst_unlock(inst);
	return ret;
}

static int vdec_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec = inst->priv;
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct vpu_format *cur_fmt;
	int i;

	cur_fmt = vpu_get_format(inst, f->type);

	pixmp->pixelformat = cur_fmt->pixfmt;
	pixmp->num_planes = cur_fmt->num_planes;
	pixmp->width = cur_fmt->width;
	pixmp->height = cur_fmt->height;
	pixmp->field = cur_fmt->field;
	pixmp->flags = cur_fmt->flags;
	for (i = 0; i < pixmp->num_planes; i++) {
		pixmp->plane_fmt[i].bytesperline = cur_fmt->bytesperline[i];
		pixmp->plane_fmt[i].sizeimage = cur_fmt->sizeimage[i];
	}

	f->fmt.pix_mp.colorspace = vdec->codec_info.color_primaries;
	f->fmt.pix_mp.xfer_func = vdec->codec_info.transfer_chars;
	f->fmt.pix_mp.ycbcr_enc = vdec->codec_info.matrix_coeffs;
	f->fmt.pix_mp.quantization = vdec->codec_info.full_range;

	inst_dbg(inst, LVL_DEBUG, "%s g_fmt: %c%c%c%c %dx%d %d,%d, %d,%d\n",
			vpu_type_name(f->type),
			f->fmt.pix_mp.pixelformat,
			f->fmt.pix_mp.pixelformat >> 8,
			f->fmt.pix_mp.pixelformat >> 16,
			f->fmt.pix_mp.pixelformat >> 24,
			f->fmt.pix_mp.width,
			f->fmt.pix_mp.height,
			pixmp->plane_fmt[0].sizeimage,
			pixmp->plane_fmt[1].sizeimage,
			pixmp->plane_fmt[0].bytesperline,
			pixmp->plane_fmt[1].bytesperline);

	return 0;
}

static int vdec_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec = inst->priv;

	vpu_try_fmt_common(inst, f);

	vpu_inst_lock(inst);
	if (vdec->seq_hdr_found) {
		f->fmt.pix_mp.colorspace = vdec->codec_info.color_primaries;
		f->fmt.pix_mp.xfer_func = vdec->codec_info.transfer_chars;
		f->fmt.pix_mp.ycbcr_enc = vdec->codec_info.matrix_coeffs;
		f->fmt.pix_mp.quantization = vdec->codec_info.full_range;
	} else {
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
		f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
		f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	}
	vpu_inst_unlock(inst);

	return 0;
}

static int vdec_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	const struct vpu_format *fmt;
	struct vpu_format *cur_fmt;
	struct vb2_queue *q;
	struct vdec_t *vdec;
	int i;

	vdec = inst->priv;
	q = v4l2_m2m_get_vq(inst->m2m_ctx, f->type);
	if (!q)
		return -EINVAL;
	if (vb2_is_streaming(q))
		return -EBUSY;

	fmt = vpu_try_fmt_common(inst, f);
	if (!fmt)
		return -EINVAL;

	cur_fmt = vpu_get_format(inst, f->type);

	vpu_inst_lock(inst);
	cur_fmt->pixfmt = fmt->pixfmt;
	cur_fmt->type = fmt->type;
	if (V4L2_TYPE_IS_OUTPUT(f->type) || vdec->seq_hdr_found <= 0) {
		cur_fmt->num_planes = fmt->num_planes;
		cur_fmt->flags = fmt->flags;
		cur_fmt->width = pixmp->width;
		cur_fmt->height = pixmp->height;
		for (i = 0; i < fmt->num_planes; i++) {
			cur_fmt->sizeimage[i] = pixmp->plane_fmt[i].sizeimage;
			cur_fmt->bytesperline[i] = pixmp->plane_fmt[i].bytesperline;
		}
		if (pixmp->field != V4L2_FIELD_ANY)
			cur_fmt->field = pixmp->field;
	} else {
		pixmp->num_planes = cur_fmt->num_planes;
		pixmp->width = cur_fmt->width;
		pixmp->height = cur_fmt->height;
		for (i = 0; i < pixmp->num_planes; i++) {
			pixmp->plane_fmt[i].bytesperline = cur_fmt->bytesperline[i];
			pixmp->plane_fmt[i].sizeimage = cur_fmt->sizeimage[i];
		}
		pixmp->field = cur_fmt->field;
	}
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		vdec->params.codec_format = cur_fmt->pixfmt;
	else
		vdec->params.output_format = cur_fmt->pixfmt;

	if (!vdec->seq_hdr_found) {
		vdec->codec_info.color_primaries = f->fmt.pix_mp.colorspace;
		vdec->codec_info.transfer_chars = f->fmt.pix_mp.xfer_func;
		vdec->codec_info.matrix_coeffs = f->fmt.pix_mp.ycbcr_enc;
		vdec->codec_info.full_range = f->fmt.pix_mp.quantization;

		inst->crop.left = 0;
		inst->crop.top = 0;
		inst->crop.width = cur_fmt->width;
		inst->crop.height = cur_fmt->height;
	} else {
		f->fmt.pix_mp.colorspace = vdec->codec_info.color_primaries;
		f->fmt.pix_mp.xfer_func = vdec->codec_info.transfer_chars;
		f->fmt.pix_mp.ycbcr_enc = vdec->codec_info.matrix_coeffs;
		f->fmt.pix_mp.quantization = vdec->codec_info.full_range;
	}

	vpu_inst_unlock(inst);

	inst_dbg(inst, LVL_FLOW, "%s s_fmt: %c%c%c%c %dx%d %u,%u, %u,%u\n",
			vpu_type_name(f->type),
			f->fmt.pix_mp.pixelformat,
			f->fmt.pix_mp.pixelformat >> 8,
			f->fmt.pix_mp.pixelformat >> 16,
			f->fmt.pix_mp.pixelformat >> 24,
			f->fmt.pix_mp.width,
			f->fmt.pix_mp.height,
			pixmp->plane_fmt[0].sizeimage,
			pixmp->plane_fmt[1].sizeimage,
			pixmp->plane_fmt[0].bytesperline,
			pixmp->plane_fmt[1].bytesperline);

	return 0;
}

static int vdec_g_selection(struct file *file, void *fh,
					struct v4l2_selection *s)
{
	struct vpu_inst *inst = to_inst(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r = inst->crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = inst->cap_format.width;
		s->r.height = inst->cap_format.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vdec_drain(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	if (!vdec->drain)
		return 0;

	if (v4l2_m2m_num_src_bufs_ready(inst->m2m_ctx))
		return 0;

	if (!vdec->params.frame_count) {
		vpu_notify_eos(inst);
		return 0;
	}

	vpu_iface_add_scode(inst, SCODE_PADDING_EOS);
	vdec->params.end_flag = 1;
	vpu_iface_set_decode_params(inst, &vdec->params, 1);
	vdec->drain = 0;
	inst_dbg(inst, LVL_FLOW, "append eos\n");

	return 0;
}

static int vdec_cmd_stop(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	inst_dbg(inst, LVL_FLOW, "stop cmd\n");

	if (inst->state == VPU_CODEC_STATE_DEINIT) {
		vpu_notify_eos(inst);
	} else {
		vdec->drain = 1;
		vdec_drain(inst);
	}

	return 0;
}

static int vdec_decoder_cmd(struct file *file,
		void *fh,
		struct v4l2_decoder_cmd *cmd)
{
	struct vpu_inst *inst = to_inst(file);
	int ret;

	ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);
	if (ret)
		return ret;

	vpu_inst_lock(inst);
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
		vdec_cmd_stop(inst);
		break;
	default:
		break;
	}
	vpu_inst_unlock(inst);

	return 0;
}

static int vdec_subscribe_event(struct v4l2_fh *fh,
		const struct v4l2_event_subscription *sub)
{
	struct vpu_inst *inst = container_of(fh, struct vpu_inst, fh);
	struct vdec_t *vdec = inst->priv;

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		vdec->eos_subscribed = true;
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	case V4L2_EVENT_SKIP:
	case V4L2_EVENT_CODEC_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return -EINVAL;
	}

	return 0;
}

static int vdec_unsubscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	struct vpu_inst *inst = container_of(fh, struct vpu_inst, fh);
	struct vdec_t *vdec = inst->priv;
	int ret;

	ret = v4l2_event_unsubscribe(fh, sub);
	if (ret)
		return ret;

	if (sub->type == V4L2_EVENT_EOS)
		vdec->eos_subscribed = false;

	return 0;
}

static const struct v4l2_ioctl_ops vdec_ioctl_ops = {
	.vidioc_querycap               = vdec_querycap,
	.vidioc_enum_fmt_vid_cap       = vdec_enum_fmt,
	.vidioc_enum_fmt_vid_out       = vdec_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane   = vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane   = vdec_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = vdec_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = vdec_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane   = vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane   = vdec_s_fmt,
	.vidioc_g_selection            = vdec_g_selection,
	.vidioc_try_decoder_cmd        = v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_decoder_cmd            = vdec_decoder_cmd,
	.vidioc_subscribe_event        = vdec_subscribe_event,
	.vidioc_unsubscribe_event      = vdec_unsubscribe_event,
	.vidioc_reqbufs                = v4l2_m2m_ioctl_reqbufs,
	.vidioc_create_bufs	       = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf	       = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_querybuf               = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf                   = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf                 = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf                  = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon               = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff              = v4l2_m2m_ioctl_streamoff,
};

static bool vdec_check_ready(struct vpu_inst *inst, unsigned int type)
{
	struct vdec_t *vdec = inst->priv;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (vdec->ts_pre_count >= vdec->frame_depth)
			return false;
		return true;
	}

	if (vdec->req_frame_count || vdec->eos_received)
		return true;

	return false;
}

static int vdec_frame_decoded(struct vpu_inst *inst, void *arg)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_dec_pic_info *info = arg;
	struct vpu_vb2_buffer *vpu_buf;
	int ret = 0;

	if (!info || info->id >= ARRAY_SIZE(vdec->slots))
		return -EINVAL;

	vpu_inst_lock(inst);
	vpu_buf = vdec->slots[info->id];
	if (!vpu_buf) {
		inst_err(inst, "decoded invalid frame[%d]\n", info->id);
		ret = -EINVAL;
		goto exit;
	}
	if (vpu_buf->state == VPU_BUF_STATE_DECODED)
		inst_dbg(inst, LVL_INFO, "buf[%d] has been decoded\n", info->id);
	vpu_buf->state = VPU_BUF_STATE_DECODED;
	vdec->decoded_frame_count++;
	if (vdec->ts_pre_count >= info->consumed_count)
		vdec->ts_pre_count -= info->consumed_count;
	else
		vdec->ts_pre_count = 0;
exit:
	vpu_inst_unlock(inst);

	return ret;
}

static struct vpu_vb2_buffer *vdec_find_buffer(struct vpu_inst *inst, u32 luma)
{
	struct vdec_t *vdec = inst->priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(vdec->slots); i++) {
		if (!vdec->slots[i])
			continue;
		if (luma == vdec->slots[i]->luma)
			return vdec->slots[i];
	}

	return NULL;
}

static void vdec_buf_done(struct vpu_inst *inst, struct vpu_frame_info *frame)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;

	if (!frame)
		return;

	vpu_inst_lock(inst);
	vpu_buf = vdec_find_buffer(inst, frame->luma);
	vpu_inst_unlock(inst);
	if (!vpu_buf) {
		inst_err(inst, "can't find buffer, id = %d, addr = 0x%x\n",
				frame->id, frame->luma);
		return;
	}
	if (frame->skipped) {
		inst_dbg(inst, LVL_DEBUG, "frame skip\n");
		vpu_notify_skip(inst);
		return;
	}

	vbuf = &vpu_buf->m2m_buf.vb;
	if (vbuf->vb2_buf.index != frame->id)
		inst_err(inst, "buffer id(%d, %d) dismatch\n",
				vbuf->vb2_buf.index, frame->id);

	if (vpu_buf->state != VPU_BUF_STATE_DECODED)
		inst_err(inst, "buffer(%d) ready without decoded\n", frame->id);
	vpu_buf->state = VPU_BUF_STATE_READY;
	vb2_set_plane_payload(&vbuf->vb2_buf, 0, inst->cap_format.sizeimage[0]);
	vb2_set_plane_payload(&vbuf->vb2_buf, 1, inst->cap_format.sizeimage[1]);
	vbuf->vb2_buf.timestamp = frame->timestamp;
	vbuf->field = inst->cap_format.field;
	inst_dbg(inst, LVL_TS, "[OUTPUT TS]%32lld\n", frame->timestamp);

	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
	vpu_inst_lock(inst);
	vdec->timestamp = frame->timestamp;
	vdec->display_frame_count++;
	vpu_inst_unlock(inst);
	inst_dbg(inst, LVL_DEBUG, "decoded : %d, display : %d\n",
			vdec->decoded_frame_count, vdec->display_frame_count);
}

static void vdec_stop_done(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vpu_inst_lock(inst);
	vdec_update_state(inst, VPU_CODEC_STATE_DEINIT, 0);
	vdec->seq_hdr_found = 0;
	vpu_inst_unlock(inst);
}

static bool vdec_check_source_change(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	const struct vpu_format *fmt;
	int i;

	fmt = vpu_helper_find_format(inst, inst->cap_format.type, vdec->codec_info.pixfmt);
	if (inst->cap_format.pixfmt != vdec->codec_info.pixfmt)
		return true;
	if (inst->cap_format.width != vdec->codec_info.decoded_width)
		return true;
	if (inst->cap_format.height != vdec->codec_info.decoded_height)
		return true;
	if (vpu_get_num_buffers(inst, inst->cap_format.type) < inst->min_buffer_cap)
		return true;
	if (inst->crop.left != vdec->codec_info.offset_x)
		return true;
	if (inst->crop.top != vdec->codec_info.offset_y)
		return true;
	if (inst->crop.width != vdec->codec_info.width)
		return true;
	if (inst->crop.height != vdec->codec_info.height)
		return true;
	if (fmt && inst->cap_format.num_planes != fmt->num_planes)
		return true;
	for (i = 0; i < inst->cap_format.num_planes; i++) {
		if (inst->cap_format.bytesperline[i] != vdec->codec_info.bytesperline[i])
			return true;
		if (inst->cap_format.sizeimage[i] != vdec->codec_info.sizeimage[i])
			return true;
	}

	return false;
}

static void vdec_init_fmt(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	const struct vpu_format *fmt;
	int i;

	fmt = vpu_helper_find_format(inst, inst->cap_format.type, vdec->codec_info.pixfmt);
	inst->out_format.width = vdec->codec_info.width;
	inst->out_format.height = vdec->codec_info.height;
	inst->cap_format.width = vdec->codec_info.decoded_width;
	inst->cap_format.height = vdec->codec_info.decoded_height;
	inst->cap_format.pixfmt = vdec->codec_info.pixfmt;
	if (fmt) {
		inst->cap_format.num_planes = fmt->num_planes;
		inst->cap_format.flags = fmt->flags;
	}
	for (i = 0; i < inst->cap_format.num_planes; i++) {
		inst->cap_format.bytesperline[i] = vdec->codec_info.bytesperline[i];
		inst->cap_format.sizeimage[i] = vdec->codec_info.sizeimage[i];
	}
	if (vdec->codec_info.progressive)
		inst->cap_format.field = V4L2_FIELD_NONE;
	else
		inst->cap_format.field = V4L2_FIELD_INTERLACED;
	if (vdec->codec_info.color_primaries == V4L2_COLORSPACE_DEFAULT)
		vdec->codec_info.color_primaries = V4L2_COLORSPACE_REC709;
	if (vdec->codec_info.transfer_chars == V4L2_XFER_FUNC_DEFAULT)
		vdec->codec_info.transfer_chars = V4L2_XFER_FUNC_709;
	if (vdec->codec_info.matrix_coeffs == V4L2_YCBCR_ENC_DEFAULT)
		vdec->codec_info.matrix_coeffs = V4L2_YCBCR_ENC_709;
	if (vdec->codec_info.full_range == V4L2_QUANTIZATION_DEFAULT)
		vdec->codec_info.full_range = V4L2_QUANTIZATION_LIM_RANGE;
}

static void vdec_init_crop(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	inst->crop.left = vdec->codec_info.offset_x;
	inst->crop.top = vdec->codec_info.offset_y;
	inst->crop.width = vdec->codec_info.width;
	inst->crop.height = vdec->codec_info.height;
}

static void vdec_init_mbi(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vdec->mbi.size = vdec->codec_info.mbi_size;
	vdec->mbi.max_count = ARRAY_SIZE(vdec->mbi.buffer);
	scnprintf(vdec->mbi.name, sizeof(vdec->mbi.name), "mbi");
	vdec->mbi.type = MEM_RES_MBI;
	vdec->mbi.tag = vdec->seq_tag;
}

static void vdec_init_dcp(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vdec->dcp.size = vdec->codec_info.dcp_size;
	vdec->dcp.max_count = ARRAY_SIZE(vdec->dcp.buffer);
	scnprintf(vdec->dcp.name, sizeof(vdec->dcp.name), "dcp");
	vdec->dcp.type = MEM_RES_DCP;
	vdec->dcp.tag = vdec->seq_tag;
}

static void vdec_request_one_fs(struct vdec_fs_info *fs)
{
	WARN_ON(!fs);

	fs->req_count++;
	if (fs->req_count > fs->max_count) {
		vpu_err("error:request %s over %d\n", fs->name, fs->max_count);
		fs->req_count = fs->max_count;
	}
}

static int vdec_alloc_fs_buffer(struct vpu_inst *inst, struct vdec_fs_info *fs)
{
	struct vpu_buffer *buffer;

	if (!inst || !fs || !fs->size)
		return -EINVAL;

	if (fs->count >= fs->req_count)
		return -EINVAL;

	buffer = &fs->buffer[fs->count];
	if (buffer->virt && buffer->length >= fs->size)
		return 0;

	vpu_free_dma(buffer);
	buffer->length = fs->size;
	return vpu_alloc_dma(inst->core, buffer);
}

static void vdec_alloc_fs(struct vpu_inst *inst, struct vdec_fs_info *fs)
{
	int ret;

	while (fs->count < fs->req_count) {
		ret = vdec_alloc_fs_buffer(inst, fs);
		if (ret)
			break;
		fs->count++;
	}
}

static void vdec_clear_fs(struct vdec_fs_info *fs)
{
	u32 i;

	if (!fs)
		return;

	for (i = 0; i < ARRAY_SIZE(fs->buffer); i++)
		vpu_free_dma(&fs->buffer[i]);
	memset(fs, 0, sizeof(*fs));
}

static int vdec_response_fs(struct vpu_inst *inst, struct vdec_fs_info *fs)
{
	struct vpu_fs_info info;
	int ret;

	if (fs->index >= fs->count)
		return 0;

	memset(&info, 0, sizeof(info));
	info.id = fs->index;
	info.type = fs->type;
	info.tag = fs->tag;
	info.luma_addr = fs->buffer[fs->index].phys;
	info.luma_size = fs->buffer[fs->index].length;
	ret = vpu_session_alloc_fs(inst, &info);
	if (ret)
		return ret;

	fs->index++;
	return 0;
}

static int vdec_response_frame_abnormal(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_fs_info info;

	if (!vdec->req_frame_count)
		return 0;

	memset(&info, 0, sizeof(info));
	info.type = MEM_RES_FRAME;
	info.tag = vdec->seq_tag + 0xf0;
	vpu_session_alloc_fs(inst, &info);
	vdec->req_frame_count--;

	return 0;
}

static int vdec_response_frame(struct vpu_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vpu_fs_info info;
	int ret;

	if (inst->state != VPU_CODEC_STATE_ACTIVE)
		return -EINVAL;

	if (!vdec->req_frame_count)
		return -EINVAL;

	if (!vbuf)
		return -EINVAL;

	if (vdec->slots[vbuf->vb2_buf.index]) {
		inst_err(inst, "repeat alloc fs %d\n", vbuf->vb2_buf.index);
		return -EINVAL;
	}

	inst_dbg(inst, LVL_DEBUG, "inst->state = %d, alloc fs %d, tag = 0x%x\n",
		inst->state, vbuf->vb2_buf.index, vdec->seq_tag);
	vpu_buf = to_vpu_vb2_buffer(vbuf);

	memset(&info, 0, sizeof(info));
	info.id = vbuf->vb2_buf.index;
	info.type = MEM_RES_FRAME;
	info.tag = vdec->seq_tag;
	info.luma_addr = vpu_get_vb_phy_addr(&vbuf->vb2_buf, 0);
	info.luma_size = inst->cap_format.sizeimage[0];
	info.chroma_addr = vpu_get_vb_phy_addr(&vbuf->vb2_buf, 1);
	info.chromau_size = inst->cap_format.sizeimage[1];
	info.bytesperline = inst->cap_format.bytesperline[0];
	ret = vpu_session_alloc_fs(inst, &info);
	if (ret)
		return ret;

	vpu_buf->tag = info.tag;
	vpu_buf->luma = info.luma_addr;
	vpu_buf->chroma_u = info.chromau_size;
	vpu_buf->chroma_v = 0;
	vpu_buf->state = VPU_BUF_STATE_INUSE;
	vdec->slots[info.id] = vpu_buf;
	vdec->req_frame_count--;

	return 0;
}

static void vdec_response_fs_request(struct vpu_inst *inst, bool force)
{
	struct vdec_t *vdec = inst->priv;
	int i;
	int ret;

	if (force) {
		for (i = vdec->req_frame_count; i > 0; i--)
			vdec_response_frame_abnormal(inst);
		return;
	}

	for (i = vdec->req_frame_count; i > 0; i--) {
		ret = vpu_process_capture_buffer(inst);
		if (ret)
			break;
		if (vdec->eos_received)
			break;
	}

	for (i = vdec->mbi.index; i < vdec->mbi.count; i++) {
		if (vdec_response_fs(inst, &vdec->mbi))
			break;
		if (vdec->eos_received)
			break;
	}
	for (i = vdec->dcp.index; i < vdec->dcp.count; i++) {
		if (vdec_response_fs(inst, &vdec->dcp))
			break;
		if (vdec->eos_received)
			break;
	}
}

static void vdec_response_fs_release(struct vpu_inst *inst, u32 id, u32 tag)
{
	struct vpu_fs_info info;

	memset(&info, 0, sizeof(info));
	info.id = id;
	info.tag = tag;
	vpu_session_release_fs(inst, &info);
}

static void vdec_recycle_buffer(struct vpu_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	if (!inst || !vbuf)
		return;

	if (vbuf->vb2_buf.state != VB2_BUF_STATE_ACTIVE)
		return;
	if (vpu_find_buf_by_idx(inst, vbuf->vb2_buf.type, vbuf->vb2_buf.index))
		return;
	v4l2_m2m_buf_queue(inst->m2m_ctx, vbuf);
}

static void vdec_clear_slots(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;
	int i;

	for (i = 0; i < ARRAY_SIZE(vdec->slots); i++) {
		if (!vdec->slots[i])
			continue;

		vpu_buf = vdec->slots[i];
		vbuf = &vpu_buf->m2m_buf.vb;
		if (vpu_buf->tag == vdec->seq_tag)
			inst_dbg(inst, LVL_WARN, "clear slot[%d], %d, %d\n",
				i,
				vdec->slots[i]->state,
				vbuf->vb2_buf.state);

		vdec_response_fs_release(inst, i, vpu_buf->tag);
		vdec_recycle_buffer(inst, vbuf);
		vdec->slots[i]->state = VPU_BUF_STATE_IDLE;
		vdec->slots[i] = NULL;
	}
}

static void vdec_event_seq_hdr(struct vpu_inst *inst,
				struct vpu_dec_codec_info *hdr)
{
	struct vdec_t *vdec = inst->priv;

	vpu_inst_lock(inst);
	memcpy(&vdec->codec_info, hdr, sizeof(vdec->codec_info));

	inst_dbg(inst, LVL_FLOW, "%d x %d, crop : (%d, %d) %d x %d\n",
			vdec->codec_info.decoded_width,
			vdec->codec_info.decoded_height,
			vdec->codec_info.offset_x,
			vdec->codec_info.offset_y,
			vdec->codec_info.width,
			vdec->codec_info.height);
	inst->min_buffer_cap = hdr->num_ref_frms + hdr->num_dpb_frms;
	vdec->is_source_changed = vdec_check_source_change(inst);
	vdec_init_fmt(inst);
	vdec_init_crop(inst);
	vdec_init_mbi(inst);
	vdec_init_dcp(inst);
	if (!vdec->seq_hdr_found) {
		vdec->seq_tag = vdec->codec_info.tag;
		if (vdec->is_source_changed) {
			vdec_update_state(inst, VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE, 0);
			vpu_notify_source_change(inst);
			vdec->is_source_changed = false;
		}
	}
	if (vdec->seq_tag != vdec->codec_info.tag) {
		vdec_response_fs_request(inst, true);
		inst_dbg(inst, LVL_DEBUG, "seq tag change: %d -> %d\n",
				vdec->seq_tag, vdec->codec_info.tag);
	}
	vdec->seq_hdr_found++;
	vpu_inst_unlock(inst);
}

static void vdec_event_resolution_change(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	inst_dbg(inst, LVL_FLOW, "resolution change\n");
	vpu_inst_lock(inst);
	vdec->seq_tag = vdec->codec_info.tag;
	vdec_clear_fs(&vdec->mbi);
	vdec_clear_fs(&vdec->dcp);
	vdec_clear_slots(inst);
	vdec_init_mbi(inst);
	vdec_init_dcp(inst);
	if (vdec->is_source_changed) {
		vdec_update_state(inst, VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE, 0);
		vdec->source_change++;
		vdec_handle_resolution_change(inst);
		vdec->is_source_changed = false;
	}
	vpu_inst_unlock(inst);
}

static void vdec_event_req_fs(struct vpu_inst *inst, struct vpu_fs_info *fs)
{
	struct vdec_t *vdec = inst->priv;

	if (!fs)
		return;

	vpu_inst_lock(inst);

	switch (fs->type) {
	case MEM_RES_FRAME:
		vdec->req_frame_count++;
		break;
	case MEM_RES_MBI:
		vdec_request_one_fs(&vdec->mbi);
		break;
	case MEM_RES_DCP:
		vdec_request_one_fs(&vdec->dcp);
		break;
	default:
		break;
	}

	vdec_alloc_fs(inst, &vdec->mbi);
	vdec_alloc_fs(inst, &vdec->dcp);

	vdec_response_fs_request(inst, false);

	vpu_inst_unlock(inst);
}

static void vdec_evnet_rel_fs(struct vpu_inst *inst, struct vpu_fs_info *fs)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;

	if (!fs || fs->id >= ARRAY_SIZE(vdec->slots))
		return;
	if (fs->type != MEM_RES_FRAME)
		return;

	if (fs->id >= vpu_get_num_buffers(inst, inst->cap_format.type)) {
		inst_err(inst, "invalid fs(%d) to release\n", fs->id);
		return;
	}

	vpu_inst_lock(inst);
	vpu_buf = vdec->slots[fs->id];
	vdec->slots[fs->id] = NULL;

	if (!vpu_buf) {
		inst_dbg(inst, LVL_DEBUG, "fs[%d] has bee released\n", fs->id);
		goto exit;
	}

	if (vpu_buf->state == VPU_BUF_STATE_DECODED) {
		inst_dbg(inst, LVL_DEBUG, "this frame is skipped\n");
		vpu_notify_skip(inst);
	}

	vdec_response_fs_release(inst, fs->id, vpu_buf->tag);
	vbuf = &vpu_buf->m2m_buf.vb;
	if (vpu_buf->state != VPU_BUF_STATE_READY)
		vdec_recycle_buffer(inst, vbuf);

	vpu_buf->state = VPU_BUF_STATE_IDLE;
	vpu_process_capture_buffer(inst);

exit:
	vpu_inst_unlock(inst);
}

static void vdec_event_eos(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	inst_dbg(inst, LVL_FLOW, "eos\n");
	vpu_inst_lock(inst);
	vdec->eos_received++;
	vdec_update_state(inst, VPU_CODEC_STATE_DRAIN, 0);
	vpu_process_capture_buffer(inst);
	vdec_handle_eos(inst);
	vpu_inst_unlock(inst);
}

static void vdec_event_notify(struct vpu_inst *inst, u32 event, void *data)
{
	switch (event) {
	case VPU_MSG_ID_SEQ_HDR_FOUND:
		vdec_event_seq_hdr(inst, data);
		break;
	case VPU_MSG_ID_RES_CHANGE:
		vdec_event_resolution_change(inst);
		break;
	case VPU_MSG_ID_FRAME_REQ:
		vdec_event_req_fs(inst, data);
		break;
	case VPU_MSG_ID_FRAME_RELEASE:
		vdec_evnet_rel_fs(inst, data);
		break;
	case VPU_MSG_ID_PIC_EOS:
		vdec_event_eos(inst);
		break;
	default:
		break;
	}
}

static int vdec_process_output(struct vpu_inst *inst, struct vb2_buffer *vb)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_v4l2_buffer *vbuf;
	struct vpu_vb2_buffer *vpu_buf;
	struct vpu_rpc_buffer_desc desc;
	s64 timestamp;
	u32 free_space;
	int ret;

	vbuf = to_vb2_v4l2_buffer(vb);
	vpu_buf = to_vpu_vb2_buffer(vbuf);
	inst_dbg(inst, LVL_DEBUG, "dec output [%d] %d : %ld\n",
		vbuf->sequence, vb->index, vb2_get_plane_payload(vb, 0));

	if (inst->state == VPU_CODEC_STATE_DEINIT)
		return -EINVAL;

	if (inst->state == VPU_CODEC_STATE_STARTED)
		vdec_update_state(inst, VPU_CODEC_STATE_ACTIVE, 0);

	ret = vpu_iface_get_stream_buffer_desc(inst, &desc);
	if (ret)
		return ret;

	free_space = vpu_helper_get_free_space(inst);
	if (free_space < vb2_get_plane_payload(vb, 0) + 0x40000)
		return -ENOMEM;

	timestamp = vb->timestamp;
	if (timestamp >= 0 && vdec->ts_start < 0)
		vdec->ts_start = timestamp;
	if (vdec->ts_input < timestamp)
		vdec->ts_input = timestamp;

	ret = vpu_iface_input_frame(inst, vb);
	if (ret < 0)
		return -ENOMEM;

	inst_dbg(inst, LVL_TS, "[INPUT  TS]%32lld\n", vb->timestamp);
	vdec->ts_pre_count++;
	vdec->params.frame_count++;

	v4l2_m2m_src_buf_remove_by_buf(inst->m2m_ctx, vbuf);
	vpu_buf->state = VPU_BUF_STATE_IDLE;
	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);

	if (vdec->drain)
		vdec_drain(inst);

	return 0;
}

static int vdec_process_capture(struct vpu_inst *inst, struct vb2_buffer *vb)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_v4l2_buffer *vbuf;
	int ret;

	if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		return -EINVAL;

	vbuf = to_vb2_v4l2_buffer(vb);

	if (vdec->eos_received) {
		if (vdec->eos_subscribed) {
			vdec_handle_eos(inst);
		} else {
			vb2_set_plane_payload(vb, 0, 0);
			vb2_set_plane_payload(vb, 1, 0);
			vbuf->flags |= V4L2_BUF_FLAG_LAST;
			v4l2_m2m_dst_buf_remove_by_buf(inst->m2m_ctx, vbuf);
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
			vdec->eos_received--;
		}
		return 0;
	}

	ret = vdec_response_frame(inst, vbuf);
	if (ret)
		return ret;
	v4l2_m2m_dst_buf_remove_by_buf(inst->m2m_ctx, vbuf);
	return 0;
}

static void vdec_on_queue_emtpy(struct vpu_inst *inst, u32 type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return;

	vdec_handle_resolution_change(inst);
	vdec_handle_eos(inst);
}

static void vdec_abort(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_rpc_buffer_desc desc;
	int ret;

	inst_dbg(inst, LVL_FLOW, "abort, inst->state = %d\n", inst->state);
	vpu_iface_add_scode(inst, SCODE_PADDING_ABORT);
	vdec->params.end_flag = 1;
	vpu_iface_set_decode_params(inst, &vdec->params, 1);

	vpu_session_abort(inst);

	ret = vpu_iface_get_stream_buffer_desc(inst, &desc);
	if (!ret)
		vpu_iface_update_stream_buffer(inst, desc.rptr, 1);

	vpu_session_rst_buf(inst);
	inst_dbg(inst, LVL_FLOW, "input : %d, decoded : %d, display : %d\n",
			vdec->params.frame_count,
			vdec->decoded_frame_count,
			vdec->display_frame_count);
	vdec->params.end_flag = 0;
	vdec->drain = 0;
	vdec->ts_pre_count = 0;
	vdec->timestamp = VPU_INVALID_TIMESTAMP;
	vdec->ts_start = VPU_INVALID_TIMESTAMP;
	vdec->ts_input = VPU_INVALID_TIMESTAMP;
	vdec->params.frame_count = 0;
	vdec->decoded_frame_count = 0;
	vdec->display_frame_count = 0;
}

static void vdec_release(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	inst_dbg(inst, LVL_FLOW, "release\n");
	vpu_inst_lock(inst);
	vdec_clear_slots(inst);
	if (inst->state != VPU_CODEC_STATE_DEINIT)
		vpu_session_stop(inst);
	vdec_clear_fs(&vdec->mbi);
	vdec_clear_fs(&vdec->dcp);
	vpu_free_dma(&vdec->udata);
	vpu_free_dma(&inst->stream_buffer);
	vdec_update_state(inst, VPU_CODEC_STATE_DEINIT, 1);
	vpu_inst_unlock(inst);
}

static void vdec_cleanup(struct vpu_inst *inst)
{
	struct vdec_t *vdec;

	if (!inst)
		return;

	vdec = inst->priv;
	if (vdec)
		vfree(vdec);
	inst->priv = NULL;
	vfree(inst);
}

static void vdec_init_params(struct vdec_t *vdec)
{
	vdec->params.frame_count = 0;
	vdec->params.end_flag = 0;
}

static int vdec_start(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	int stream_buffer_size;
	int ret;

	if (inst->state != VPU_CODEC_STATE_DEINIT)
		return 0;

	inst_dbg(inst, LVL_FLOW, "start\n");
	vdec->udata.length = 0x1000;
	ret = vpu_alloc_dma(inst->core, &vdec->udata);
	if (ret) {
		inst_err(inst, "alloc udata fail\n");
		goto error;
	}

	vpu_iface_init_instance(inst);
	stream_buffer_size = vpu_iface_get_stream_buffer_size(inst->core);
	if (stream_buffer_size > 0) {
		inst->stream_buffer.length = stream_buffer_size;
		ret = vpu_alloc_dma(inst->core, &inst->stream_buffer);
		if (ret) {
			inst_err(inst, "alloc stream buffer fail\n");
			goto error;
		}
		inst->use_stream_buffer = true;
		vpu_iface_config_stream_buffer(inst, &inst->stream_buffer);
	}

	vdec->params.udata.base = vdec->udata.phys;
	vdec->params.udata.size = vdec->udata.length;
	ret = vpu_iface_set_decode_params(inst, &vdec->params, 0);
	if (ret) {
		inst_err(inst, "set decode params fail\n");
		goto error;
	}

	vdec_init_params(vdec);
	ret = vpu_session_start(inst);
	if (ret) {
		inst_err(inst, "start fail\n");
		goto error;
	}

	vdec_update_state(inst, VPU_CODEC_STATE_STARTED, 0);

	return 0;
error:
	vpu_free_dma(&vdec->udata);
	vpu_free_dma(&inst->stream_buffer);
	return ret;
}

static int vdec_start_session(struct vpu_inst *inst, u32 type)
{
	struct vdec_t *vdec = inst->priv;
	int ret = 0;

	if (inst->state == VPU_CODEC_STATE_DEINIT) {
		if (V4L2_TYPE_IS_OUTPUT(type)) {
			ret = vdec_start(inst);
			if (ret)
				return ret;
		}
	}

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (inst->state == VPU_CODEC_STATE_SEEK)
			vdec_update_state(inst, vdec->state, 1);
		vdec->eos_received = 0;
		vpu_process_output_buffer(inst);
	} else {
		if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
			vdec_update_state(inst, VPU_CODEC_STATE_ACTIVE, 0);
		vpu_process_capture_buffer(inst);
	}
	if (inst->state == VPU_CODEC_STATE_ACTIVE)
		vdec_response_fs_request(inst, false);

	return ret;
}

static int vdec_stop_session(struct vpu_inst *inst, u32 type)
{
	struct vdec_t *vdec = inst->priv;

	if (inst->state == VPU_CODEC_STATE_DEINIT)
		return 0;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		vdec_update_state(inst, VPU_CODEC_STATE_SEEK, 0);
		vdec->drain = 0;
	} else {
		if (inst->state != VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
			vdec_abort(inst);

		vdec->eos_received = 0;
		vdec_clear_slots(inst);
	}

	return 0;
}

static int vdec_get_debug_info(struct vpu_inst *inst, char *str, u32 size, u32 i)
{
	struct vdec_t *vdec = inst->priv;
	int num = -1;

	switch (i) {
	case 0:
		num = scnprintf(str, size,
				"req_frame_count = %d\ninterlaced = %d\n",
				vdec->req_frame_count,
				vdec->codec_info.progressive ? 0 : 1);
		break;
	case 1:
		num = scnprintf(str, size,
				"mbi: size = 0x%x request = %d, alloc = %d, response = %d\n",
				vdec->mbi.size,
				vdec->mbi.req_count,
				vdec->mbi.count,
				vdec->mbi.index);
		break;
	case 2:
		num = scnprintf(str, size,
				"dcp: size = 0x%x request = %d, alloc = %d, response = %d\n",
				vdec->dcp.size,
				vdec->dcp.req_count,
				vdec->dcp.count,
				vdec->dcp.index);
		break;
	case 3:
		num = scnprintf(str, size, "input_frame_count = %d\n", vdec->params.frame_count);
		break;
	case 4:
		num = scnprintf(str, size, "decoded_frame_count = %d\n", vdec->decoded_frame_count);
		break;
	case 5:
		num = scnprintf(str, size, "display_frame_count = %d\n", vdec->display_frame_count);
		break;
	case 6:
		num = scnprintf(str, size, "drain = %d, eos = %d, source_change = %d\n",
				vdec->drain, vdec->eos_received, vdec->source_change);
		break;
	case 7:
		num = scnprintf(str, size, "ts_pre_count = %d, frame_depth = %d\n",
				vdec->ts_pre_count, vdec->frame_depth);
		break;
	case 8:
		num = scnprintf(str, size, "b_dis_reorder = %d, b_non_frame = %d\n",
				vdec->params.b_dis_reorder,
				vdec->params.b_non_frame);
		break;
	case 9:
	{
		s64 timestamp = vdec->timestamp;
		s64 ts_start = vdec->ts_start;
		s64 ts_input = vdec->ts_input;

		num = scnprintf(str, size, "timestamp = %9lld.%09lld(%9lld.%09lld, %9lld.%09lld)\n",
				timestamp / NSEC_PER_SEC,
				timestamp % NSEC_PER_SEC,
				ts_start / NSEC_PER_SEC,
				ts_start % NSEC_PER_SEC,
				ts_input / NSEC_PER_SEC,
				ts_input % NSEC_PER_SEC);
	}
		break;
	default:
		break;
	}

	return num;
}

static struct vpu_inst_ops vdec_inst_ops = {
	.ctrl_init = vdec_ctrl_init,
	.check_ready = vdec_check_ready,
	.buf_done = vdec_buf_done,
	.get_one_frame = vdec_frame_decoded,
	.stop_done = vdec_stop_done,
	.event_notify = vdec_event_notify,
	.release = vdec_release,
	.cleanup = vdec_cleanup,
	.start = vdec_start_session,
	.stop = vdec_stop_session,
	.process_output = vdec_process_output,
	.process_capture = vdec_process_capture,
	.on_queue_empty = vdec_on_queue_emtpy,
	.get_debug_info = vdec_get_debug_info,
	.wait_prepare = vpu_inst_unlock,
	.wait_finish = vpu_inst_lock,
};

static void vdec_init(struct file *file)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec;
	struct v4l2_format f;

	vdec = inst->priv;
	vdec->frame_depth = VDEC_FRAME_DEPTH;
	vdec->timestamp = VPU_INVALID_TIMESTAMP;
	vdec->ts_start = VPU_INVALID_TIMESTAMP;
	vdec->ts_input = VPU_INVALID_TIMESTAMP;

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f.fmt.pix_mp.width = 1280;
	f.fmt.pix_mp.height = 720;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	vdec_s_fmt(file, &inst->fh, &f);

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12_8L128;
	f.fmt.pix_mp.width = 1280;
	f.fmt.pix_mp.height = 720;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	vdec_s_fmt(file, &inst->fh, &f);
}

int vdec_open(struct file *file)
{
	struct vpu_inst *inst;
	struct vdec_t *vdec;
	int ret;

	inst = vzalloc(sizeof(*inst));
	if (!inst)
		return -ENOMEM;

	vdec = vzalloc(sizeof(*vdec));
	if (!vdec) {
		vfree(inst);
		return -ENOMEM;
	}

	inst->ops = &vdec_inst_ops;
	inst->formats = vdec_formats;
	inst->type = VPU_CORE_TYPE_DEC;
	inst->priv = vdec;

	ret = vpu_v4l2_open(file, inst);
	if (ret) {
		vdec_cleanup(inst);
		return ret;
	}

	vdec_init(file);

	return 0;
}

__poll_t vdec_poll(struct file *file, poll_table *wait)
{
	struct vpu_inst *inst = to_inst(file);
	struct vb2_queue *src_q, *dst_q;
	__poll_t ret;

	ret = v4l2_m2m_fop_poll(file, wait);
	src_q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	if (vb2_is_streaming(src_q) && !vb2_is_streaming(dst_q))
		ret &= (~EPOLLERR);

	return ret;
}

static const struct v4l2_file_operations vdec_fops = {
	.owner = THIS_MODULE,
	.open = vdec_open,
	.release = vpu_v4l2_close,
	.unlocked_ioctl = video_ioctl2,
	.poll = vdec_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

int vdec_create_video_device(struct vpu_dev *vpu)
{
	struct video_device *vdec;
	int ret;

	if (!vpu)
		return -EINVAL;

	if (vpu->vdev_dec)
		return 0;

	vdec = video_device_alloc();
	if (!vdec) {
		vpu_err("alloc vpu decoder video device fail\n");
		return -ENOMEM;
	}
	strscpy(vdec->name, "imx-vpu-decoder", sizeof(vdec->name));
	vdec->release = video_device_release;
	vdec->fops = &vdec_fops;
	vdec->ioctl_ops = &vdec_ioctl_ops;
	vdec->vfl_dir = VFL_DIR_M2M;
	vdec->v4l2_dev = &vpu->v4l2_dev;
	vdec->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	ret = video_register_device(vdec, VFL_TYPE_VIDEO, -1);
	if (ret) {
		video_device_release(vdec);
		return ret;
	}
	video_set_drvdata(vdec, vpu);
	vpu->vdev_dec = vdec;

	return 0;
}
