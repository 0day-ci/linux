// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera Video node.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/freezer.h>
#include <linux/kthread.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#include "keembay-cam-xlink.h"
#include "keembay-pipeline.h"
#include "keembay-video.h"
#include "keembay-vpu-frame.h"

#define KMB_CAM_VIDEO_NAME "keembay-video"

/* Xlink data channel size and timeout */
#define KMB_VID_CH_DATA_SIZE	1024
#define KMB_VID_CH_TIMEOUT_MS	5000

#define KMB_VID_MIN_WIDTH	16
#define KMB_VID_MIN_HEIGHT	16
#define KMB_VID_MAX_WIDTH	U16_MAX
#define KMB_VID_MAX_HEIGHT	U16_MAX
#define KMB_VID_STEP_WIDTH	8
#define KMB_VID_STEP_HEIGHT	8

#define to_kmb_video_buf(vbuf)	container_of(vbuf, struct kmb_frame_buffer, vb)

/* Kmb video format info structure */
struct kmb_video_fmt_info {
	const char *description;
	u32 code;
	u32 pixelformat;
	enum kmb_frame_types type;
	u32 colorspace;
	unsigned int planes;
	unsigned int bpp;
	unsigned int h_subsample;
	unsigned int v_subsample;
	bool contiguous_memory;
};

/* Supported video formats */
static const struct kmb_video_fmt_info video_formats[] = {
	{
		.description = "NV12",
		.code = MEDIA_BUS_FMT_YUYV8_1_5X8,
		.pixelformat = V4L2_PIX_FMT_NV12,
		.type = KMB_FRAME_TYPE_NV12,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.planes = 2,
		.bpp = 8,
		.h_subsample = 1,
		.v_subsample = 2,
		.contiguous_memory = true,
	},
	{
		.description = "Planar YUV 4:2:0",
		.code = MEDIA_BUS_FMT_UYYVYY8_0_5X24,
		.pixelformat = V4L2_PIX_FMT_YUV420,
		.type = KMB_FRAME_TYPE_YUV420P,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.planes = 3,
		.bpp = 8,
		.h_subsample = 2,
		.v_subsample = 2,
		.contiguous_memory = false,
	},
	{
		.description = "Planar YUV 4:4:4",
		.code = MEDIA_BUS_FMT_YUV8_1X24,
		.pixelformat = V4L2_PIX_FMT_YUV444,
		.type = KMB_FRAME_TYPE_YUV444P,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.planes = 3,
		.bpp = 8,
		.h_subsample = 1,
		.v_subsample = 1,
		.contiguous_memory = false,
	},
	{
		.description = "RAW 8 Garyscale",
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.pixelformat = V4L2_PIX_FMT_GREY,
		.type = KMB_FRAME_TYPE_RAW8,
		.colorspace = V4L2_COLORSPACE_RAW,
		.planes = 1,
		.bpp = 8,
		.h_subsample = 1,
		.v_subsample = 1,
		.contiguous_memory = false,
	},
	{
		.description = "RAW 10 Grayscale",
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.pixelformat = V4L2_PIX_FMT_Y10,
		.type = KMB_FRAME_TYPE_RAW10,
		.colorspace = V4L2_COLORSPACE_RAW,
		.planes = 1,
		.bpp = 10,
		.h_subsample = 1,
		.v_subsample = 1,
		.contiguous_memory = false,
	}
};

static const struct kmb_video_fmt_info *
kmb_video_get_fmt_info_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(video_formats); i++)
		if (video_formats[i].code == code)
			return &video_formats[i];

	return NULL;
}

static const struct kmb_video_fmt_info *
kmb_video_get_fmt_info_by_pixfmt(u32 pix_fmt)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(video_formats); i++)
		if (video_formats[i].pixelformat == pix_fmt)
			return &video_formats[i];

	return NULL;
}

/* Buffer processing operations */
static void kmb_video_insert_buf(struct kmb_video *kmb_vid,
				 struct kmb_frame_buffer *buf)
{
	INIT_LIST_HEAD(&buf->list);

	mutex_lock(&kmb_vid->dma_lock);
	list_add_tail(&buf->list, &kmb_vid->dma_queue);
	mutex_unlock(&kmb_vid->dma_lock);
}

