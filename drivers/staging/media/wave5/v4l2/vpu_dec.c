// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Wave5 series multi-standard codec IP - decoder interface
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#include "vpu_dec.h"

static const struct vpu_format vpu_dec_fmt_list[2][6] = {
	[VPU_FMT_TYPE_CODEC] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_HEVC,
			.num_planes   = 1,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_H264,
			.num_planes   = 1,
			.max_width    = 8192,
			.min_width    = 32,
			.max_height   = 8192,
			.min_height   = 32,
		},
	},
	[VPU_FMT_TYPE_RAW] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420,
			.num_planes   = 1,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12,
			.num_planes   = 1,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21,
			.num_planes   = 1,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420M,
			.num_planes   = 3,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12M,
			.num_planes   = 2,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21M,
			.num_planes   = 2,
			.max_width    = 8192,
			.min_width    = 8,
			.max_height   = 8192,
			.min_height   = 8,
		},
	}
};

static enum wave_std to_vpu_codstd(unsigned int v4l2_pix_fmt)
{
	switch (v4l2_pix_fmt) {
	case V4L2_PIX_FMT_H264:
		return W_AVC_DEC;
	case V4L2_PIX_FMT_HEVC:
		return W_HEVC_DEC;
	default:
		return STD_UNKNOWN;
	}
}

static const struct vpu_format *find_vpu_format(unsigned int v4l2_pix_fmt, enum vpu_format_type type)
{
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(vpu_dec_fmt_list[type]); index++) {
		if (vpu_dec_fmt_list[type][index].v4l2_pix_fmt == v4l2_pix_fmt)
			return &vpu_dec_fmt_list[type][index];
	}

	return NULL;
}

static const struct vpu_format *find_vpu_format_by_index(unsigned int index, enum vpu_format_type type)
{
	if (index >= ARRAY_SIZE(vpu_dec_fmt_list[type]))
		return NULL;

	if (vpu_dec_fmt_list[type][index].v4l2_pix_fmt == 0)
		return NULL;

	return &vpu_dec_fmt_list[type][index];
}

static void handle_bitstream_buffer(struct vpu_instance *inst)
{
	struct v4l2_m2m_buffer *v4l2_m2m_buf = NULL;

	unsigned long flags;

	spin_lock_irqsave(&inst->bitstream_lock, flags);

	v4l2_m2m_for_each_src_buf(inst->v4l2_fh.m2m_ctx, v4l2_m2m_buf) {
		enum ret_code                 ret_code = RETCODE_SUCCESS;
		struct vb2_v4l2_buffer *vbuf    = &v4l2_m2m_buf->vb;
		struct vpu_buffer      *vpu_buf = to_vpu_buf(vbuf);
		u32                  src_size = vb2_get_plane_payload(&vbuf->vb2_buf, 0);
		void                   *src_buf  = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
		dma_addr_t         bs_rd_ptr = 0;
		dma_addr_t         bs_wr_ptr = 0;
		u32                  bs_remain_size = 0;
		size_t offset;

		if (vpu_buf->consumed) {
			DPRINTK(inst->dev, 1, "already consumed buffer\n");
			continue;
		}

		vpu_dec_get_bitstream_buffer(inst, &bs_rd_ptr, &bs_wr_ptr, &bs_remain_size);

		if (bs_remain_size < src_size) {
			DPRINTK(inst->dev, 1, "fill next time : remained size < source size.\n");
			continue;
		}

		offset = bs_wr_ptr - inst->bitstream_vbuf.daddr;
		if (bs_wr_ptr + src_size > inst->bitstream_vbuf.daddr + inst->bitstream_vbuf.size) {
			int temp_size;

			temp_size = inst->bitstream_vbuf.daddr + inst->bitstream_vbuf.size - bs_wr_ptr;
			vdi_write_memory(inst->dev, &inst->bitstream_vbuf, offset, src_buf,
					 temp_size, VDI_128BIT_LITTLE_ENDIAN);
			vdi_write_memory(inst->dev, &inst->bitstream_vbuf, 0,
					 src_buf + temp_size, src_size - temp_size, VDI_128BIT_LITTLE_ENDIAN);
		} else {
			vdi_write_memory(inst->dev, &inst->bitstream_vbuf, offset, src_buf,
					 src_size, VDI_128BIT_LITTLE_ENDIAN);
		}

		ret_code = vpu_dec_update_bitstream_buffer(inst, src_size);
		if (ret_code != RETCODE_SUCCESS) {
			DPRINTK(inst->dev, 1, "failed to call vpu_dec_update_bitstream_buffer() : %d\n", ret_code);
			continue;
		}

		vpu_buf->consumed = TRUE;

		if (inst->state == VPU_INST_STATE_WAIT_BUF)
			inst->state = VPU_INST_STATE_PIC_RUN;
	}

	spin_unlock_irqrestore(&inst->bitstream_lock, flags);
}

static void handle_src_buffer(struct vpu_instance *inst)
{
	struct vb2_v4l2_buffer *src_buf;

	unsigned long flags;

	spin_lock_irqsave(&inst->bitstream_lock, flags);

	src_buf = v4l2_m2m_next_src_buf(inst->v4l2_fh.m2m_ctx);
	if (src_buf) {
		struct vpu_buffer *vpu_buf = to_vpu_buf(src_buf);

		if (vpu_buf->consumed) {
			u32 remain_num = 0;

			DPRINTK(inst->dev, 1, "already consumed buffer\n");
			remain_num = v4l2_m2m_num_src_bufs_ready(inst->v4l2_fh.m2m_ctx);
			DPRINTK(inst->dev, 1, "remain buffer : %d\n", remain_num);
			if (remain_num > 1) {
				src_buf = v4l2_m2m_src_buf_remove(inst->v4l2_fh.m2m_ctx);
				inst->timestamp = src_buf->vb2_buf.timestamp;
				v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
			}
		}
	}
	spin_unlock_irqrestore(&inst->bitstream_lock, flags);
}

