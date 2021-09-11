// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_fourcc.h>

#include "loongson_drv.h"

static void loongson_plane_atomic_update(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	struct loongson_crtc *lcrtc;
	struct loongson_device *ldev;
	struct drm_plane_state *lstate = plane->state;
	u32 gpu_addr = 0;
	u32 fb_addr = 0;
	u32 reg_val = 0;
	u32 reg_offset;
	u32 pitch;
	u8 depth;
	u32 x, y;

	if (!lstate->crtc || !lstate->fb)
		return;

	pitch = lstate->fb->pitches[0];
	lcrtc = to_loongson_crtc(lstate->crtc);
	ldev = lcrtc->ldev;
	reg_offset = lcrtc->reg_offset;
	x = lstate->crtc->x;
	y = lstate->crtc->y;
	depth = lstate->fb->format->cpp[0] << 3;

	gpu_addr = loongson_gpu_offset(lstate, ldev);
	reg_val = (pitch + 255) & ~255;
	ls7a_mm_wreg(ldev, FB_STRI_REG + reg_offset, reg_val);

	switch (depth) {
	case 12 ... 16:
		fb_addr = gpu_addr + y * pitch + ALIGN(x, 64) * 2;
		break;
	case 24 ... 32:
	default:
		fb_addr = gpu_addr + y * pitch + ALIGN(x, 64) * 4;
		break;
	}

	ls7a_mm_wreg(ldev, FB_ADDR0_REG + reg_offset, fb_addr);
	ls7a_mm_wreg(ldev, FB_ADDR1_REG + reg_offset, fb_addr);
	reg_val = lcrtc->cfg_reg | CFG_ENABLE;
	ls7a_mm_wreg(ldev, FB_CFG_REG + reg_offset, reg_val);
}

static const uint32_t loongson_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const uint64_t loongson_format_modifiers[] = { DRM_FORMAT_MOD_LINEAR,
						      DRM_FORMAT_MOD_INVALID };

static const struct drm_plane_funcs loongson_plane_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = drm_atomic_helper_plane_reset,
	.update_plane = drm_atomic_helper_update_plane,
};

static const struct drm_plane_helper_funcs loongson_plane_helper_funcs = {
	.prepare_fb	= drm_gem_vram_plane_helper_prepare_fb,
	.cleanup_fb	= drm_gem_vram_plane_helper_cleanup_fb,
	.atomic_update = loongson_plane_atomic_update,
};

struct loongson_plane *loongson_plane_init(struct drm_device *dev, int index)
{
	struct loongson_plane *plane;

	plane = drmm_universal_plane_alloc(dev, struct loongson_plane, base,
					   BIT(index), &loongson_plane_funcs,
					   loongson_formats,
					   ARRAY_SIZE(loongson_formats),
					   loongson_format_modifiers,
					   DRM_PLANE_TYPE_PRIMARY, NULL);
	if (IS_ERR(plane)) {
		drm_err(dev, "failed to allocate and initialize plane\n");
		return plane;
	}

	drm_plane_helper_add(&plane->base, &loongson_plane_helper_funcs);

	return plane;
}