static void __kmb_video_buf_discard(struct kmb_video *kmb_vid,
				    struct kmb_frame_buffer *buf)
{
	lockdep_assert_held(&kmb_vid->dma_lock);

	list_del(&buf->list);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
}

static int kmb_video_process_buf(struct kmb_video *kmb_vid,
				 struct kmb_frame_buffer *buf)
{
	const struct kmb_video_fmt_info *info = kmb_vid->active_fmt.info;
	struct v4l2_pix_format_mplane *pix = &kmb_vid->active_fmt.pix;
	struct kmb_vpu_frame_buffer rt_frame_buf;
	int ret;

	lockdep_assert_held(&kmb_vid->lock);

	memset(&rt_frame_buf, 0, sizeof(rt_frame_buf));
	rt_frame_buf.spec.bpp = info->bpp;
	rt_frame_buf.spec.type = info->type;
	rt_frame_buf.spec.width = pix->width;
	rt_frame_buf.spec.height = pix->height;
	rt_frame_buf.spec.stride = pix->plane_fmt[0].bytesperline;
	rt_frame_buf.p1 = buf->addr[0];

	/* Planes not used by the VPU should be set with addr 0 */
	if (pix->num_planes > 1)
		rt_frame_buf.p2 = buf->addr[1];
	if (pix->num_planes > 2)
		rt_frame_buf.p3 = buf->addr[2];

	ret = kmb_cam_xlink_write_msg(kmb_vid->xlink_cam,
				      kmb_vid->chan_id,
				      (u8 *)&rt_frame_buf,
				      sizeof(rt_frame_buf));
	if (ret < 0) {
		dev_err(kmb_vid->dma_dev, "Error on buffer queue %d", ret);
		return ret;
	}

	return 0;
}

static void kmb_video_process_all_bufs(struct kmb_video *kmb_vid)
{
	struct kmb_frame_buffer *buf;
	struct list_head *next;
	struct list_head *pos;
	int ret;

	mutex_lock(&kmb_vid->dma_lock);

	/* Discard buf is removing buffer from the list */
	list_for_each_safe(pos, next, &kmb_vid->dma_queue) {
		buf = list_entry(pos, struct kmb_frame_buffer, list);

		ret = kmb_video_process_buf(kmb_vid, buf);
		if (ret) {
			dev_err(&kmb_vid->video->dev,
				"Cannot process output buf 0x%pad",
				&buf->addr[0]);
			__kmb_video_buf_discard(kmb_vid, buf);
			continue;
		}
	}

	mutex_unlock(&kmb_vid->dma_lock);
}

static int kmb_video_queue_output_buf(struct kmb_video *kmb_vid,
				      struct kmb_frame_buffer *buf)
{
	int ret = 0;

	kmb_video_insert_buf(kmb_vid, buf);

	mutex_lock(&kmb_vid->dma_lock);

	/* Process buffers only when device is streaming */
	if (vb2_is_streaming(&kmb_vid->vb2_q)) {
		ret = kmb_video_process_buf(kmb_vid, buf);
		if (ret) {
			dev_err(&kmb_vid->video->dev,
				"Fail to process output buf 0x%pad",
				&buf->addr[0]);
			__kmb_video_buf_discard(kmb_vid, buf);
		}
	}

	mutex_unlock(&kmb_vid->dma_lock);

	return ret;
}

static void kmb_video_release_all_bufs(struct kmb_video *kmb_vid,
				       enum vb2_buffer_state state)
{
	struct list_head *next = NULL;
	struct list_head *pos = NULL;
	struct kmb_frame_buffer *buf;