static void update_resolution_info(struct v4l2_pix_format_mplane *pix_mp, unsigned int width, unsigned int height)
{
	switch (pix_mp->pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		pix_mp->width  = round_up(width, 32);
		pix_mp->height = height;
		pix_mp->plane_fmt[0].bytesperline = round_up(width, 32);
		pix_mp->plane_fmt[0].sizeimage    = width * height * 3 / 2;
		memset(&pix_mp->plane_fmt[0].reserved, 0, sizeof(pix_mp->plane_fmt[0].reserved));
		break;
	case V4L2_PIX_FMT_YUV420M:
		pix_mp->width  = round_up(width, 32);
		pix_mp->height = height;
		pix_mp->plane_fmt[0].bytesperline = round_up(width, 32);
		pix_mp->plane_fmt[0].sizeimage    = width * height;
		memset(&pix_mp->plane_fmt[0].reserved, 0, sizeof(pix_mp->plane_fmt[0].reserved));
		pix_mp->plane_fmt[1].bytesperline = round_up(width, 32) / 2;
		pix_mp->plane_fmt[1].sizeimage    = width * height / 4;
		memset(&pix_mp->plane_fmt[1].reserved, 0, sizeof(pix_mp->plane_fmt[1].reserved));
		pix_mp->plane_fmt[2].bytesperline = round_up(width, 32) / 2;
		pix_mp->plane_fmt[2].sizeimage    = width * height / 4;
		memset(&pix_mp->plane_fmt[2].reserved, 0, sizeof(pix_mp->plane_fmt[2].reserved));
		break;
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV21M:
		pix_mp->width  = round_up(width, 32);
		pix_mp->height = height;
		pix_mp->plane_fmt[0].bytesperline = round_up(width, 32);
		pix_mp->plane_fmt[0].sizeimage    = width * height;
		memset(&pix_mp->plane_fmt[0].reserved, 0, sizeof(pix_mp->plane_fmt[0].reserved));
		pix_mp->plane_fmt[1].bytesperline = round_up(width, 32);
		pix_mp->plane_fmt[1].sizeimage    = width * height / 2;
		memset(&pix_mp->plane_fmt[1].reserved, 0, sizeof(pix_mp->plane_fmt[1].reserved));
		break;
	default:
		pix_mp->width  = width;
		pix_mp->height = height;
		pix_mp->plane_fmt[0].bytesperline = 0;
		pix_mp->plane_fmt[0].sizeimage    = width * height / 2;
		memset(&pix_mp->plane_fmt[0].reserved, 0, sizeof(pix_mp->plane_fmt[0].reserved));
		break;
	}
}

static void vpu_dec_start_decode(struct vpu_instance *inst)
{
	struct dec_param pic_param;
	struct queue_status_info q_status;
	u32 remain_cmd_q, max_cmd_q = 0;

	memset(&pic_param, 0, sizeof(struct dec_param));

	vpu_dec_give_command(inst, DEC_GET_QUEUE_STATUS, &q_status);
	DPRINTK(inst->dev, 1, "min_src_buf_cnt : %d | default : %d | qcount : %d | report_q : %d\n",
		inst->min_src_frame_buf_count, COMMAND_QUEUE_DEPTH, q_status.instance_queue_count,
		q_status.report_queue_count);

	max_cmd_q    = (inst->min_src_frame_buf_count < COMMAND_QUEUE_DEPTH) ?
		inst->min_src_frame_buf_count : COMMAND_QUEUE_DEPTH;
	remain_cmd_q = max_cmd_q - q_status.instance_queue_count;

	while (remain_cmd_q) {
		enum ret_code ret_code;

		ret_code = vpu_dec_start_one_frame(inst, &pic_param);
		if (ret_code != RETCODE_SUCCESS) {
			if (ret_code != RETCODE_QUEUEING_FAILURE) {
				struct vb2_v4l2_buffer *src_buf =
					v4l2_m2m_src_buf_remove(inst->v4l2_fh.m2m_ctx);

				inst->state = VPU_INST_STATE_STOP;
				v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
				break;
			}
		} else {
			DPRINTK(inst->dev, 1, "vpu_dec_start_one_frame success %d\n", ret_code);
		}

		remain_cmd_q--;
	}
}

static void vpu_dec_stop_decode(struct vpu_instance *inst)
{
	u32 i;
	unsigned long flags;

	inst->state = VPU_INST_STATE_STOP;

	spin_lock_irqsave(&inst->bitstream_lock, flags);
	vpu_dec_update_bitstream_buffer(inst, 0);
	spin_unlock_irqrestore(&inst->bitstream_lock, flags);

	for (i = 0; i < inst->min_dst_frame_buf_count; i++) {
		vpu_dec_clr_disp_flag(inst, i);
		DPRINTK(inst->dev, 1, "clear display flag : %d\n", i);
	}
}

static void vpu_dec_finish_decode(struct vpu_instance *inst)
{
	struct dec_output_info dec_output_info;
	enum ret_code ret_code;
	int irq_status;

	if (kfifo_out(&inst->dev->irq_status, &irq_status, sizeof(int)))
		vpu_clear_interrupt_ex(inst, irq_status);

	if (irq_status & (1 << INT_WAVE5_BSBUF_EMPTY)) {
		DPRINTK(inst->dev, 1, "bitstream EMPTY!!!!\n");
		inst->state = VPU_INST_STATE_WAIT_BUF;
		handle_src_buffer(inst);
		handle_bitstream_buffer(inst);
	}

	if (irq_status & (1 << INT_WAVE5_DEC_PIC)) {
		ret_code = vpu_dec_get_output_info(inst, &dec_output_info);
		if (ret_code != RETCODE_SUCCESS) {
			v4l2_m2m_job_finish(inst->dev->v4l2_m2m_dev, inst->v4l2_fh.m2m_ctx);
		} else {
			if (inst->state == VPU_INST_STATE_STOP) {
				struct queue_status_info q_status;

				if (dec_output_info.index_frame_display >= 0)
					vpu_dec_clr_disp_flag(inst, dec_output_info.index_frame_display);

				vpu_dec_give_command(inst, DEC_GET_QUEUE_STATUS, &q_status);

				if (q_status.report_queue_count + q_status.instance_queue_count == 0)
					v4l2_m2m_job_finish(inst->dev->v4l2_m2m_dev, inst->v4l2_fh.m2m_ctx);
			} else if (dec_output_info.index_frame_decoded == DECODED_IDX_FLAG_NO_FB &&
					dec_output_info.index_frame_display == DISPLAY_IDX_FLAG_NO_FB) {
				DPRINTK(inst->dev, 1, "no more frame buffer\n");
				inst->state = VPU_INST_STATE_WAIT_BUF;
			} else {
				handle_src_buffer(inst);

				if (dec_output_info.index_frame_display >= 0) {
					struct vb2_v4l2_buffer *dst_buf =
						v4l2_m2m_dst_buf_remove_by_idx(inst->v4l2_fh.m2m_ctx,
									       dec_output_info.index_frame_display);
					int stride = dec_output_info.disp_frame.stride;
					int height = dec_output_info.disp_pic_height;

					if (inst->dst_fmt.num_planes == 1) {
						vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
								      (stride * height * 3 / 2));
					} else if (inst->dst_fmt.num_planes == 2) {
						vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
								      (stride * height));
						vb2_set_plane_payload(&dst_buf->vb2_buf, 1,
								      ((stride / 2) * height));
					} else if (inst->dst_fmt.num_planes == 3) {
						vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
								      (stride * height));
						vb2_set_plane_payload(&dst_buf->vb2_buf, 1,
								      ((stride / 2) * (height / 2)));
						vb2_set_plane_payload(&dst_buf->vb2_buf, 2,
								      ((stride / 2) * (height / 2)));
					}

					dst_buf->vb2_buf.timestamp = inst->timestamp;
					dst_buf->field = V4L2_FIELD_NONE;
					v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
				} else if (dec_output_info.index_frame_display == DISPLAY_IDX_FLAG_SEQ_END) {
					static const struct v4l2_event vpu_event_eos = {
						.type = V4L2_EVENT_EOS
					};

					DPRINTK(inst->dev, 1, "stream end\n");
					inst->state = VPU_INST_STATE_STOP;
					v4l2_event_queue_fh(&inst->v4l2_fh, &vpu_event_eos);
				}

				if (!kfifo_len(&inst->dev->irq_status))
					v4l2_m2m_job_finish(inst->dev->v4l2_m2m_dev,
							    inst->v4l2_fh.m2m_ctx);
			}
		}
	}
}

