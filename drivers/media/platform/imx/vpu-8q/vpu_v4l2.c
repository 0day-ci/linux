// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#define TAG		"V4L2"
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
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
#include "vpu_core.h"
#include "vpu_v4l2.h"
#include "vpu_msgs.h"
#include "vpu_helpers.h"
#include "vpu_log.h"

void vpu_inst_lock(struct vpu_inst *inst)
{
	mutex_lock(&inst->lock);
}

void vpu_inst_unlock(struct vpu_inst *inst)
{
	mutex_unlock(&inst->lock);
}

dma_addr_t vpu_get_vb_phy_addr(struct vb2_buffer *vb, u32 plane_no)
{
	return vb2_dma_contig_plane_dma_addr(vb, plane_no) +
			vb->planes[plane_no].data_offset;
}

unsigned int vpu_get_vb_length(struct vb2_buffer *vb, u32 plane_no)
{
	if (plane_no >= vb->num_planes)
		return 0;
	return vb2_plane_size(vb, plane_no) - vb->planes[plane_no].data_offset;
}

void vpu_v4l2_set_error(struct vpu_inst *inst)
{
	struct vb2_queue *src_q;
	struct vb2_queue *dst_q;

	src_q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	if (src_q)
		src_q->error = 1;
	if (dst_q)
		dst_q->error = 1;
}

int vpu_notify_eos(struct vpu_inst *inst)
{
	const struct v4l2_event ev = {
		.id = 0,
		.type = V4L2_EVENT_EOS
	};

	inst_dbg(inst, LVL_FLOW, "notify eos event\n");
	v4l2_event_queue_fh(&inst->fh, &ev);

	return 0;
}

int vpu_notify_source_change(struct vpu_inst *inst)
{
	const struct v4l2_event ev = {
		.id = 0,
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION
	};

	inst_dbg(inst, LVL_FLOW, "notify source change event\n");
	v4l2_event_queue_fh(&inst->fh, &ev);
	return 0;
}

int vpu_notify_skip(struct vpu_inst *inst)
{
	const struct v4l2_event ev = {
		.id = 0,
		.type = V4L2_EVENT_SKIP,
		.u.data[0] = 0xff,
	};

	inst_dbg(inst, LVL_FLOW, "notify skip event\n");
	v4l2_event_queue_fh(&inst->fh, &ev);

	return 0;
}

int vpu_notify_codec_error(struct vpu_inst *inst)
{
	const struct v4l2_event ev = {
		.id = 0,
		.type = V4L2_EVENT_CODEC_ERROR,
	};

	inst_dbg(inst, LVL_FLOW, "notify error event\n");
	v4l2_event_queue_fh(&inst->fh, &ev);
	vpu_v4l2_set_error(inst);

	return 0;
}

const struct vpu_format *vpu_try_fmt_common(struct vpu_inst *inst,
		 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	u32 type = f->type;
	u32 stride;
	u32 bytesperline;
	u32 sizeimage;
	const struct vpu_format *fmt;
	int i;

	fmt = vpu_helper_find_format(inst, type, pixmp->pixelformat);
	if (!fmt) {
		fmt = vpu_helper_enum_format(inst, type, 0);
		if (!fmt)
			return NULL;
		pixmp->pixelformat = fmt->pixfmt;
	}

	stride = inst->core->res->stride;
	pixmp->width = vpu_helper_valid_frame_width(inst, pixmp->width);
	pixmp->height = vpu_helper_valid_frame_height(inst, pixmp->height);
	pixmp->flags = fmt->flags;
	pixmp->num_planes = fmt->num_planes;
	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;
	for (i = 0; i < pixmp->num_planes; i++) {
		sizeimage = vpu_helper_get_plane_size(pixmp->pixelformat,
				pixmp->width, pixmp->height, i, stride,
				pixmp->field == V4L2_FIELD_INTERLACED ? 1 : 0,
				&bytesperline);
		if ((s32)(pixmp->plane_fmt[i].bytesperline) <= 0)
			pixmp->plane_fmt[i].bytesperline = bytesperline;
		if ((s32)(pixmp->plane_fmt[i].sizeimage) <= 0)
			pixmp->plane_fmt[i].sizeimage = sizeimage;
		if (pixmp->plane_fmt[i].bytesperline < bytesperline)
			pixmp->plane_fmt[i].bytesperline = bytesperline;
		if (pixmp->plane_fmt[i].sizeimage <= sizeimage)
			pixmp->plane_fmt[i].sizeimage = sizeimage;
	}

	return fmt;
}

static bool vpu_check_ready(struct vpu_inst *inst, u32 type)
{
	if (!inst)
		return false;
	if (inst->state == VPU_CODEC_STATE_DEINIT || inst->id < 0)
		return false;
	if (!inst->ops->check_ready)
		return true;
	return call_vop(inst, check_ready, type);
}