	mutex_lock(&kmb_vid->dma_lock);
	list_for_each_safe(pos, next, &kmb_vid->dma_queue) {
		buf = list_entry(pos, struct kmb_frame_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	mutex_unlock(&kmb_vid->dma_lock);
}

static void kmb_video_remove_buf(struct kmb_video *kmb_vid,
				 struct kmb_frame_buffer *buf)
{
	mutex_lock(&kmb_vid->dma_lock);
	list_del(&buf->list);
	mutex_unlock(&kmb_vid->dma_lock);
}

static struct kmb_frame_buffer *
kmb_video_find_buf_by_addr(struct kmb_video *kmb_vid, uint64_t addr)
{
	struct kmb_frame_buffer *buf = NULL;
	struct list_head *node = NULL;

	mutex_lock(&kmb_vid->dma_lock);

	list_for_each(node, &kmb_vid->dma_queue) {
		buf = list_entry(node, struct kmb_frame_buffer, list);
		if (buf->addr[0] == addr) {
			mutex_unlock(&kmb_vid->dma_lock);
			return buf;
		}
	}

	mutex_unlock(&kmb_vid->dma_lock);

	return NULL;
}

static void kmb_video_fmt_info_to_pix(const struct kmb_video_fmt_info *info,
				      struct v4l2_mbus_framefmt *mbus_fmt,
				      struct v4l2_pix_format_mplane *pix)
{
	u32 bytesperline;
	u32 sizeimage;
	u32 v_sub = 1;
	u32 h_sub = 1;
	unsigned int i;

	pix->width = mbus_fmt->width;
	pix->height = mbus_fmt->height;

	pix->pixelformat = info->pixelformat;
	pix->colorspace = info->colorspace;
	pix->num_planes = info->planes;

	for (i = 0; i < pix->num_planes; i++) {
		bytesperline = pix->width * info->bpp / 8 / h_sub;

		if (pix->plane_fmt[i].bytesperline < bytesperline)
			pix->plane_fmt[i].bytesperline = bytesperline;

		sizeimage = pix->plane_fmt[i].bytesperline *
			    pix->height / v_sub;

		if (pix->plane_fmt[i].sizeimage < sizeimage)
			pix->plane_fmt[i].sizeimage = sizeimage;

		h_sub = info->h_subsample;
		v_sub = info->v_subsample;
	}
}

static int kmb_video_get_subdev_fmt(struct kmb_video *kmb_vid,
				    struct v4l2_pix_format_mplane *pix)
{
	const struct kmb_video_fmt_info *fmt_info;
	struct v4l2_subdev_format sd_fmt;
	struct v4l2_subdev *subdev;
	struct media_pad *remote;
	int ret;

	remote = media_entity_remote_pad(&kmb_vid->pad);
	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return -EINVAL;

	subdev = media_entity_to_v4l2_subdev(remote->entity);
	if (!subdev)
		return -EINVAL;

	memset(&sd_fmt, 0, sizeof(sd_fmt));
	sd_fmt.pad = remote->index;
	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &sd_fmt);
	if (ret < 0)
		return ret;

	fmt_info = kmb_video_get_fmt_info_by_code(sd_fmt.format.code);
	if (!fmt_info)
		return -EINVAL;

	kmb_video_fmt_info_to_pix(fmt_info,  &sd_fmt.format, pix);

	return 0;
}

static int kmb_video_queue_setup(struct vb2_queue *q,
				 unsigned int *num_buffers,
				 unsigned int *num_planes,
				 unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct kmb_video *kmb_vid = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane *pix = &kmb_vid->active_fmt.pix;
	unsigned int i;

	if (kmb_vid->active_fmt.info->contiguous_memory) {
		*num_planes = 1;
		for (i = 0; i < pix->num_planes; i++)
			sizes[0] += pix->plane_fmt[i].sizeimage;
	} else {
		*num_planes = pix->num_planes;
		for (i = 0; i < pix->num_planes; i++)
			sizes[i] = pix->plane_fmt[i].sizeimage;
	}

	return 0;
}

static int kmb_video_buffer_prepare(struct vb2_buffer *vb)
{
	struct kmb_video *kmb_vid = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane *pix = &kmb_vid->active_fmt.pix;
	unsigned int size_image = 0;
	unsigned int i;

	if (kmb_vid->active_fmt.info->contiguous_memory) {
		for (i = 0; i < pix->num_planes; i++)
			size_image += pix->plane_fmt[i].sizeimage;

		vb2_set_plane_payload(vb, 0, size_image);
	} else {
		for (i = 0; i < pix->num_planes; i++)
			vb2_set_plane_payload(vb, i,
					      pix->plane_fmt[i].sizeimage);
	}

	return 0;
}

static int kmb_video_buf_init(struct vb2_buffer *vb)
{
	struct kmb_video *kmb_vid = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct kmb_frame_buffer *buf = to_kmb_video_buf(vbuf);
	struct v4l2_pix_format_mplane *pix = &kmb_vid->active_fmt.pix;
	unsigned int i;

	if (kmb_vid->active_fmt.info->contiguous_memory) {
		buf->addr[0] = vb2_dma_contig_plane_dma_addr(vb, 0);
		for (i = 1; i < pix->num_planes; i++) {
			buf->addr[i] = buf->addr[i - 1] +
				pix->plane_fmt[i - 1].sizeimage;
		}
	} else {
		for (i = 0; i < pix->num_planes; i++)
			buf->addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
	}

	return 0;
}

static void kmb_video_buf_queue(struct vb2_buffer *vb)
{
	struct kmb_video *kmb_vid = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct kmb_frame_buffer *buf = to_kmb_video_buf(vbuf);
	int ret;

	ret = kmb_video_queue_output_buf(kmb_vid, buf);
	if (ret)
		dev_err(kmb_vid->dma_dev, "Fail output buf queue %d", ret);
}

static int kmb_video_worker_thread(void *video)
{
	struct kmb_vpu_frame_buffer rt_frame_buf;
	struct kmb_video *kmb_vid = video;
	struct kmb_frame_buffer *buf = NULL;
	bool stopped = false;
	int ret;

	set_freezable();

	while (!kthread_should_stop()) {
		try_to_freeze();

		if (stopped) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			continue;
		}

		memset(&rt_frame_buf, 0, sizeof(rt_frame_buf));
		ret = kmb_cam_xlink_read_msg(kmb_vid->xlink_cam,
					     kmb_vid->chan_id,
					     (u8 *)&rt_frame_buf,
					     sizeof(rt_frame_buf));
		if (ret < 0) {
			stopped = true;
			/* Continue here to enter in freeze state */
			continue;
		}

		buf = kmb_video_find_buf_by_addr(kmb_vid, rt_frame_buf.p1);
		if (buf) {
			kmb_video_remove_buf(kmb_vid, buf);

			buf->vb.vb2_buf.timestamp = rt_frame_buf.ts;
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		} else {
			dev_err(kmb_vid->dma_dev, "Ouch cannot find buff %llx",
				rt_frame_buf.p1);
		}
	}