static int vpu_dec_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct vpu_instance *inst = to_vpu_inst(fh);

	DPRINTK(inst->dev, 1, "\n");

	strscpy(cap->driver,   VPU_DEC_DRV_NAME,            sizeof(cap->driver));
	strscpy(cap->card,     VPU_DEC_DRV_NAME,            sizeof(cap->card));
	strscpy(cap->bus_info, "platform:" VPU_DEC_DRV_NAME, sizeof(cap->bus_info));

	return 0;
}

static int vpu_dec_enum_framesizes(struct file *file, void *fh, struct v4l2_frmsizeenum *fsize)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	DPRINTK(inst->dev, 1, "\n");

	if (fsize->index)
		return -EINVAL;

	vpu_fmt = find_vpu_format(fsize->pixel_format, VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		vpu_fmt = find_vpu_format(fsize->pixel_format, VPU_FMT_TYPE_RAW);
		if (!vpu_fmt)
			return -EINVAL;
	}

	fsize->type                 = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width   = vpu_fmt->min_width;
	fsize->stepwise.max_width   = vpu_fmt->max_width;
	fsize->stepwise.step_width  = 1;
	fsize->stepwise.min_height  = vpu_fmt->min_height;
	fsize->stepwise.max_height  = vpu_fmt->max_height;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int vpu_dec_enum_fmt_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	DPRINTK(inst->dev, 1, "index : %d\n", f->index);

	vpu_fmt = find_vpu_format_by_index(f->index, VPU_FMT_TYPE_RAW);
	if (!vpu_fmt)
		return -EINVAL;

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags       = 0;

	return 0;
}

static int vpu_dec_try_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance     *inst = to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	DPRINTK(inst->dev, 1, "4cc %d width %d height %d num_planes %d colorspace %d field : %d\n",
		f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.colorspace, f->fmt.pix_mp.field);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	vpu_fmt = find_vpu_format(f->fmt.pix_mp.pixelformat, VPU_FMT_TYPE_RAW);
	if (!vpu_fmt) {
		f->fmt.pix_mp.pixelformat = inst->dst_fmt.pixelformat;
		f->fmt.pix_mp.num_planes  = inst->dst_fmt.num_planes;
		update_resolution_info(&f->fmt.pix_mp, inst->dst_fmt.width, inst->dst_fmt.height);
	} else {
		f->fmt.pix_mp.pixelformat = vpu_fmt->v4l2_pix_fmt;
		f->fmt.pix_mp.num_planes  = vpu_fmt->num_planes;
		update_resolution_info(&f->fmt.pix_mp, clamp(f->fmt.pix_mp.width,
							     vpu_fmt->min_width, vpu_fmt->max_width),
				clamp(f->fmt.pix_mp.height, vpu_fmt->min_height,
				      vpu_fmt->max_height));
	}

	f->fmt.pix_mp.flags        = 0;
	f->fmt.pix_mp.field        = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace   = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc    = inst->ycbcr_enc;
	f->fmt.pix_mp.hsv_enc      = inst->hsv_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func    = inst->xfer_func;
	memset(&f->fmt.pix_mp.reserved, 0, sizeof(f->fmt.pix_mp.reserved));

	return 0;
}

static int vpu_dec_s_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	int i, ret;

	DPRINTK(inst->dev, 1, "4cc %d width %d height %d num_planes %d colorspace %d field %d\n",
		f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.colorspace, f->fmt.pix_mp.field);

	ret = vpu_dec_try_fmt_cap(file, fh, f);
	if (ret)
		return ret;

	inst->dst_fmt.width        = f->fmt.pix_mp.width;
	inst->dst_fmt.height       = f->fmt.pix_mp.height;
	inst->dst_fmt.pixelformat  = f->fmt.pix_mp.pixelformat;
	inst->dst_fmt.field        = f->fmt.pix_mp.field;
	inst->dst_fmt.flags        = f->fmt.pix_mp.flags;
	inst->dst_fmt.num_planes   = f->fmt.pix_mp.num_planes;
	for (i = 0; i < inst->dst_fmt.num_planes; i++) {
		inst->dst_fmt.plane_fmt[i].bytesperline = f->fmt.pix_mp.plane_fmt[i].bytesperline;
		inst->dst_fmt.plane_fmt[i].sizeimage    = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	return 0;
}

