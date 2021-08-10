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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/font.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>

#include "vivpu.h"
#include "vivpu-dec.h"
#include "vivpu-video.h"

unsigned int vivpu_debug;
module_param(vivpu_debug, uint, 0644);
MODULE_PARM_DESC(vivpu_debug, " activates debug info");

const unsigned int vivpu_src_default_w = 640;
const unsigned int vivpu_src_default_h = 480;
const unsigned int vivpu_src_default_depth = 8;

unsigned int vivpu_transtime;
module_param(vivpu_transtime, uint, 0644);
MODULE_PARM_DESC(vivpu_transtime, " simulated process time.");

struct v4l2_ctrl *vivpu_find_control(struct vivpu_ctx *ctx, u32 id)
{
	unsigned int i;

	for (i = 0; ctx->ctrls[i]; i++)
		if (ctx->ctrls[i]->id == id)
			return ctx->ctrls[i];

	return NULL;
}

void *vivpu_find_control_data(struct vivpu_ctx *ctx, u32 id)
{
	struct v4l2_ctrl *ctrl;

	ctrl = vivpu_find_control(ctx, id);
	if (ctrl)
		return ctrl->p_cur.p;

	return NULL;
}

u32 vivpu_control_num_elems(struct vivpu_ctx *ctx, u32 id)
{
	struct v4l2_ctrl *ctrl;

	ctrl = vivpu_find_control(ctx, id);
	if (ctrl)
		return ctrl->elems;

	return 0;
}

static void vivpu_device_release(struct video_device *vdev)
{
	struct vivpu_dev *dev = container_of(vdev, struct vivpu_dev, vfd);

	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_m2m_release(dev->m2m_dev);
	media_device_cleanup(&dev->mdev);
	kfree(dev);
}

static const struct vivpu_control vivpu_controls[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_FRAME_HEADER,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_SEQUENCE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_TILE_GROUP,
		.cfg.dims = { V4L2_AV1_MAX_TILE_COUNT },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY,
		.cfg.dims = { V4L2_AV1_MAX_TILE_COUNT },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_TILE_LIST,
		.cfg.dims = { V4L2_AV1_MAX_TILE_COUNT },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_TILE_LIST_ENTRY,
		.cfg.dims = { V4L2_AV1_MAX_TILE_COUNT },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_PROFILE,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_LEVEL,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_AV1_OPERATING_MODE,
	},
};

#define VIVPU_CONTROLS_COUNT	ARRAY_SIZE(vivpu_controls)

static int vivpu_init_ctrls(struct vivpu_ctx *ctx)
{
	struct vivpu_dev *dev = ctx->dev;
	struct v4l2_ctrl_handler *hdl = &ctx->hdl;
	struct v4l2_ctrl *ctrl;
	unsigned int ctrl_size;
	unsigned int i;

	v4l2_ctrl_handler_init(hdl, VIVPU_CONTROLS_COUNT);
	if (hdl->error) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to initialize control handler\n");
		return hdl->error;
	}

	ctrl_size = sizeof(ctrl) * VIVPU_CONTROLS_COUNT + 1;

	ctx->ctrls = kzalloc(ctrl_size, GFP_KERNEL);
	if (!ctx->ctrls)
		return -ENOMEM;

	for (i = 0; i < VIVPU_CONTROLS_COUNT; i++) {
		ctrl = v4l2_ctrl_new_custom(hdl, &vivpu_controls[i].cfg,
					    NULL);
		if (hdl->error) {
			v4l2_err(&dev->v4l2_dev,
				 "Failed to create new custom control, errno: %d\n",
				 hdl->error);

			return hdl->error;
		}

		ctx->ctrls[i] = ctrl;
	}

	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	return 0;
}

static void vivpu_free_ctrls(struct vivpu_ctx *ctx)
{
	kfree(ctx->ctrls);
	v4l2_ctrl_handler_free(&ctx->hdl);
}

static int vivpu_open(struct file *file)
{
	struct vivpu_dev *dev = video_drvdata(file);
	struct vivpu_ctx *ctx = NULL;
	int rc = 0;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto unlock;
	}

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;

	rc = vivpu_init_ctrls(ctx);
	if (rc)
		goto free_ctx;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &vivpu_queue_init);

	mutex_init(&ctx->vb_mutex);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		rc = PTR_ERR(ctx->fh.m2m_ctx);
		goto free_hdl;
	}

	vivpu_set_default_format(ctx);

	v4l2_fh_add(&ctx->fh);

	dprintk(dev, "Created instance: %p, m2m_ctx: %p\n",
		ctx, ctx->fh.m2m_ctx);

	mutex_unlock(&dev->dev_mutex);
	return rc;

