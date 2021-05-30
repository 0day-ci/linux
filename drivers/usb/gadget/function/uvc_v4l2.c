// SPDX-License-Identifier: GPL-2.0+
/*
 *	uvc_v4l2.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/usb/g_uvc.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "f_uvc.h"
#include "uvc.h"
#include "uvc_queue.h"
#include "uvc_video.h"
#include "uvc_v4l2.h"
#include "u_uvc.h"
#include "uvc_configfs.h"

u32 uvc_v4l2_get_bytesperline(struct uvcg_format *fmt, struct uvcg_frame *frm)
{
	struct uvcg_uncompressed *u;

	switch (fmt->type) {
	case UVCG_UNCOMPRESSED:
		u = to_uvcg_uncompressed(&fmt->group.cg_item);
		if (!u)
			return 0;

		return u->desc.bBitsPerPixel * frm->frame.w_width / 8;
	case UVCG_MJPEG:
		return frm->frame.w_width;
	}

	return 0;
}

struct uvcg_frame *find_frame_by_index(struct uvc_device *uvc,
					      struct uvcg_format *ufmt,
					      int index)
{
	int i;

	for (i = 0; i < uvc->nframes; i++) {
		if (uvc->frm[i]->fmt_type != ufmt->type)
			continue;

		if (index == uvc->frm[i]->frame.b_frame_index)
			break;
	}

	if (i == uvc->nframes)
		return NULL;

	return uvc->frm[i];
}

static struct uvcg_format *find_format_by_pix(struct uvc_device *uvc,
						    u32 pixelformat)
{
	int i;

	for (i = 0; i < uvc->nformats; i++)
		if (uvc->fmt[i]->fcc == pixelformat)
			break;

	if (i == uvc->nformats)
		return NULL;

	return uvc->fmt[i];
}

int uvc_frame_default(struct uvcg_format *ufmt)
{
	struct uvcg_uncompressed *m;
	struct uvcg_uncompressed *u;
	int ret = 1;

	switch (ufmt->type) {
	case UVCG_UNCOMPRESSED:
		u = to_uvcg_uncompressed(&ufmt->group.cg_item);
		if (u)
			ret = u->desc.bDefaultFrameIndex;
		break;
	case UVCG_MJPEG:
		m = to_uvcg_uncompressed(&ufmt->group.cg_item);
		if (m)
			ret = m->desc.bDefaultFrameIndex;
		break;
	}

	if (!ret)
		ret = 1;

	return ret;
}

static struct uvcg_frame *find_frm_by_size(struct uvc_device *uvc,
					   struct uvcg_format *ufmt,
					   u16 rw, u16 rh)
{
	struct uvc_video *video = &uvc->video;
	struct uvcg_frame *ufrm = NULL;
	unsigned int d, maxd;
	int i;

	/* Find the closest image size. The distance between image sizes is
	 * the size in pixels of the non-overlapping regions between the
	 * requested size and the frame-specified size.
	 */
	maxd = (unsigned int)-1;

	for (i = 0; i < uvc->nframes; i++) {
		u16 w, h;

		if (uvc->frm[i]->fmt_type != ufmt->type)
			continue;

		w = uvc->frm[i]->frame.w_width;
		h = uvc->frm[i]->frame.w_height;

		d = min(w, rw) * min(h, rh);
		d = w*h + rw*rh - 2*d;
		if (d < maxd) {
			maxd = d;
			ufrm = uvc->frm[i];
		}

		if (maxd == 0)
			break;
	}

	if (ufrm == NULL)
		uvcg_dbg(&video->uvc->func, "Unsupported size %ux%u\n", rw, rh);

	return ufrm;
}

/* --------------------------------------------------------------------------
 * Requests handling
 */

static int
uvc_send_response(struct uvc_device *uvc, struct uvc_request_data *data)
{
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	struct usb_request *req = uvc->control_req;

	if (data->length < 0)
		return usb_ep_set_halt(cdev->gadget->ep0);

	req->length = min_t(unsigned int, uvc->event_length, data->length);
	req->zero = data->length < uvc->event_length;

	memcpy(req->buf, data->data, req->length);

	return usb_ep_queue(cdev->gadget->ep0, req, GFP_KERNEL);
}