static int vpu_dec_g_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	int i;

	DPRINTK(inst->dev, 1, "\n");

	f->fmt.pix_mp.width        = inst->dst_fmt.width;
	f->fmt.pix_mp.height       = inst->dst_fmt.height;
	f->fmt.pix_mp.pixelformat  = inst->dst_fmt.pixelformat;
	f->fmt.pix_mp.field        = inst->dst_fmt.field;
	f->fmt.pix_mp.flags        = inst->dst_fmt.flags;
	f->fmt.pix_mp.num_planes   = inst->dst_fmt.num_planes;
	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		f->fmt.pix_mp.plane_fmt[i].bytesperline = inst->dst_fmt.plane_fmt[i].bytesperline;
		f->fmt.pix_mp.plane_fmt[i].sizeimage    = inst->dst_fmt.plane_fmt[i].sizeimage;
	}

	f->fmt.pix_mp.colorspace   = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc    = inst->ycbcr_enc;
	f->fmt.pix_mp.hsv_enc      = inst->hsv_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func    = inst->xfer_func;

	return 0;
}

static int vpu_dec_enum_fmt_out(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	DPRINTK(inst->dev, 1, "index : %d\n", f->index);

	vpu_fmt = find_vpu_format_by_index(f->index, VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt)
		return -EINVAL;

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags       = 0;

	return 0;
}

static int vpu_dec_try_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance     *inst = to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	DPRINTK(inst->dev, 1, "4cc %d width %d height %d num_planes %d colorspace %d field %d\n",
		f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.colorspace, f->fmt.pix_mp.field);

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	vpu_fmt = find_vpu_format(f->fmt.pix_mp.pixelformat, VPU_FMT_TYPE_CODEC);
	if (!vpu_fmt) {
		f->fmt.pix_mp.pixelformat = inst->src_fmt.pixelformat;
		f->fmt.pix_mp.num_planes  = inst->src_fmt.num_planes;
		update_resolution_info(&f->fmt.pix_mp, inst->src_fmt.width, inst->src_fmt.height);
	} else {
		f->fmt.pix_mp.pixelformat = vpu_fmt->v4l2_pix_fmt;
		f->fmt.pix_mp.num_planes  = vpu_fmt->num_planes;
		update_resolution_info(&f->fmt.pix_mp, clamp(f->fmt.pix_mp.width,
							     vpu_fmt->min_width, vpu_fmt->max_width),
				clamp(f->fmt.pix_mp.height, vpu_fmt->min_height,
				      vpu_fmt->max_height));
	}

	f->fmt.pix_mp.flags = 0;
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	memset(&f->fmt.pix_mp.reserved, 0, sizeof(f->fmt.pix_mp.reserved));

	return 0;
}

static int vpu_dec_s_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	int i, ret;

	DPRINTK(inst->dev, 1, "pixelformat %d width %d height %d num_planes %d  field : %d\n",
		f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.field);

	ret = vpu_dec_try_fmt_out(file, fh, f);
	if (ret)
		return ret;

	inst->src_fmt.width        = f->fmt.pix_mp.width;
	inst->src_fmt.height       = f->fmt.pix_mp.height;
	inst->src_fmt.pixelformat  = f->fmt.pix_mp.pixelformat;
	inst->src_fmt.field        = f->fmt.pix_mp.field;
	inst->src_fmt.flags        = f->fmt.pix_mp.flags;
	inst->src_fmt.num_planes   = f->fmt.pix_mp.num_planes;
	for (i = 0; i < inst->src_fmt.num_planes; i++) {
		inst->src_fmt.plane_fmt[i].bytesperline = f->fmt.pix_mp.plane_fmt[i].bytesperline;
		inst->src_fmt.plane_fmt[i].sizeimage    = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	inst->colorspace   = f->fmt.pix_mp.colorspace;
	inst->ycbcr_enc    = f->fmt.pix_mp.ycbcr_enc;
	inst->hsv_enc      = f->fmt.pix_mp.hsv_enc;
	inst->quantization = f->fmt.pix_mp.quantization;
	inst->xfer_func    = f->fmt.pix_mp.xfer_func;

	update_resolution_info(&inst->dst_fmt, f->fmt.pix_mp.width, f->fmt.pix_mp.height);

	return 0;
}

static int vpu_dec_g_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	int i;

	DPRINTK(inst->dev, 1, "\n");

	f->fmt.pix_mp.width        = inst->src_fmt.width;
	f->fmt.pix_mp.height       = inst->src_fmt.height;
	f->fmt.pix_mp.pixelformat  = inst->src_fmt.pixelformat;
	f->fmt.pix_mp.field        = inst->src_fmt.field;
	f->fmt.pix_mp.flags        = inst->src_fmt.flags;
	f->fmt.pix_mp.num_planes   = inst->src_fmt.num_planes;
	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		f->fmt.pix_mp.plane_fmt[i].bytesperline = inst->src_fmt.plane_fmt[i].bytesperline;
		f->fmt.pix_mp.plane_fmt[i].sizeimage    = inst->src_fmt.plane_fmt[i].sizeimage;
	}

	f->fmt.pix_mp.colorspace   = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc    = inst->ycbcr_enc;
	f->fmt.pix_mp.hsv_enc      = inst->hsv_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func    = inst->xfer_func;

	return 0;
}