	return 0;
}

static int kmb_video_worker_start(struct kmb_video *kmb_vid)
{
	int ret;

	ret = kmb_cam_xlink_open_channel(kmb_vid->xlink_cam, kmb_vid->chan_id);
	if (ret)
		return ret;

	kmb_vid->thread = kthread_run(kmb_video_worker_thread,
				      kmb_vid, "kmb_vnode_thread");
	if (IS_ERR(kmb_vid->thread)) {
		dev_err(&kmb_vid->video->dev, "Cannot start thread");
		ret = -ENOMEM;
		kmb_vid->thread = NULL;
		goto error_close_xlink_channel;
	}

	return 0;

error_close_xlink_channel:
	kmb_cam_xlink_close_channel(kmb_vid->xlink_cam, kmb_vid->chan_id);

	return ret;
}

static int kmb_video_worker_stop(struct kmb_video *kmb_vid)
{
	int ret;

	/*
	 * Xlink has no functionality to unblock read volatile function,
	 * only way to unblock is to close the channel.
	 */
	kmb_cam_xlink_close_channel(kmb_vid->xlink_cam, kmb_vid->chan_id);
	if (!kmb_vid->thread) {
		dev_warn(&kmb_vid->video->dev, "No thread running");
		return 0;
	}

	ret = kthread_stop(kmb_vid->thread);
	if (ret < 0)
		dev_err(&kmb_vid->video->dev, "Thread stop failed %d", ret);

	kmb_vid->thread = NULL;

	return ret;
}