free_hdl:
	vivpu_free_ctrls(ctx);
	v4l2_fh_exit(&ctx->fh);
free_ctx:
	kfree(ctx);
unlock:
	mutex_unlock(&dev->dev_mutex);
	return rc;
}

static int vivpu_release(struct file *file)
{
	struct vivpu_dev *dev = video_drvdata(file);
	struct vivpu_ctx *ctx = vivpu_file_to_ctx(file);

	dprintk(dev, "Releasing instance %p\n", ctx);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	vivpu_free_ctrls(ctx);
	mutex_lock(&dev->dev_mutex);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mutex_unlock(&dev->dev_mutex);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations vivpu_fops = {
	.owner		= THIS_MODULE,
	.open		= vivpu_open,
	.release	= vivpu_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device vivpu_videodev = {
	.name		= VIVPU_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &vivpu_fops,
	.ioctl_ops	= &vivpu_ioctl_ops,
	.minor		= -1,
	.release	= vivpu_device_release,
	.device_caps	= V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static const struct v4l2_m2m_ops vivpu_m2m_ops = {
	.device_run	= vivpu_device_run,
};

static const struct media_device_ops vivpu_m2m_media_ops = {
	.req_validate	= vivpu_request_validate,
	.req_queue	= v4l2_m2m_request_queue,
};

static int vivpu_probe(struct platform_device *pdev)
{
	struct vivpu_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto error_vivpu_dev;

	mutex_init(&dev->dev_mutex);

	dev->vfd = vivpu_videodev;
	vfd = &dev->vfd;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	video_set_drvdata(vfd, dev);
	v4l2_info(&dev->v4l2_dev,
		  "Device registered as /dev/video%d\n", vfd->num);

	platform_set_drvdata(pdev, dev);

	dev->m2m_dev = v4l2_m2m_init(&vivpu_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		dev->m2m_dev = NULL;
		goto error_dev;
	}

	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, "vivpu", sizeof(dev->mdev.model));
	strscpy(dev->mdev.bus_info, "platform:vivpu",
		sizeof(dev->mdev.bus_info));
	media_device_init(&dev->mdev);
	dev->mdev.ops = &vivpu_m2m_media_ops;
	dev->v4l2_dev.mdev = &dev->mdev;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto error_m2m;
	}

	ret = v4l2_m2m_register_media_controller(dev->m2m_dev, vfd,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem media controller\n");
		goto error_v4l2;
	}

	ret = media_device_register(&dev->mdev);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register mem2mem media device\n");
		goto error_m2m_mc;
	}

	return 0;

error_m2m_mc:
	v4l2_m2m_unregister_media_controller(dev->m2m_dev);
error_v4l2:
	video_unregister_device(&dev->vfd);
	/* vivpu_device_release called by video_unregister_device to release various objects */
	return ret;
error_m2m:
	v4l2_m2m_release(dev->m2m_dev);
error_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
error_vivpu_dev:
	kfree(dev);

	return ret;
}

static int vivpu_remove(struct platform_device *pdev)
{
	struct vivpu_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " VIVPU_NAME);

#ifdef CONFIG_MEDIA_CONTROLLER
	if (media_devnode_is_registered(dev->mdev.devnode)) {
		media_device_unregister(&dev->mdev);
		v4l2_m2m_unregister_media_controller(dev->m2m_dev);
	}
#endif
	video_unregister_device(&dev->vfd);

	return 0;
}

static struct platform_driver vivpu_pdrv = {
	.probe		= vivpu_probe,
	.remove		= vivpu_remove,
	.driver		= {
		.name	= VIVPU_NAME,
	},
};

static void vivpu_dev_release(struct device *dev) {}

static struct platform_device vivpu_pdev = {
	.name		= VIVPU_NAME,
	.dev.release	= vivpu_dev_release,
};

static void __exit vivpu_exit(void)
{
	platform_driver_unregister(&vivpu_pdrv);
	platform_device_unregister(&vivpu_pdev);
}

static int __init vivpu_init(void)
{
	int ret;

	ret = platform_device_register(&vivpu_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&vivpu_pdrv);
	if (ret)
		platform_device_unregister(&vivpu_pdev);

	return ret;
}

MODULE_DESCRIPTION("Virtual VPU device");
MODULE_AUTHOR("Daniel Almeida <daniel.almeida@collabora.com>");
MODULE_LICENSE("GPL v2");

module_init(vivpu_init);
module_exit(vivpu_exit);