static int vpu_dec_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_instance *inst = to_vpu_inst(fh);

	DPRINTK(inst->dev, 1, "type : %d | target : %d\n", s->type, s->target);

	if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		switch (s->target) {
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		case V4L2_SEL_TGT_COMPOSE_PADDED:
			s->r.left   = 0;
			s->r.top    = 0;
			s->r.width  = inst->dst_fmt.width;
			s->r.height = inst->dst_fmt.height;
			break;
		case V4L2_SEL_TGT_COMPOSE:
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
			s->r.left   = 0;
			s->r.top    = 0;
			s->r.width  = inst->src_fmt.width;
			s->r.height = inst->src_fmt.height;
			break;
		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int vpu_dec_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_instance *inst = to_vpu_inst(fh);

	DPRINTK(inst->dev, 1, "type : %d | target : %d\n", s->type, s->target);

	if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		switch (s->target) {
		case V4L2_SEL_TGT_COMPOSE:
			DPRINTK(inst->dev, 1, "V4L2_SEL_TGT_COMPOSE width : %d | height : %d\n",
				s->r.width, s->r.height);
			inst->dst_fmt.width  = s->r.width;
			inst->dst_fmt.height = s->r.height;
			break;
		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int vpu_dec_try_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *dc)
{
	struct vpu_instance *inst = to_vpu_inst(fh);

	DPRINTK(inst->dev, 1, "decoder command : %d\n", dc->cmd);

	if (dc->cmd != V4L2_DEC_CMD_STOP && dc->cmd != V4L2_DEC_CMD_START)
		return -EINVAL;

	dc->flags = 0;

	if (dc->cmd == V4L2_DEC_CMD_STOP) {
		dc->stop.pts = 0;
	} else if (dc->cmd == V4L2_DEC_CMD_START) {
		dc->start.speed = 0;
		dc->start.format = V4L2_DEC_START_FMT_NONE;
	}

	return 0;
}

static int vpu_dec_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *dc)
{
	struct vpu_instance *inst = to_vpu_inst(fh);
	int ret;

	DPRINTK(inst->dev, 1, "decoder command : %d\n", dc->cmd);

	ret = vpu_dec_try_decoder_cmd(file, fh, dc);
	if (ret < 0)
		return ret;

	if (!vb2_is_streaming(v4l2_m2m_get_vq(inst->v4l2_fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)) ||
	    !vb2_is_streaming(v4l2_m2m_get_vq(inst->v4l2_fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)))
		return 0;

	switch (dc->cmd) {
	case V4L2_DEC_CMD_STOP:
		vpu_dec_stop_decode(inst);
		break;
	case V4L2_DEC_CMD_START:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vpu_dec_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	struct vpu_instance *inst = to_vpu_inst(fh);

	DPRINTK(inst->dev, 1, "type : %d | id : %d | flags : %d\n", sub->type, sub->id, sub->flags);

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops vpu_dec_ioctl_ops = {
	.vidioc_querycap                = vpu_dec_querycap,
	.vidioc_enum_framesizes         = vpu_dec_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= vpu_dec_enum_fmt_cap,
	.vidioc_s_fmt_vid_cap_mplane    = vpu_dec_s_fmt_cap,
	.vidioc_g_fmt_vid_cap_mplane    = vpu_dec_g_fmt_cap,
	.vidioc_try_fmt_vid_cap_mplane  = vpu_dec_try_fmt_cap,

	.vidioc_enum_fmt_vid_out	= vpu_dec_enum_fmt_out,
	.vidioc_s_fmt_vid_out_mplane    = vpu_dec_s_fmt_out,
	.vidioc_g_fmt_vid_out_mplane    = vpu_dec_g_fmt_out,
	.vidioc_try_fmt_vid_out_mplane  = vpu_dec_try_fmt_out,

	.vidioc_g_selection             = vpu_dec_g_selection,
	.vidioc_s_selection             = vpu_dec_s_selection,

	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf             = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf                    = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,

	.vidioc_try_decoder_cmd         = vpu_dec_try_decoder_cmd,
	.vidioc_decoder_cmd             = vpu_dec_decoder_cmd,

	.vidioc_subscribe_event         = vpu_dec_subscribe_event,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
};

static int vpu_dec_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_instance *inst = ctrl_to_vpu_inst(ctrl);

	DPRINTK(inst->dev, 1, "name : %s\n", ctrl->name);

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		if (inst->state != VPU_INST_STATE_NONE && inst->state != VPU_INST_STATE_OPEN)
			ctrl->val = inst->min_dst_frame_buf_count;
		break;
	default:
		return -EINVAL;
	}

	DPRINTK(inst->dev, 1, "value : %d\n", ctrl->val);

	return 0;
}