static int kmb_video_capture_start_streaming(struct vb2_queue *q,
					     unsigned int count)
{
	struct kmb_video *kmb_vid = vb2_get_drv_priv(q);
	int ret;

	ret = kmb_pipe_prepare(kmb_vid->pipe);
	if (ret < 0)
		goto error_discard_all_bufs;

	ret = kmb_video_worker_start(kmb_vid);
	if (ret < 0)
		goto error_pipeline_stop;

	/* Process all pending buffers after worker is started */
	kmb_video_process_all_bufs(kmb_vid);

	/*
	 * Run the pipeline after all buffers are provided for processing,
	 * the main reason is to not skip any frame from the source.
	 */
	ret = kmb_pipe_run(kmb_vid->pipe, &kmb_vid->video->entity);
	if (ret < 0)
		goto error_pipeline_stop;

	return 0;

error_pipeline_stop:
	kmb_pipe_stop(kmb_vid->pipe, &kmb_vid->video->entity);
error_discard_all_bufs:
	kmb_video_release_all_bufs(kmb_vid, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void kmb_video_capture_stop_streaming(struct vb2_queue *q)
{
	struct kmb_video *kmb_vid = vb2_get_drv_priv(q);

	kmb_pipe_stop(kmb_vid->pipe, &kmb_vid->video->entity);

	kmb_video_worker_stop(kmb_vid);

	kmb_video_release_all_bufs(kmb_vid, VB2_BUF_STATE_ERROR);
}

/* driver-specific operations */
static const struct vb2_ops kmb_video_vb2_q_capture_ops = {
	.queue_setup     = kmb_video_queue_setup,
	.buf_prepare     = kmb_video_buffer_prepare,
	.buf_init        = kmb_video_buf_init,
	.buf_queue       = kmb_video_buf_queue,
	.start_streaming = kmb_video_capture_start_streaming,
	.stop_streaming  = kmb_video_capture_stop_streaming,
};

static int kmb_video_querycap(struct file *file, void *fh,
			      struct v4l2_capability *cap)
{
	cap->bus_info[0] = 0;
	strscpy(cap->driver, KMB_CAM_VIDEO_NAME, sizeof(cap->driver));
	strscpy(cap->card, KMB_CAM_VIDEO_NAME, sizeof(cap->card));

	return 0;
}

static int kmb_video_enum_fmt(struct file *file, void *fh,
			      struct v4l2_fmtdesc *f)
{
	const struct kmb_video_fmt_info *info;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type))
		return -EINVAL;

	if (f->mbus_code) {
		if (f->index != 0)
			return -EINVAL;

		info = kmb_video_get_fmt_info_by_code(f->mbus_code);
		if (!info)
			return -EINVAL;
	} else {
		info = &video_formats[f->index];
		if (!info)
			return -EINVAL;
	}

	f->pixelformat = info->pixelformat;
	f->mbus_code = info->code;
	strscpy(f->description, info->description, sizeof(f->description));

	return 0;
}

static int kmb_video_enum_framesizes(struct file *file, void *fh,
				     struct v4l2_frmsizeenum *fsize)
{
	const struct kmb_video_fmt_info *info;

	if (fsize->index != 0)
		return -EINVAL;

	info = kmb_video_get_fmt_info_by_pixfmt(fsize->pixel_format);
	if (!info)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = KMB_VID_MIN_WIDTH;
	fsize->stepwise.max_width = KMB_VID_MAX_WIDTH;
	fsize->stepwise.step_width = KMB_VID_STEP_WIDTH;
	fsize->stepwise.min_height = KMB_VID_MIN_HEIGHT;
	fsize->stepwise.max_height = KMB_VID_MAX_HEIGHT;
	fsize->stepwise.step_height = KMB_VID_STEP_HEIGHT;

	return 0;
}

static int kmb_video_try_fmt(struct file *file, void *fh,
			     struct v4l2_format *f)
{
	const struct kmb_video_fmt_info *info;
	struct v4l2_mbus_framefmt mbus_fmt;

	info = kmb_video_get_fmt_info_by_pixfmt(f->fmt.pix_mp.pixelformat);
	if (!info)
		info = &video_formats[0];

	mbus_fmt.width = f->fmt.pix_mp.width;
	mbus_fmt.height = f->fmt.pix_mp.height;
	kmb_video_fmt_info_to_pix(info, &mbus_fmt, &f->fmt.pix_mp);

	return 0;
}

static int kmb_video_set_fmt(struct file *file, void *fh,
			     struct v4l2_format *f)
{
	struct kmb_video *kmb_vid = video_drvdata(file);
	const struct kmb_video_fmt_info *info;
	struct v4l2_mbus_framefmt mbus_fmt;

	info = kmb_video_get_fmt_info_by_pixfmt(f->fmt.pix_mp.pixelformat);
	if (!info)
		info = &video_formats[0];

	mbus_fmt.width = f->fmt.pix_mp.width;
	mbus_fmt.height = f->fmt.pix_mp.height;
	kmb_video_fmt_info_to_pix(info, &mbus_fmt, &f->fmt.pix_mp);

