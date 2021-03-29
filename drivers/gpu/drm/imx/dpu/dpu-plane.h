/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2017-2020 NXP
 */

#ifndef __DPU_PLANE_H__
#define __DPU_PLANE_H__

#include <linux/kernel.h>

#include <drm/drm_device.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "dpu.h"

#define dpu_plane_dbg(plane, fmt, ...)					\
	drm_dbg_kms((plane)->dev, "[PLANE:%d:%s] " fmt,			\
		    (plane)->base.id, (plane)->name, ##__VA_ARGS__)

struct dpu_plane {
	struct drm_plane	base;
	struct dpu_plane_grp	*grp;
};

union dpu_plane_stage {
	struct dpu_constframe	*cf;
	struct dpu_layerblend	*lb;
	void			*ptr;
};

struct dpu_plane_state {
	struct drm_plane_state	base;
	union dpu_plane_stage	stage;
	struct dpu_fetchunit	*source;
	struct dpu_layerblend	*blend;
	bool			is_top;
};

static inline struct dpu_plane *to_dpu_plane(struct drm_plane *plane)
{
	return container_of(plane, struct dpu_plane, base);
}

static inline struct dpu_plane_state *
to_dpu_plane_state(struct drm_plane_state *plane_state)
{
	return container_of(plane_state, struct dpu_plane_state, base);
}

struct dpu_plane *dpu_plane_initialize(struct drm_device *drm,
				       unsigned int possible_crtcs,
				       struct dpu_plane_grp *grp,
				       enum drm_plane_type type);
#endif /* __DPU_PLANE_H__ */
