/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vdrm_drv.h -- Virtual DRM driver
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#ifndef __VDRM_DRV_H__
#define __VDRM_DRV_H__

#include <drm/drm_device.h>

#include "vdrm_api.h"

struct vdrm_property {
	struct drm_property *prop;
	struct drm_property *parent_prop;
	uint64_t default_val;
};

struct vdrm_plane_info {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
	unsigned int z;
};

struct vdrm_device;

struct vdrm_display {
	struct drm_connector connector;
	struct drm_crtc crtc;
	struct drm_crtc *parent_crtc;
	struct drm_plane *plane;
	struct drm_encoder encoder;
	struct drm_pending_vblank_event *event;
	struct vdrm_device *dev;
	bool vblank_enabled;
	wait_queue_head_t flip_wait;
	bool crtc_enabled;
	int vblank_count;

	struct list_head head;

	struct vdrm_plane_info plane_info;

	const struct drm_plane_funcs *parent_plane_funcs;
	const struct drm_plane_helper_funcs *parent_plane_helper_funcs;
	struct drm_plane_funcs plane_funcs;
	struct drm_plane_helper_funcs plane_helper_funcs;
};

struct vdrm_device {
	struct drm_device ddev;
	struct drm_device *parent;

	int num_crtcs;
	struct list_head disps;

	const struct vdrm_funcs *funcs;
	struct vdrm_property *props;
	int num_props;

	struct {
		struct drm_property *offset_x;
		struct drm_property *offset_y;
		struct drm_property *width;
		struct drm_property *height;
	} plane_props;

	struct vdrm_plane_info of_plane_info;
};

static inline struct vdrm_device *to_vdrm_device(struct drm_device *dev)
{
	return container_of(dev, struct vdrm_device, ddev);
}

#endif /* __VDRM_DRV_H__ */