	kmb_vid->active_fmt.pix = f->fmt.pix_mp;
	kmb_vid->active_fmt.info = info;

	return 0;
}

static int kmb_video_get_fmt(struct file *file, void *fh,
			     struct v4l2_format *f)
{
	struct kmb_video *kmb_vid = video_drvdata(file);

	f->fmt.pix_mp = kmb_vid->active_fmt.pix;

	return 0;
}

static int kmb_video_check_format(struct kmb_video *kmb_vid)
{
	int ret;
	struct v4l2_pix_format_mplane pix;

	ret = kmb_video_get_subdev_fmt(kmb_vid, &pix);
	if (ret < 0)
		return ret;

	if (kmb_vid->active_fmt.pix.pixelformat != pix.pixelformat ||
	    kmb_vid->active_fmt.pix.height != pix.height ||
	    kmb_vid->active_fmt.pix.width != pix.width ||
	    kmb_vid->active_fmt.pix.num_planes != pix.num_planes) {
		dev_err(&kmb_vid->video->dev, "Pix fmt mismatch:\n\t"
			"pix_fmt %u %u\n\theight %u %u\n\twidth %u %u\n\t"
			"num_planes %u %u",
			kmb_vid->active_fmt.pix.pixelformat, pix.pixelformat,
			kmb_vid->active_fmt.pix.height, pix.height,
			kmb_vid->active_fmt.pix.width, pix.width,
			kmb_vid->active_fmt.pix.num_planes, pix.num_planes);
		ret =  -EINVAL;
	}

	return ret;
}

static int kmb_video_streamon(struct file *file, void *fh,
			      enum v4l2_buf_type type)
{
	struct kmb_video *kmb_vid = video_drvdata(file);
	int ret;

	if (type != kmb_vid->vb2_q.type)
		return -EINVAL;

	ret =  kmb_video_check_format(kmb_vid);
	if (ret < 0)
		return ret;

	return vb2_streamon(&kmb_vid->vb2_q, type);
}

/* V4L2 ioctl operations */
static const struct v4l2_ioctl_ops kmb_vid_ioctl_ops = {
	.vidioc_querycap                 = kmb_video_querycap,
	.vidioc_enum_fmt_vid_cap         = kmb_video_enum_fmt,
	.vidioc_enum_framesizes          = kmb_video_enum_framesizes,
	.vidioc_g_fmt_vid_cap_mplane     = kmb_video_get_fmt,
	.vidioc_try_fmt_vid_cap_mplane   = kmb_video_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane     = kmb_video_set_fmt,
	.vidioc_reqbufs                  = vb2_ioctl_reqbufs,
	.vidioc_querybuf                 = vb2_ioctl_querybuf,
	.vidioc_qbuf                     = vb2_ioctl_qbuf,
	.vidioc_dqbuf                    = vb2_ioctl_dqbuf,
	.vidioc_streamon                 = kmb_video_streamon,
	.vidioc_streamoff                = vb2_ioctl_streamoff,
	.vidioc_expbuf                   = vb2_ioctl_expbuf,
};

static int kmb_video_open(struct file *file)
{
	struct kmb_video *kmb_vid = video_drvdata(file);
	struct v4l2_mbus_framefmt fmt;
	int ret;

	mutex_lock(&kmb_vid->lock);
	ret = v4l2_fh_open(file);
	if (ret) {
		mutex_unlock(&kmb_vid->lock);
		return ret;
	}

	INIT_LIST_HEAD(&kmb_vid->dma_queue);

	ret = kmb_pipe_request(kmb_vid->pipe);
	if (ret < 0)
		goto error_fh_release;

	/* Fill default format. */
	memset(&fmt, 0, sizeof(fmt));
	kmb_video_fmt_info_to_pix(&video_formats[0], &fmt,
				  &kmb_vid->active_fmt.pix);
	kmb_vid->active_fmt.info = &video_formats[0];

	mutex_unlock(&kmb_vid->lock);

	return 0;

error_fh_release:
	_vb2_fop_release(file, NULL);
	mutex_unlock(&kmb_vid->lock);

	return ret;
}