int vpu_process_output_buffer(struct vpu_inst *inst)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vpu_vb2_buffer *vpu_buf = NULL;

	if (!inst)
		return -EINVAL;

	if (!vpu_check_ready(inst, inst->out_format.type))
		return -EINVAL;

	v4l2_m2m_for_each_src_buf(inst->m2m_ctx, buf) {
		vpu_buf = container_of(buf, struct vpu_vb2_buffer, m2m_buf);
		if (vpu_buf->state == VPU_BUF_STATE_IDLE)
			break;
		vpu_buf = NULL;
	}

	if (!vpu_buf)
		return -EINVAL;

	inst_dbg(inst, LVL_DEBUG, "frame id = %d / %d\n",
			vpu_buf->m2m_buf.vb.sequence, inst->sequence);
	return call_vop(inst, process_output, &vpu_buf->m2m_buf.vb.vb2_buf);
}

int vpu_process_capture_buffer(struct vpu_inst *inst)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vpu_vb2_buffer *vpu_buf = NULL;

	if (!inst)
		return -EINVAL;

	if (!vpu_check_ready(inst, inst->cap_format.type))
		return -EINVAL;

	v4l2_m2m_for_each_dst_buf(inst->m2m_ctx, buf) {
		vpu_buf = container_of(buf, struct vpu_vb2_buffer, m2m_buf);
		if (vpu_buf->state == VPU_BUF_STATE_IDLE)
			break;
		vpu_buf = NULL;
	}
	if (!vpu_buf)
		return -EINVAL;

	return call_vop(inst, process_capture, &vpu_buf->m2m_buf.vb.vb2_buf);
}

struct vb2_v4l2_buffer *vpu_find_buf_by_sequence(struct vpu_inst *inst,
						u32 type, u32 sequence)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vb2_v4l2_buffer *vbuf = NULL;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		v4l2_m2m_for_each_src_buf(inst->m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->sequence == sequence)
				break;
			vbuf = NULL;
		}
	} else {
		v4l2_m2m_for_each_dst_buf(inst->m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->sequence == sequence)
				break;
			vbuf = NULL;
		}
	}

	return vbuf;
}

struct vb2_v4l2_buffer *vpu_find_buf_by_idx(struct vpu_inst *inst,
						u32 type, u32 idx)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vb2_v4l2_buffer *vbuf = NULL;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		v4l2_m2m_for_each_src_buf(inst->m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->vb2_buf.index == idx)
				break;
			vbuf = NULL;
		}
	} else {
		v4l2_m2m_for_each_dst_buf(inst->m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->vb2_buf.index == idx)
				break;
			vbuf = NULL;
		}
	}

	return vbuf;
}

int vpu_get_num_buffers(struct vpu_inst *inst, u32 type)
{
	struct vb2_queue *q;

	if (!inst || !inst->m2m_ctx)
		return -EINVAL;
	if (V4L2_TYPE_IS_OUTPUT(type))
		q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	else
		q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);

	return q->num_buffers;
}

static void vpu_m2m_device_run(void *priv)
{
}

static void vpu_m2m_job_abort(void *priv)
{
	struct vpu_inst *inst = priv;

	v4l2_m2m_job_finish(inst->m2m_dev, inst->m2m_ctx);
}

static const struct v4l2_m2m_ops vpu_m2m_ops = {
	.device_run = vpu_m2m_device_run,
	.job_abort = vpu_m2m_job_abort
};

static int vpu_vb2_queue_setup(struct vb2_queue *vq,
				unsigned int *buf_count,
				unsigned int *plane_count,
				unsigned int psize[],
				struct device *allocators[])
{
	struct vpu_inst *inst = vb2_get_drv_priv(vq);
	struct vpu_format *cur_fmt;
	int i;

	cur_fmt = vpu_get_format(inst, vq->type);

	if (*plane_count) {
		if (*plane_count != cur_fmt->num_planes)
			return -EINVAL;
		for (i = 0; i < cur_fmt->num_planes; i++) {
			if (psize[i] < cur_fmt->sizeimage[i])
				return -EINVAL;
		}
	}

	*plane_count = cur_fmt->num_planes;
	for (i = 0; i < cur_fmt->num_planes; i++)
		psize[i] = cur_fmt->sizeimage[i];

	inst_dbg(inst, LVL_FLOW, "%s queue setup : %u; %u, %u\n",
			vpu_type_name(vq->type),
			*buf_count,
			psize[0], psize[1]);

	return 0;
}

static int vpu_vb2_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_vb2_buffer *vpu_buf = to_vpu_vb2_buffer(vbuf);

	vpu_buf->state = VPU_BUF_STATE_IDLE;

	return 0;
}