/* --------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int
uvc_v4l2_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct usb_composite_dev *cdev = uvc->func.config->cdev;

	strlcpy(cap->driver, "g_uvc", sizeof(cap->driver));
	strlcpy(cap->card, cdev->gadget->name, sizeof(cap->card));
	strlcpy(cap->bus_info, dev_name(&cdev->gadget->dev),
		sizeof(cap->bus_info));
	return 0;
}

static int
uvc_v4l2_get_format(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	fmt->fmt.pix.pixelformat = video->cur_format->fcc;
	fmt->fmt.pix.width = video->cur_frame->frame.w_width;
	fmt->fmt.pix.height = video->cur_frame->frame.w_height;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = uvc_v4l2_get_bytesperline(video->cur_format, video->cur_frame);
	fmt->fmt.pix.sizeimage = video->cur_frame->frame.dw_max_video_frame_buffer_size;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;

	return 0;
}

static int _uvc_v4l2_try_fmt(struct uvc_video *video,
	struct v4l2_format *fmt, struct uvcg_format **uvc_format, struct uvcg_frame **uvc_frame)
{
	struct uvc_device *uvc = video->uvc;
	struct uvcg_format *ufmt;
	struct uvcg_frame *ufrm;
	u8 *fcc;
	int i;

	if (fmt->type != video->queue.queue.type)
		return -EINVAL;

	fcc = (u8 *)&fmt->fmt.pix.pixelformat;
	uvcg_dbg(&uvc->func, "Trying format 0x%08x (%c%c%c%c): %ux%u\n",
		fmt->fmt.pix.pixelformat,
		fcc[0], fcc[1], fcc[2], fcc[3],
		fmt->fmt.pix.width, fmt->fmt.pix.height);

	for (i = 0; i < uvc->nformats; i++)
		if (uvc->fmt[i]->fcc == fmt->fmt.pix.pixelformat)
			break;

	if (i == uvc->nformats)
		ufmt = video->def_format;

	ufmt = uvc->fmt[i];

	ufrm = find_frm_by_size(uvc, ufmt,
				fmt->fmt.pix.width, fmt->fmt.pix.height);
	if (!ufrm)
		return -EINVAL;

	fmt->fmt.pix.width = ufrm->frame.w_width;
	fmt->fmt.pix.height = ufrm->frame.w_height;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = uvc_v4l2_get_bytesperline(ufmt, ufrm);
	fmt->fmt.pix.sizeimage = ufrm->frame.dw_max_video_frame_buffer_size;
	fmt->fmt.pix.pixelformat = ufmt->fcc;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;

	if (!fmt->fmt.pix.sizeimage && fmt->fmt.pix.bytesperline)
		fmt->fmt.pix.sizeimage = fmt->fmt.pix.bytesperline *
				fmt->fmt.pix.height;

	if (uvc_format != NULL)
		*uvc_format = ufmt;
	if (uvc_frame != NULL)
		*uvc_frame = ufrm;

	return 0;
}

static int
uvc_v4l2_try_fmt(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	return _uvc_v4l2_try_fmt(video, fmt, NULL, NULL);
}

static int
uvc_v4l2_set_format(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	struct uvcg_format *ufmt;
	struct uvcg_frame *ufrm;
	int ret;

	ret = _uvc_v4l2_try_fmt(video, fmt, &ufmt, &ufrm);
	if (ret)
		return ret;

	video->cur_format = ufmt;
	video->cur_frame = ufrm;

	return ret;
}

static int
uvc_v4l2_enum_frameintervals(struct file *file, void *fh,
		struct v4l2_frmivalenum *fival)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvcg_format *ufmt = NULL;
	struct uvcg_frame *ufrm = NULL;
	int i;

	ufmt = find_format_by_pix(uvc, fival->pixel_format);
	if (!ufmt)
		return -EINVAL;

	for (i = 0; i < uvc->nframes; ++i) {
		if (uvc->frm[i]->fmt_type != ufmt->type)
			continue;

		if (uvc->frm[i]->frame.w_width == fival->width &&
				uvc->frm[i]->frame.w_height == fival->height) {
			ufrm = uvc->frm[i];
			break;
		}
	}
	if (!ufrm)
		return -EINVAL;

	if (fival->index >= ufrm->frame.b_frame_interval_type)
		return -EINVAL;

	/* TODO: handle V4L2_FRMIVAL_TYPE_STEPWISE */
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = ufrm->dw_frame_interval[fival->index];
	fival->discrete.denominator = 10000000;
	v4l2_simplify_fraction(&fival->discrete.numerator,
		&fival->discrete.denominator, 8, 333);

	return 0;
}

static int
uvc_v4l2_enum_framesizes(struct file *file, void *fh,
		struct v4l2_frmsizeenum *fsize)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvcg_format *ufmt = NULL;
	struct uvcg_frame *ufrm = NULL;

	ufmt = find_format_by_pix(uvc, fsize->pixel_format);
	if (!ufmt)
		return -EINVAL;

	if (fsize->index >= ufmt->num_frames)
		return -EINVAL;

	ufrm = find_frame_by_index(uvc, ufmt, fsize->index + 1);
	if (!ufrm)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = ufrm->frame.w_width;
	fsize->discrete.height = ufrm->frame.w_height;

	return 0;
}

static int
uvc_v4l2_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvcg_format *ufmt;

	if (f->index >= uvc->nformats)
		return -EINVAL;

	ufmt = uvc->fmt[f->index];
	if (!ufmt)
		return -EINVAL;

	f->pixelformat = ufmt->fcc;
	f->flags |= V4L2_FMT_FLAG_COMPRESSED;

	strscpy(f->description, ufmt->name, sizeof(f->description));
	f->description[sizeof(f->description) - 1] = 0;

	return 0;
}