static int kmb_video_release(struct file *file)
{
	struct kmb_video *kmb_vid = video_drvdata(file);
	int ret;

	mutex_lock(&kmb_vid->lock);

	kmb_pipe_release(kmb_vid->pipe);

	ret = _vb2_fop_release(file, NULL);

	mutex_unlock(&kmb_vid->lock);

	return ret;
}

/* FS operations for V4L2 device */
static const struct v4l2_file_operations kmb_vid_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open           = kmb_video_open,
	.release        = kmb_video_release,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
};

/**
 * kmb_video_init - Initialize entity
 * @kmb_vid: pointer to kmb video device
 * @name: entity name
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_video_init(struct kmb_video *kmb_vid, const char *name)
{
	int ret;

	kmb_vid->video = video_device_alloc();
	if (!kmb_vid->video) {
		dev_err(&kmb_vid->video->dev,
			"Failed to allocate video device");
		return -ENOMEM;
	}

	mutex_init(&kmb_vid->lock);
	mutex_init(&kmb_vid->dma_lock);

	kmb_vid->video->fops  = &kmb_vid_fops;
	kmb_vid->video->ioctl_ops = &kmb_vid_ioctl_ops;
	kmb_vid->video->minor = -1;
	kmb_vid->video->release  = video_device_release;
	kmb_vid->video->vfl_type = VFL_TYPE_VIDEO;
	kmb_vid->video->lock = &kmb_vid->lock;
	kmb_vid->video->queue = &kmb_vid->vb2_q;
	video_set_drvdata(kmb_vid->video, kmb_vid);
	snprintf(kmb_vid->video->name, sizeof(kmb_vid->video->name),
		 "kmb_video %s", name);

	kmb_vid->vb2_q.drv_priv = kmb_vid;
	kmb_vid->vb2_q.ops = &kmb_video_vb2_q_capture_ops;
	kmb_vid->vb2_q.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	kmb_vid->vb2_q.buf_struct_size = sizeof(struct kmb_frame_buffer);
	kmb_vid->vb2_q.io_modes = VB2_MMAP | VB2_DMABUF;
	kmb_vid->vb2_q.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	kmb_vid->vb2_q.mem_ops = &vb2_dma_contig_memops;
	kmb_vid->vb2_q.dev = kmb_vid->dma_dev;
	kmb_vid->vb2_q.lock = &kmb_vid->lock;
	kmb_vid->vb2_q.min_buffers_needed = 1;

	kmb_vid->pad.flags = MEDIA_PAD_FL_SINK;
	kmb_vid->video->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
				      V4L2_CAP_STREAMING | V4L2_CAP_IO_MC;

	ret = media_entity_pads_init(&kmb_vid->video->entity, 1, &kmb_vid->pad);
	if (ret < 0)
		goto error_mutex_destroy;

	ret = vb2_queue_init(&kmb_vid->vb2_q);
	if (ret < 0) {
		dev_err(&kmb_vid->video->dev, "Failed to init vb2 queue");
		goto error_video_cleanup;
	}

	return 0;

error_video_cleanup:
	kmb_video_cleanup(kmb_vid);
error_mutex_destroy:
	mutex_destroy(&kmb_vid->lock);
	mutex_destroy(&kmb_vid->dma_lock);

	return ret;
}

/**
 * kmb_video_cleanup - Free resources associated with entity
 * @kmb_vid: pointer to kmb video device
 */
void kmb_video_cleanup(struct kmb_video *kmb_vid)
{
	media_entity_cleanup(&kmb_vid->video->entity);
	mutex_destroy(&kmb_vid->lock);
	mutex_destroy(&kmb_vid->dma_lock);
}

/**
 * kmb_video_register - Register V4L2 device
 * @kmb_vid: pointer to kmb video device
 * @v4l2_dev: pointer to V4L2 device drivers
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_video_register(struct kmb_video *kmb_vid,
		       struct v4l2_device *v4l2_dev)
{
	int ret;

	kmb_vid->video->v4l2_dev = v4l2_dev;
	ret = video_register_device(kmb_vid->video, VFL_TYPE_VIDEO, -1);
	if (ret < 0)
		dev_err(&kmb_vid->video->dev, "Failed to register video device");

	return ret;
}

/**
 * kmb_video_unregister - Unregister V4L device
 * @kmb_vid: pointer to kmb video device
 */
void kmb_video_unregister(struct kmb_video *kmb_vid)
{
	video_unregister_device(kmb_vid->video);
}
