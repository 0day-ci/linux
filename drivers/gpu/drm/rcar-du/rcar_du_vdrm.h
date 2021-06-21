/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_du_vdrm.h -- R-Car Display Unit Virtual DRMs
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#ifndef __RCAR_DU_VDRM_H__
#define __RCAR_DU_VDRM_H__

#include <drm/drm_atomic.h>

#include "rcar_du_drv.h"
#include "../vdrm/vdrm_api.h"

#ifdef CONFIG_DRM_RCAR_DU_VDRM

void rcar_du_vdrm_crtc_complete(struct rcar_du_crtc *crtc, unsigned int status);
void rcar_du_vdrm_vblank_event(struct rcar_du_crtc *crtc);
int rcar_du_vdrm_count(struct rcar_du_device *rcdu);
int rcar_du_vdrms_init(struct rcar_du_device *rcdu);
int rcar_du_vdrm_plane_init(struct vdrm_device *vdrm,
			    struct rcar_du_vsp_plane *plane,
			    const struct drm_plane_funcs *funcs,
			    const struct drm_plane_helper_funcs *helper_funcs,
			    const u32 *formats, unsigned int num_formats,
			    int max_zpos);
int rcar_du_vdrm_crtc_init(struct rcar_du_crtc *crtc, int index);
int rcar_du_vdrms_register(struct rcar_du_device *rcdu);
void rcar_du_vdrms_fini(struct rcar_du_device *rcdu);

#else

static inline void
rcar_du_vdrm_crtc_complete(struct rcar_du_crtc *crtc, unsigned int status) { }
static inline void rcar_du_vdrm_vblank_event(struct rcar_du_crtc *crtc) { }
static inline int rcar_du_vdrm_count(struct rcar_du_device *rcdu)
{
	return 0;
}
static inline int rcar_du_vdrms_init(struct rcar_du_device *rcdu)
{
	return 0;
}
static inline int
rcar_du_vdrm_plane_init(struct vdrm_device *vdrm,
			struct rcar_du_vsp_plane *plane,
			const struct drm_plane_funcs *funcs,
			const struct drm_plane_helper_funcs *helper_funcs,
			const u32 *formats, unsigned int num_formats,
			int max_zpos)
{
	return 0;
}
static inline int rcar_du_vdrm_crtc_init(struct rcar_du_crtc *crtc, int index)
{
	return 0;
}
static inline int rcar_du_vdrms_register(struct rcar_du_device *rcdu)
{
	return 0;
}
static inline void rcar_du_vdrms_fini(struct rcar_du_device *rcdu) { }

#endif

#endif /* __RCAR_DU_VDRM_H__ */