static int
uvc_v4l2_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	if (b->type != video->queue.queue.type)
		return -EINVAL;

	return uvcg_alloc_buffers(&video->queue, b);
}

static int
uvc_v4l2_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	return uvcg_query_buffer(&video->queue, b);
}

static int
uvc_v4l2_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret;

	ret = uvcg_queue_buffer(&video->queue, b);
	if (ret < 0)
		return ret;

	schedule_work(&video->pump);

	return ret;
}

static int
uvc_v4l2_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	return uvcg_dequeue_buffer(&video->queue, b, file->f_flags & O_NONBLOCK);
}

static int
uvc_v4l2_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret;

	if (type != video->queue.queue.type)
		return -EINVAL;

	/* Enable UVC video. */
	ret = uvcg_video_enable(video, 1);
	if (ret < 0)
		return ret;

	/*
	 * Complete the alternate setting selection setup phase now that
	 * userspace is ready to provide video frames.
	 */
	uvc_function_setup_continue(uvc);
	uvc->state = UVC_STATE_STREAMING;

	return 0;
}

static int
uvc_v4l2_streamoff(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	if (type != video->queue.queue.type)
		return -EINVAL;

	return uvcg_video_enable(video, 0);
}

static int
uvc_v4l2_subscribe_event(struct v4l2_fh *fh,
			 const struct v4l2_event_subscription *sub)
{
	if (sub->type < UVC_EVENT_FIRST || sub->type > UVC_EVENT_LAST)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 2, NULL);
}

static int
uvc_v4l2_unsubscribe_event(struct v4l2_fh *fh,
			   const struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

static long
uvc_v4l2_ioctl_default(struct file *file, void *fh, bool valid_prio,
		       unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	switch (cmd) {
	case UVCIOC_SEND_RESPONSE:
		return uvc_send_response(uvc, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

const struct v4l2_ioctl_ops uvc_v4l2_ioctl_ops = {
	.vidioc_querycap = uvc_v4l2_querycap,
	.vidioc_try_fmt_vid_out = uvc_v4l2_try_fmt,
	.vidioc_g_fmt_vid_out = uvc_v4l2_get_format,
	.vidioc_s_fmt_vid_out = uvc_v4l2_set_format,
	.vidioc_enum_frameintervals = uvc_v4l2_enum_frameintervals,
	.vidioc_enum_framesizes = uvc_v4l2_enum_framesizes,
	.vidioc_enum_fmt_vid_out = uvc_v4l2_enum_fmt,
	.vidioc_reqbufs = uvc_v4l2_reqbufs,
	.vidioc_querybuf = uvc_v4l2_querybuf,
	.vidioc_qbuf = uvc_v4l2_qbuf,
	.vidioc_dqbuf = uvc_v4l2_dqbuf,
	.vidioc_streamon = uvc_v4l2_streamon,
	.vidioc_streamoff = uvc_v4l2_streamoff,
	.vidioc_subscribe_event = uvc_v4l2_subscribe_event,
	.vidioc_unsubscribe_event = uvc_v4l2_unsubscribe_event,
	.vidioc_default = uvc_v4l2_ioctl_default,
};

/* --------------------------------------------------------------------------
 * V4L2
 */

static int
uvc_v4l2_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	v4l2_fh_init(&handle->vfh, vdev);
	v4l2_fh_add(&handle->vfh);

	handle->device = &uvc->video;
	file->private_data = &handle->vfh;

	uvc_function_connect(uvc);
	return 0;
}

static int
uvc_v4l2_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle = to_uvc_file_handle(file->private_data);
	struct uvc_video *video = handle->device;

	uvc_function_disconnect(uvc);

	mutex_lock(&video->mutex);
	uvcg_video_enable(video, 0);
	uvcg_free_buffers(&video->queue);
	mutex_unlock(&video->mutex);

	file->private_data = NULL;
	v4l2_fh_del(&handle->vfh);
	v4l2_fh_exit(&handle->vfh);
	kfree(handle);

	return 0;
}

static int
uvc_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvcg_queue_mmap(&uvc->video.queue, vma);
}

static __poll_t
uvc_v4l2_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvcg_queue_poll(&uvc->video.queue, file, wait);
}

#ifndef CONFIG_MMU
static unsigned long uvcg_v4l2_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvcg_queue_get_unmapped_area(&uvc->video.queue, pgoff);
}
#endif

const struct v4l2_file_operations uvc_v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= uvc_v4l2_open,
	.release	= uvc_v4l2_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= uvc_v4l2_mmap,
	.poll		= uvc_v4l2_poll,
#ifndef CONFIG_MMU
	.get_unmapped_area = uvcg_v4l2_get_unmapped_area,
#endif
};