static void vpu_vb2_buf_cleanup(struct vb2_buffer *vb)
{
}

static int vpu_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct vpu_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_vb2_buffer *vpu_buf = to_vpu_vb2_buffer(vbuf);
	struct vpu_format *cur_fmt;
	u32 i;

	cur_fmt = vpu_get_format(inst, vb->type);
	if (vb->num_planes != cur_fmt->num_planes)
		return -EINVAL;
	for (i = 0; i < cur_fmt->num_planes; i++) {
		if (vpu_get_vb_length(vb, i) < cur_fmt->sizeimage[i]) {
			inst_err(inst, "%s buf[%d] is invalid\n",
					vpu_type_name(vb->type),
					vb->index);
			vpu_buf->state = VPU_BUF_STATE_ERROR;
		}
	}

	return 0;
}

static void vpu_vb2_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *q = vb->vb2_queue;

	if (vbuf->flags & V4L2_BUF_FLAG_LAST)
		vpu_notify_eos(inst);

	if (list_empty(&q->done_list))
		call_vop(inst, on_queue_empty, q->type);
}

void vpu_vb2_buffers_return(struct vpu_inst *inst,
		unsigned int type, enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *buf;

	if (!inst || !inst->m2m_ctx)
		return;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		while ((buf = v4l2_m2m_src_buf_remove(inst->m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(inst->m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	}
}

static int vpu_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpu_inst *inst = vb2_get_drv_priv(q);
	int ret;

	vpu_inst_unlock(inst);
	ret = vpu_inst_register(inst);
	vpu_inst_lock(inst);
	if (ret)
		return ret;

	vpu_inst_get(inst);
	inst_dbg(inst, LVL_FLOW, "%s start streaming : %d\n",
			vpu_type_name(q->type), q->num_buffers);
	call_vop(inst, start, q->type);
	vb2_clear_last_buffer_dequeued(q);

	return 0;
}

static void vpu_vb2_stop_streaming(struct vb2_queue *q)
{
	struct vpu_inst *inst = vb2_get_drv_priv(q);

	inst_dbg(inst, LVL_FLOW, "%s stop streaming\n", vpu_type_name(q->type));

	call_vop(inst, stop, q->type);
	vpu_vb2_buffers_return(inst, q->type, VB2_BUF_STATE_ERROR);
	if (V4L2_TYPE_IS_OUTPUT(q->type))
		inst->sequence = 0;

	vpu_inst_put(inst);
}

static void vpu_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_inst *inst = vb2_get_drv_priv(vb->vb2_queue);

	inst_dbg(inst, LVL_DEBUG, "%s buf queue\n", vpu_type_name(vb->type));

	if (V4L2_TYPE_IS_OUTPUT(vb->type)) {
		vbuf->sequence = inst->sequence++;
		if ((s64)vb->timestamp < 0)
			vb->timestamp = VPU_INVALID_TIMESTAMP;
	}

	v4l2_m2m_buf_queue(inst->m2m_ctx, vbuf);
	vpu_process_output_buffer(inst);
	vpu_process_capture_buffer(inst);
}

static struct vb2_ops vpu_vb2_ops = {
	.queue_setup        = vpu_vb2_queue_setup,
	.buf_init           = vpu_vb2_buf_init,
	.buf_cleanup        = vpu_vb2_buf_cleanup,
	.buf_prepare        = vpu_vb2_buf_prepare,
	.buf_finish         = vpu_vb2_buf_finish,
	.start_streaming    = vpu_vb2_start_streaming,
	.stop_streaming     = vpu_vb2_stop_streaming,
	.buf_queue          = vpu_vb2_buf_queue,
	.wait_prepare       = vb2_ops_wait_prepare,
	.wait_finish        = vb2_ops_wait_finish,
};

static int vpu_m2m_queue_init(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq)
{
	struct vpu_inst *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = &vpu_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	if (inst->type == VPU_CORE_TYPE_DEC && inst->use_stream_buffer)
		src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->drv_priv = inst;
	src_vq->buf_struct_size = sizeof(struct vpu_vb2_buffer);
	src_vq->allow_zero_bytesused = 1;
	src_vq->min_buffers_needed = 1;
	src_vq->dev = inst->core->dev;
	src_vq->lock = &inst->lock;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &vpu_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	if (inst->type == VPU_CORE_TYPE_ENC && inst->use_stream_buffer)
		dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->drv_priv = inst;
	dst_vq->buf_struct_size = sizeof(struct vpu_vb2_buffer);
	dst_vq->allow_zero_bytesused = 1;
	dst_vq->min_buffers_needed = 1;
	dst_vq->dev = inst->core->dev;
	dst_vq->lock = &inst->lock;
	ret = vb2_queue_init(dst_vq);
	if (ret) {
		vb2_queue_release(src_vq);
		return ret;
	}

	return 0;
}

static int vpu_v4l2_release(struct vpu_inst *inst)
{
	struct vpu_core *core = inst->core;

	inst_dbg(inst, LVL_FLOW, "%s\n", __func__);

	vpu_release_core(core);

	if (inst->workqueue) {
		cancel_work_sync(&inst->msg_work);
		destroy_workqueue(inst->workqueue);
		inst->workqueue = NULL;
	}
	if (inst->m2m_ctx) {
		v4l2_m2m_ctx_release(inst->m2m_ctx);
		inst->m2m_ctx = NULL;
	}
	if (inst->m2m_dev) {
		v4l2_m2m_release(inst->m2m_dev);
		inst->m2m_dev = NULL;
	}

	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	mutex_destroy(&inst->lock);
	v4l2_fh_del(&inst->fh);
	v4l2_fh_exit(&inst->fh);

	call_vop(inst, cleanup);

	return 0;
}

int vpu_v4l2_open(struct file *file, struct vpu_inst *inst)
{
	struct vpu_dev *vpu = video_drvdata(file);
	struct video_device *vdev;
	struct vpu_core *core = NULL;
	int ret = 0;

	WARN_ON(!file || !inst || !inst->ops);

	if (inst->type == VPU_CORE_TYPE_ENC)
		vdev = vpu->vdev_enc;
	else
		vdev = vpu->vdev_dec;

	mutex_init(&inst->lock);
	INIT_LIST_HEAD(&inst->cmd_q);

	inst->id = VPU_INST_NULL_ID;
	inst->release = vpu_v4l2_release;
	inst->core = vpu_request_core(vpu, inst->type);

	core = inst->core;
	if (!core) {
		vpu_err("there is no core for %s\n",
			vpu_core_type_desc(inst->type));
		return -EINVAL;
	}

	inst->min_buffer_cap = 2;
	inst->min_buffer_out = 2;

	ret = call_vop(inst, ctrl_init);
	if (ret)
		goto error;

	inst->m2m_dev = v4l2_m2m_init(&vpu_m2m_ops);
	if (IS_ERR(inst->m2m_dev)) {
		vpu_err("v4l2_m2m_init fail\n");
		ret = PTR_ERR(inst->m2m_dev);
		goto error;
	}

	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev,
					inst, vpu_m2m_queue_init);
	if (IS_ERR(inst->m2m_ctx)) {
		vpu_err("v4l2_m2m_ctx_init fail\n");
		ret = PTR_ERR(inst->m2m_dev);
		goto error;
	}

	v4l2_fh_init(&inst->fh, vdev);
	v4l2_fh_add(&inst->fh);
	inst->fh.ctrl_handler = &inst->ctrl_handler;
	inst->fh.m2m_ctx = inst->m2m_ctx;
	file->private_data = &inst->fh;
	inst->state = VPU_CODEC_STATE_DEINIT;
	inst->workqueue = alloc_workqueue("vpu_inst", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (inst->workqueue) {
		INIT_WORK(&inst->msg_work, vpu_inst_run_work);
		ret = kfifo_init(&inst->msg_fifo,
				inst->msg_buffer,
				roundup_pow_of_two(sizeof(inst->msg_buffer)));
		if (ret) {
			destroy_workqueue(inst->workqueue);
			inst->workqueue = NULL;
		}
	}
	atomic_set(&inst->ref_count, 0);
	vpu_inst_get(inst);
	vpu_dbg(LVL_FLOW, "open, tgid = %d, pid = %d\n", inst->tgid, inst->pid);

	return 0;
error:
	if (inst->m2m_ctx) {
		v4l2_m2m_ctx_release(inst->m2m_ctx);
		inst->m2m_ctx = NULL;
	}
	if (inst->m2m_dev) {
		v4l2_m2m_release(inst->m2m_dev);
		inst->m2m_dev = NULL;
	}
	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	vpu_release_core(inst->core);

	return ret;
}

int vpu_v4l2_close(struct file *file)
{
	struct vpu_inst *inst = to_inst(file);
	struct vb2_queue *src_q;
	struct vb2_queue *dst_q;

	inst_dbg(inst, LVL_FLOW, "close\n");
	src_q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	vpu_inst_lock(inst);
	if (vb2_is_streaming(src_q))
		v4l2_m2m_streamoff(file, inst->m2m_ctx, src_q->type);
	if (vb2_is_streaming(dst_q))
		v4l2_m2m_streamoff(file, inst->m2m_ctx, dst_q->type);
	vpu_inst_unlock(inst);

	call_vop(inst, release);
	vpu_inst_unregister(inst);
	vpu_inst_put(inst);

	return 0;
}