static int vpu_dec_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_instance *inst = ctrl_to_vpu_inst(ctrl);

	DPRINTK(inst->dev, 1, "name : %s | value : %d\n", ctrl->name, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_VPU_THUMBNAIL_MODE:
		inst->thumbnail_mode = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops vpu_dec_ctrl_ops = {
	.g_volatile_ctrl = vpu_dec_g_volatile_ctrl,
	.s_ctrl          = vpu_dec_s_ctrl,
};

static const struct v4l2_ctrl_config vpu_thumbnail_mode = {
	.ops   = &vpu_dec_ctrl_ops,
	.id    = V4L2_CID_VPU_THUMBNAIL_MODE,
	.name  = "thumbnail mode",
	.type  = V4L2_CTRL_TYPE_BOOLEAN,
	.def   = 0,
	.min   = 0,
	.max   = 1,
	.step  = 1,
	.flags = V4L2_CTRL_FLAG_WRITE_ONLY,
};

static void set_default_dec_openparam(struct dec_open_param *open_param)
{
	open_param->bitstream_mode  = BS_MODE_INTERRUPT;
	open_param->stream_endian   = VPU_STREAM_ENDIAN;
	open_param->frame_endian    = VPU_FRAME_ENDIAN;
	open_param->cbcr_interleave = FALSE;
	open_param->nv21           = FALSE;
}

static int vpu_dec_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			       unsigned int *num_planes, unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane inst_format =
		(q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? inst->src_fmt : inst->dst_fmt;
	unsigned int i;

	DPRINTK(inst->dev, 1, "num_buffers : %d | num_planes : %d | type : %d\n", *num_buffers,
		*num_planes, q->type);

	if (*num_planes) {
		if (inst_format.num_planes != *num_planes)
			return -EINVAL;

		for (i = 0; i < *num_planes; i++) {
			if (sizes[i] < inst_format.plane_fmt[i].sizeimage)
				return -EINVAL;
		}
	} else {
		*num_planes = inst_format.num_planes;

		if (*num_planes == 1) {
			sizes[0] = inst_format.width * inst_format.height * 3 / 2;
			if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
				sizes[0] = inst_format.plane_fmt[0].sizeimage;
			DPRINTK(inst->dev, 1, "size[0] : %d\n", sizes[0]);
		} else if (*num_planes == 2) {
			sizes[0] = inst_format.width * inst_format.height;
			sizes[1] = inst_format.width * inst_format.height / 2;
			DPRINTK(inst->dev, 1, "size[0] : %d | size[1] : %d\n", sizes[0], sizes[1]);
		} else if (*num_planes == 3) {
			sizes[0] = inst_format.width * inst_format.height;
			sizes[1] = inst_format.width * inst_format.height / 4;
			sizes[2] = inst_format.width * inst_format.height / 4;
			DPRINTK(inst->dev, 1, "size[0] : %d | size[1] : %d | size[2] : %d\n",
				sizes[0], sizes[1], sizes[2]);
		}
	}

	if (inst->state == VPU_INST_STATE_NONE && q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		enum ret_code ret_code;
		struct dec_open_param open_param;

		memset(&open_param, 0, sizeof(struct dec_open_param));
		set_default_dec_openparam(&open_param);

		inst->bitstream_vbuf.size = ALIGN(inst->src_fmt.plane_fmt[0].sizeimage, 1024) * 4;
		if (vdi_allocate_dma_memory(inst->dev, &inst->bitstream_vbuf) < 0)
			DPRINTK(inst->dev, 1, "alloc bitstream fail: %zu\n", inst->bitstream_vbuf.size);

		inst->std = to_vpu_codstd(inst->src_fmt.pixelformat);
		if (inst->std == STD_UNKNOWN) {
			dev_warn(inst->dev->dev, "unsupported pixelformat: %.4s\n",
				 (char *)&inst->src_fmt.pixelformat);
			return -EINVAL;
		}
		open_param.bitstream_buffer     = inst->bitstream_vbuf.daddr;
		open_param.bitstream_buffer_size = inst->bitstream_vbuf.size;

		ret_code = vpu_dec_open_api(inst, &open_param);
		if (ret_code != RETCODE_SUCCESS) {
			DPRINTK(inst->dev, 1, "failed to call vpu_dec_open_api() : %d\n", ret_code);
			return -EINVAL;
		}

		inst->state = VPU_INST_STATE_OPEN;

		//vpu_dec_give_command(inst, ENABLE_LOGGING, 0);

		if (inst->thumbnail_mode)
			vpu_dec_give_command(inst, ENABLE_DEC_THUMBNAIL_MODE, 0);

		inst->min_src_frame_buf_count = *num_buffers;
	}

	if (inst->state != VPU_INST_STATE_NONE && inst->state != VPU_INST_STATE_OPEN && q->type ==
			V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		*num_buffers = inst->min_dst_frame_buf_count;

		if (inst->state == VPU_INST_STATE_INIT_SEQ) {
			s32 non_linear_num = inst->min_dst_frame_buf_count;
			s32 fb_stride, fb_height;
			s32 luma_size, chroma_size;

			for (i = 0; i < non_linear_num; i++) {
				fb_stride   = inst->dst_fmt.width;
				fb_height   = ALIGN(inst->dst_fmt.height, 32);
				luma_size   = fb_stride * fb_height;
				chroma_size = ALIGN(fb_stride / 2, 16) * fb_height;

				inst->frame_vbuf[i].size = luma_size + chroma_size;
				if (vdi_allocate_dma_memory(inst->dev, &inst->frame_vbuf[i]) < 0)
					DPRINTK(inst->dev, 1, "failed to alloc FBC buffer : %zu\n",
						inst->frame_vbuf[i].size);

				inst->frame_buf[i].buf_y    = inst->frame_vbuf[i].daddr;
				inst->frame_buf[i].buf_cb   = inst->frame_vbuf[i].daddr + luma_size;
				inst->frame_buf[i].buf_cr   = (dma_addr_t)-1;
				inst->frame_buf[i].size    = inst->frame_vbuf[i].size;
				inst->frame_buf[i].width   = inst->src_fmt.width;
				inst->frame_buf[i].stride  = fb_stride;
				inst->frame_buf[i].map_type = COMPRESSED_FRAME_MAP;
				inst->frame_buf[i].update_fb_info = TRUE;
			}
		}
	}

	return 0;
}

static int vpu_dec_buf_init(struct vb2_buffer *vb)
{
	struct vpu_instance    *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	DPRINTK(inst->dev, 1, "type : %4d  index %4d size[0] %4ld size[1] %4ld size[2] : %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));
	return 0;
}

static int vpu_dec_buf_prepare(struct vb2_buffer *vb)
{
	struct vpu_instance    *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf     = to_vb2_v4l2_buffer(vb);

	DPRINTK(inst->dev, 1, "type : %4d index %4d size[0] %4ld size[1] %4ld size[2] %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));

	return 0;
}

static void vpu_dec_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf    = to_vb2_v4l2_buffer(vb);
	struct vpu_instance    *inst    = vb2_get_drv_priv(vb->vb2_queue);
	struct vpu_buffer      *vpu_buf = to_vpu_buf(vbuf);

	DPRINTK(inst->dev, 1, "type %4d index %4d size[0] %4ld size[1] : %4ld | size[2] : %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));

	v4l2_m2m_buf_queue(inst->v4l2_fh.m2m_ctx, vbuf);

	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		vpu_buf->consumed = FALSE;
		handle_bitstream_buffer(inst);
		vbuf->sequence = inst->queued_src_buf_num++;
	}

	if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (inst->state == VPU_INST_STATE_INIT_SEQ) {
			dma_addr_t buf_addr_y = 0, buf_addr_cb = 0, buf_addr_cr = 0;
			s32 buf_size     = 0;
			s32 non_linear_num = inst->min_dst_frame_buf_count;
			s32 fb_stride     = inst->dst_fmt.width;
			s32 luma_size     = fb_stride * inst->dst_fmt.height;
			s32 chroma_size   = (fb_stride / 2) * (inst->dst_fmt.height / 2);

			if (inst->dst_fmt.num_planes == 1) {
				buf_size    = vb2_plane_size(&vbuf->vb2_buf, 0);
				buf_addr_y  = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
				buf_addr_cb = buf_addr_y + luma_size;
				buf_addr_cr = buf_addr_cb + chroma_size;
			} else if (inst->dst_fmt.num_planes == 2) {
				buf_size    = vb2_plane_size(&vbuf->vb2_buf, 0) +
					vb2_plane_size(&vbuf->vb2_buf, 1);
				buf_addr_y  = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
				buf_addr_cb = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 1);
				buf_addr_cr = buf_addr_cb + chroma_size;
			} else if (inst->dst_fmt.num_planes == 3) {
				buf_size    = vb2_plane_size(&vbuf->vb2_buf, 0) +
					vb2_plane_size(&vbuf->vb2_buf, 1) +
					vb2_plane_size(&vbuf->vb2_buf, 2);
				buf_addr_y  = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
				buf_addr_cb = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 1);
				buf_addr_cr = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 2);
			}
			inst->frame_buf[vb->index + non_linear_num].buf_y    = buf_addr_y;
			inst->frame_buf[vb->index + non_linear_num].buf_cb   = buf_addr_cb;
			inst->frame_buf[vb->index + non_linear_num].buf_cr   = buf_addr_cr;
			inst->frame_buf[vb->index + non_linear_num].size    = buf_size;
			inst->frame_buf[vb->index + non_linear_num].width   = inst->src_fmt.width;
			inst->frame_buf[vb->index + non_linear_num].stride  = fb_stride;
			inst->frame_buf[vb->index + non_linear_num].map_type = LINEAR_FRAME_MAP;
			inst->frame_buf[vb->index + non_linear_num].update_fb_info = TRUE;
		}

		if (inst->state == VPU_INST_STATE_PIC_RUN || inst->state == VPU_INST_STATE_STOP ||
		    inst->state == VPU_INST_STATE_WAIT_BUF) {
			vpu_dec_clr_disp_flag(inst, vb->index);
			if (inst->state == VPU_INST_STATE_WAIT_BUF)
				inst->state = VPU_INST_STATE_PIC_RUN;
		}
		vbuf->sequence = inst->queued_dst_buf_num++;
	}
}

static void vpu_dec_buf_finish(struct vb2_buffer *vb)
{
	struct vpu_instance    *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	DPRINTK(inst->dev, 1, "type %4d index : %4d size[0] %4ld size[1] %4ld | size[2] : %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));
}

static void vpu_dec_buf_cleanup(struct vb2_buffer *vb)
{
	struct vpu_instance    *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	DPRINTK(inst->dev, 1, "type %4d index %4d size[0] %4ld size[1] : %4ld | size[2] : %4ld\n",
		vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));
}

static int vpu_dec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);

	DPRINTK(inst->dev, 1, "type : %d\n", q->type);

	if (inst->state == VPU_INST_STATE_OPEN) {
		struct dec_initial_info initial_info;
		enum ret_code        ret_code;

		ret_code = vpu_dec_issue_seq_init(inst);
		if (ret_code != RETCODE_SUCCESS)
			DPRINTK(inst->dev, 1, "failed vpu_dec_issue_seq_init() : %d\n", ret_code);

		if (vpu_wait_interrupt(inst, VPU_DEC_TIMEOUT) < 0)
			DPRINTK(inst->dev, 1, "failed to call vpu_wait_interrupt()\n");

		ret_code = vpu_dec_complete_seq_init(inst, &initial_info);
		if (ret_code != RETCODE_SUCCESS) {
			DPRINTK(inst->dev, 1, "vpu_dec_complete_seq_init: %d, reason : %d\n",
				ret_code, initial_info.seq_init_err_reason);
		} else {
			static const struct v4l2_event vpu_event_src_ch = {
				.type = V4L2_EVENT_SOURCE_CHANGE,
				.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
			};

			DPRINTK(inst->dev, 1, "width %d height %d profile %d | minbuffer : %d\n",
				initial_info.pic_width, initial_info.pic_height,
				initial_info.profile, initial_info.min_frame_buffer_count);

			inst->state = VPU_INST_STATE_INIT_SEQ;
			inst->min_dst_frame_buf_count = initial_info.min_frame_buffer_count + 1;

			if (initial_info.pic_width  != inst->src_fmt.width ||
			    initial_info.pic_height != inst->src_fmt.height) {
				update_resolution_info(&inst->src_fmt, initial_info.pic_width,
						       initial_info.pic_height);
				update_resolution_info(&inst->dst_fmt, initial_info.pic_width,
						       initial_info.pic_height);
			}

			v4l2_event_queue_fh(&inst->v4l2_fh, &vpu_event_src_ch);
		}
	}

	if (inst->state == VPU_INST_STATE_INIT_SEQ && q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		enum ret_code ret_code;
		s32 non_linear_num = inst->min_dst_frame_buf_count;
		s32 linear_num    = inst->min_dst_frame_buf_count;
		s32 fb_stride     = inst->dst_fmt.width;

		DPRINTK(inst->dev, 1, "fb_stride %d | inst->dst_fmt.height %d\n", fb_stride, inst->dst_fmt.height);
		ret_code = vpu_dec_register_frame_buffer_ex(inst, non_linear_num, linear_num,
							    fb_stride, inst->dst_fmt.height, COMPRESSED_FRAME_MAP);
		if (ret_code != RETCODE_SUCCESS)
			DPRINTK(inst->dev, 1, "fail vpu_dec_register_frame_buffer_ex %d", ret_code);

		inst->state = VPU_INST_STATE_PIC_RUN;
	}

	return 0;
}

static void vpu_dec_stop_streaming(struct vb2_queue *q)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *buf;

	DPRINTK(inst->dev, 1, "type : %d\n", q->type);

	if (vb2_is_streaming(v4l2_m2m_get_vq(inst->v4l2_fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)) &&
	    vb2_is_streaming(v4l2_m2m_get_vq(inst->v4l2_fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)))
		inst->state = VPU_INST_STATE_STOP;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		while ((buf = v4l2_m2m_src_buf_remove(inst->v4l2_fh.m2m_ctx))) {
			DPRINTK(inst->dev, 1, "buf type : %4d | index : %4d\n", buf->vb2_buf.type,
				buf->vb2_buf.index);
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
		}
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(inst->v4l2_fh.m2m_ctx))) {
			u32 plane = 0;

			DPRINTK(inst->dev, 1, "buf type : %4d | index : %4d\n", buf->vb2_buf.type,
				buf->vb2_buf.index);

			for (plane = 0; plane < inst->dst_fmt.num_planes; plane++)
				vb2_set_plane_payload(&buf->vb2_buf, plane, 0);

			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
		}
	}
}

static const struct vb2_ops vpu_dec_vb2_ops = {
	.queue_setup     = vpu_dec_queue_setup,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.buf_init        = vpu_dec_buf_init,
	.buf_prepare     = vpu_dec_buf_prepare,
	.buf_queue       = vpu_dec_buf_queue,
	.buf_finish      = vpu_dec_buf_finish,
	.buf_cleanup     = vpu_dec_buf_cleanup,
	.start_streaming = vpu_dec_start_streaming,
	.stop_streaming  = vpu_dec_stop_streaming,
};

static void set_default_format(struct v4l2_pix_format_mplane *src_fmt,
			       struct v4l2_pix_format_mplane *dst_fmt)
{
	src_fmt->pixelformat  = vpu_dec_fmt_list[VPU_FMT_TYPE_CODEC][0].v4l2_pix_fmt;
	src_fmt->field        = V4L2_FIELD_NONE;
	src_fmt->flags        = 0;
	src_fmt->num_planes   = vpu_dec_fmt_list[VPU_FMT_TYPE_CODEC][0].num_planes;
	update_resolution_info(src_fmt, 720, 480);

	dst_fmt->pixelformat  = vpu_dec_fmt_list[VPU_FMT_TYPE_RAW][0].v4l2_pix_fmt;
	dst_fmt->field        = V4L2_FIELD_NONE;
	dst_fmt->flags        = 0;
	dst_fmt->num_planes   = vpu_dec_fmt_list[VPU_FMT_TYPE_RAW][0].num_planes;
	update_resolution_info(dst_fmt, 736, 480);
}

static int vpu_dec_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct vpu_instance *inst = priv;
	struct vpu_platform_data *pdata = dev_get_platdata(inst->dev->v4l2_dev.dev);
	int ret;

	DPRINTK(inst->dev, 1, "\n");

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	if (pdata && pdata->mem_ops)
		src_vq->mem_ops = pdata->mem_ops;
	else
		src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->ops = &vpu_dec_vb2_ops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->buf_struct_size = sizeof(struct vpu_buffer);
	src_vq->allow_zero_bytesused = 1;
	src_vq->min_buffers_needed = 0;
	src_vq->drv_priv = inst;
	src_vq->lock = &inst->dev->dev_lock;
	src_vq->dev = inst->dev->v4l2_dev.dev;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	if (pdata && pdata->mem_ops)
		dst_vq->mem_ops = pdata->mem_ops;
	else
		dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->ops = &vpu_dec_vb2_ops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->buf_struct_size = sizeof(struct vpu_buffer);
	dst_vq->allow_zero_bytesused = 1;
	dst_vq->min_buffers_needed = 0;
	dst_vq->drv_priv = inst;
	dst_vq->lock = &inst->dev->dev_lock;
	dst_vq->dev = inst->dev->v4l2_dev.dev;
	ret = vb2_queue_init(dst_vq);
	if (ret)
		return ret;

	return 0;
}

static const struct vpu_instance_ops vpu_dec_inst_ops = {
	.start_process  = vpu_dec_start_decode,
	.stop_process   = vpu_dec_stop_decode,
	.finish_process = vpu_dec_finish_decode,
};

static int vpu_dec_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct vpu_device   *dev  = video_drvdata(filp);
	struct vpu_instance *inst = NULL;
	struct v4l2_ctrl    *ctrl;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->dev  = dev;
	inst->type = VPU_INST_TYPE_DEC;
	inst->ops  = &vpu_dec_inst_ops;

	spin_lock_init(&inst->bitstream_lock);

	v4l2_fh_init(&inst->v4l2_fh, vdev);
	filp->private_data = &inst->v4l2_fh;
	v4l2_fh_add(&inst->v4l2_fh);

	inst->v4l2_fh.m2m_ctx = v4l2_m2m_ctx_init(dev->v4l2_m2m_dev, inst, vpu_dec_queue_init);
	if (IS_ERR(inst->v4l2_fh.m2m_ctx))
		return -ENODEV;

	v4l2_ctrl_handler_init(&inst->v4l2_ctrl_hdl, 10);
	v4l2_ctrl_new_custom(&inst->v4l2_ctrl_hdl, &vpu_thumbnail_mode, NULL);
	ctrl = v4l2_ctrl_new_std(&inst->v4l2_ctrl_hdl, &vpu_dec_ctrl_ops,
				 V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (inst->v4l2_ctrl_hdl.error)
		return -ENODEV;

	inst->v4l2_fh.ctrl_handler = &inst->v4l2_ctrl_hdl;
	v4l2_ctrl_handler_setup(&inst->v4l2_ctrl_hdl);

	set_default_format(&inst->src_fmt, &inst->dst_fmt);
	inst->colorspace   = V4L2_COLORSPACE_REC709;
	inst->ycbcr_enc    = V4L2_YCBCR_ENC_DEFAULT;
	inst->hsv_enc      = 0;
	inst->quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->xfer_func    = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int vpu_dec_release(struct file *filp)
{
	int i;
	struct vpu_instance *inst = to_vpu_inst(filp->private_data);
	unsigned int loop_count = 0;

	v4l2_m2m_ctx_release(inst->v4l2_fh.m2m_ctx);
	if (inst->state != VPU_INST_STATE_NONE) {
		while (vpu_dec_close(inst) == RETCODE_VPU_STILL_RUNNING) {
			if (vpu_wait_interrupt(inst, VPU_DEC_TIMEOUT) < 0) {
				DPRINTK(inst->dev, 1, "failed to call vpu_wait_interrupt()\n");
				if (loop_count < 10) {
					loop_count++;
					continue;
				} else {
					DPRINTK(inst->dev, 1, "failed to call vpu_dec_close()\n");
					break;
				}
			} else {
				break;
			}
		}
	}
	for (i = 0; i < inst->min_dst_frame_buf_count; i++) {
		if (inst->frame_vbuf[i].size != 0)
			vdi_free_dma_memory(inst->dev, &inst->frame_vbuf[i]);
	}
	if (inst->bitstream_vbuf.size != 0)
		vdi_free_dma_memory(inst->dev, &inst->bitstream_vbuf);
	v4l2_ctrl_handler_free(&inst->v4l2_ctrl_hdl);
	v4l2_fh_del(&inst->v4l2_fh);
	v4l2_fh_exit(&inst->v4l2_fh);
	kfree(inst);

	return 0;
}

static const struct v4l2_file_operations vpu_dec_fops = {
	.owner          = THIS_MODULE,
	.open           = vpu_dec_open,
	.release        = vpu_dec_release,
	.unlocked_ioctl = video_ioctl2,
	.poll           = v4l2_m2m_fop_poll,
	.mmap           = v4l2_m2m_fop_mmap,
};

int vpu_dec_register_device(struct vpu_device *dev)
{
	struct video_device *vdev_dec;

	vdev_dec = devm_kzalloc(dev->v4l2_dev.dev, sizeof(*vdev_dec), GFP_KERNEL);
	if (!vdev_dec)
		return -ENOMEM;

	dev->video_dev_dec = vdev_dec;

	strscpy(vdev_dec->name, VPU_DEC_DEV_NAME, sizeof(vdev_dec->name));
	vdev_dec->fops        = &vpu_dec_fops;
	vdev_dec->ioctl_ops   = &vpu_dec_ioctl_ops;
	vdev_dec->release     = video_device_release_empty;
	vdev_dec->v4l2_dev    = &dev->v4l2_dev;
	vdev_dec->vfl_dir     = VFL_DIR_M2M;
	vdev_dec->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	vdev_dec->lock        = &dev->dev_lock;

	if (video_register_device(vdev_dec, VFL_TYPE_VIDEO, -1))
		return -1;

	video_set_drvdata(vdev_dec, dev);

	return 0;
}

void vpu_dec_unregister_device(struct vpu_device *dev)
{
	video_unregister_device(dev->video_dev_dec);
}

