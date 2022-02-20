// SPDX-License-Identifier: GPL-2.0
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_vblank.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_damage_helper.h>

#include "lsdc_drv.h"
#include "lsdc_regs.h"
#include "lsdc_pll.h"

static const u32 lsdc_primary_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const u32 lsdc_cursor_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static const u64 lsdc_fb_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static void lsdc_update_fb_format(struct lsdc_device *ldev,
				  struct drm_crtc *crtc,
				  const struct drm_format_info *fmt_info)
{
	unsigned int index = drm_crtc_index(crtc);
	u32 val = 0;
	u32 fmt;

	switch (fmt_info->format) {
	case DRM_FORMAT_RGB565:
		fmt = LSDC_PF_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
		fmt = LSDC_PF_XRGB8888;
		break;
	case DRM_FORMAT_ARGB8888:
		fmt = LSDC_PF_XRGB8888;
		break;
	default:
		fmt = LSDC_PF_XRGB8888;
		break;
	}

	if (index == 0) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC0_CFG_REG);
		val = (val & ~CFG_PIX_FMT_MASK) | fmt;
		lsdc_reg_write32(ldev, LSDC_CRTC0_CFG_REG, val);
	} else if (index == 1) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC1_CFG_REG);
		val = (val & ~CFG_PIX_FMT_MASK) | fmt;
		lsdc_reg_write32(ldev, LSDC_CRTC1_CFG_REG, val);
	}
}

static void lsdc_update_fb_start_addr(struct lsdc_device *ldev,
				      struct drm_crtc *crtc,
				      u64 paddr)
{
	unsigned int index = drm_crtc_index(crtc);
	u32 addr_reg;
	u32 cfg_reg;
	u32 val;

	/*
	 * Find which framebuffer address register should update.
	 * if FB_ADDR0_REG is in using, we write the addr to FB_ADDR1_REG,
	 * if FB_ADDR1_REG is in using, we write the addr to FB_ADDR0_REG
	 */
	if (index == 0) {
		/* CRTC0 */
		val = lsdc_reg_read32(ldev, LSDC_CRTC0_CFG_REG);

		cfg_reg = LSDC_CRTC0_CFG_REG;

		if (val & CFG_FB_IDX_BIT) {
			addr_reg = LSDC_CRTC0_FB_ADDR0_REG;
			drm_dbg(&ldev->drm, "CRTC0 FB0 will be use\n");
		} else {
			addr_reg = LSDC_CRTC0_FB_ADDR1_REG;
			drm_dbg(&ldev->drm, "CRTC0 FB1 will be use\n");
		}
	} else if (index == 1) {
		/* CRTC1 */
		val = lsdc_reg_read32(ldev, LSDC_CRTC1_CFG_REG);

		cfg_reg = LSDC_CRTC1_CFG_REG;

		if (val & CFG_FB_IDX_BIT) {
			addr_reg = LSDC_CRTC1_FB_ADDR0_REG;
			drm_dbg(&ldev->drm, "CRTC1 FB0 will be use\n");
		} else {
			addr_reg = LSDC_CRTC1_FB_ADDR1_REG;
			drm_dbg(&ldev->drm, "CRTC1 FB1 will be use\n");
		}
	}

	lsdc_reg_write32(ldev, addr_reg, paddr);

	/*
	 * Then, we triger the fb switch, the switch of the framebuffer
	 * to be scanout will complete at the next vblank.
	 */
	lsdc_reg_write32(ldev, cfg_reg, val | CFG_PAGE_FLIP_BIT);

	drm_dbg(&ldev->drm, "crtc%u scantout from 0x%llx\n", index, paddr);
}

static unsigned int lsdc_get_fb_offset(struct drm_framebuffer *fb,
				       struct drm_plane_state *state,
				       unsigned int plane)
{
	unsigned int offset = fb->offsets[plane];

	offset += fb->format->cpp[plane] * (state->src_x >> 16);
	offset += fb->pitches[plane] * (state->src_y >> 16);

	return offset;
}

static s64 lsdc_get_vram_bo_offset(struct drm_framebuffer *fb)
{
	struct drm_gem_vram_object *gbo;
	s64 gpu_addr;

	gbo = drm_gem_vram_of_gem(fb->obj[0]);
	gpu_addr = drm_gem_vram_offset(gbo);

	return gpu_addr;
}

static int lsdc_pixpll_atomic_check(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct lsdc_display_pipe *dispipe = container_of(crtc, struct lsdc_display_pipe, crtc);
	struct lsdc_pll *pixpll = &dispipe->pixpll;
	const struct lsdc_pixpll_funcs *pfuncs = pixpll->funcs;
	struct lsdc_crtc_state *priv_state = to_lsdc_crtc_state(state);
	bool ret = pfuncs->compute(pixpll, state->mode.clock, &priv_state->pparams);

	if (ret)
		return 0;

	drm_warn(crtc->dev, "failed find PLL parameters for %u\n", state->mode.clock);

	return -EINVAL;
}

static int lsdc_primary_plane_atomic_check(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_framebuffer *old_fb = old_plane_state->fb;
	struct drm_crtc *crtc = new_plane_state->crtc;
	u32 new_format = new_fb->format->format;
	struct drm_crtc_state *new_crtc_state;
	int ret;

	if (!crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (WARN_ON(!new_crtc_state))
		return -EINVAL;

	/*
	 * Require full modeset if enabling or disabling a plane,
	 * or changing its position, size, depth or format.
	 */
	if ((!new_fb || !old_fb ||
	     old_plane_state->crtc_x != new_plane_state->crtc_x ||
	     old_plane_state->crtc_y != new_plane_state->crtc_y ||
	     old_plane_state->crtc_w != new_plane_state->crtc_w ||
	     old_plane_state->crtc_h != new_plane_state->crtc_h ||
	     old_fb->format->format != new_format))
		new_crtc_state->mode_changed = true;

	if (new_crtc_state->mode_changed) {
		ret = lsdc_pixpll_atomic_check(crtc, new_crtc_state);
		if (ret != 0)
			return ret;
	}

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   new_crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, true);
}

static void lsdc_update_stride(struct lsdc_device *ldev,
			       struct drm_crtc *crtc,
			       unsigned int stride)
{
	unsigned int index = drm_crtc_index(crtc);

	if (index == 0)
		lsdc_reg_write32(ldev, LSDC_CRTC0_STRIDE_REG, stride);
	else if (index == 1)
		lsdc_reg_write32(ldev, LSDC_CRTC1_STRIDE_REG, stride);

	drm_dbg(&ldev->drm, "update stride to %u\n", stride);
}

static void lsdc_primary_plane_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct lsdc_device *ldev = to_lsdc(plane->dev);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_framebuffer *fb = new_plane_state->fb;
	u32 fb_offset = lsdc_get_fb_offset(fb, new_plane_state, 0);
	dma_addr_t fb_addr;

	if (ldev->use_vram_helper) {
		s64 gpu_addr;

		gpu_addr = lsdc_get_vram_bo_offset(fb);
		if (gpu_addr < 0)
			return;

		fb_addr = ldev->vram_base + gpu_addr + fb_offset;
	} else {
		struct drm_gem_cma_object *obj = drm_fb_cma_get_gem_obj(fb, 0);

		fb_addr = obj->paddr + fb_offset;
	}

	lsdc_update_fb_start_addr(ldev, crtc, fb_addr);

	lsdc_update_stride(ldev, crtc, fb->pitches[0]);

	if (drm_atomic_crtc_needs_modeset(crtc->state))
		lsdc_update_fb_format(ldev, crtc, fb->format);
}

static void lsdc_primary_plane_atomic_disable(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	drm_dbg(plane->dev, "%s disabled\n", plane->name);
}

static int lsdc_plane_prepare_fb(struct drm_plane *plane,
				 struct drm_plane_state *new_state)
{
	struct lsdc_device *ldev = to_lsdc(plane->dev);

	if (ldev->use_vram_helper)
		return drm_gem_vram_plane_helper_prepare_fb(plane, new_state);

	return drm_gem_plane_helper_prepare_fb(plane, new_state);
}

static void lsdc_plane_cleanup_fb(struct drm_plane *plane,
				  struct drm_plane_state *old_state)
{
	struct drm_device *ddev = plane->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);

	if (ldev->use_vram_helper)
		return drm_gem_vram_plane_helper_cleanup_fb(plane, old_state);
}

static const struct drm_plane_helper_funcs lsdc_primary_plane_helpers = {
	.prepare_fb = lsdc_plane_prepare_fb,
	.cleanup_fb = lsdc_plane_cleanup_fb,
	.atomic_check = lsdc_primary_plane_atomic_check,
	.atomic_update = lsdc_primary_plane_atomic_update,
	.atomic_disable = lsdc_primary_plane_atomic_disable,
};

static int lsdc_cursor_atomic_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_crtc_state *crtc_state;
	int ret;

	/* no need for further checks if the plane is being disabled */
	if (!crtc || !fb)
		return 0;

	if (!new_plane_state->visible)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state,
						   new_plane_state->crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state,
						  crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true,
						  true);
	if (ret)
		return ret;

	if (fb->width < LSDC_CURS_MIN_SIZE ||
	    fb->height < LSDC_CURS_MIN_SIZE ||
	    fb->width > LSDC_CURS_MAX_SIZE ||
	    fb->height > LSDC_CURS_MAX_SIZE) {
		drm_err(plane->dev, "Invalid cursor size: %dx%d\n",
			fb->width, fb->height);
		return -EINVAL;
	}

	return 0;
}

static void lsdc_cursor_update_location(struct lsdc_device *ldev,
					struct drm_crtc *crtc)
{
	u32 val;

	val = lsdc_reg_read32(ldev, LSDC_CURSOR_CFG_REG);

	if ((val & CURSOR_FORMAT_MASK) == 0)
		val |= CURSOR_FORMAT_ARGB8888;

	/* Update the location of the cursor
	 * if bit 4 of LSDC_CURSOR_CFG_REG is 1, then the cursor will be
	 * locate at CRTC1, if bit 4 of LSDC_CURSOR_CFG_REG is 0, then
	 * the cursor will be locate at CRTC0.
	 */
	if (drm_crtc_index(crtc))
		val |= CURSOR_LOCATION_BIT;

	lsdc_reg_write32(ldev, LSDC_CURSOR_CFG_REG, val);
}

/* update the position of the cursor */
static void lsdc_cursor_update_position(struct lsdc_device *ldev, int x, int y)
{
	if (x < 0)
		x = 0;

	if (y < 0)
		y = 0;

	lsdc_reg_write32(ldev, LSDC_CURSOR_POSITION_REG, (y << 16) | x);
}

static void lsdc_cursor_atomic_update(struct drm_plane *plane,
				      struct drm_atomic_state *state)
{
	struct drm_device *ddev = plane->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_framebuffer *old_fb = old_plane_state->fb;

	if (new_fb != old_fb) {
		u64 cursor_addr;

		if (ldev->use_vram_helper) {
			s64 offset;

			offset = lsdc_get_vram_bo_offset(new_fb);
			cursor_addr = ldev->vram_base + offset;

			drm_dbg_kms(ddev, "%s offset: %llx\n", plane->name, offset);
		} else {
			struct drm_gem_cma_object *cursor_obj;

			cursor_obj = drm_fb_cma_get_gem_obj(new_fb, 0);
			if (!cursor_obj)
				return;

			cursor_addr = cursor_obj->paddr;
		}

		lsdc_reg_write32(ldev, LSDC_CURSOR_ADDR_REG, cursor_addr);
	}

	lsdc_cursor_update_position(ldev, new_plane_state->crtc_x, new_plane_state->crtc_y);

	lsdc_cursor_update_location(ldev, new_plane_state->crtc);
}

static void lsdc_cursor_atomic_disable(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct drm_device *ddev = plane->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_plane_state *old_plane_state;
	struct drm_crtc *crtc;

	old_plane_state = drm_atomic_get_old_plane_state(state, plane);

	if (old_plane_state)
		crtc = old_plane_state->crtc;

	lsdc_reg_write32(ldev, LSDC_CURSOR_CFG_REG, 0);

	drm_dbg(ddev, "%s disabled\n", plane->name);
}

static const struct drm_plane_helper_funcs lsdc_cursor_plane_helpers = {
	.prepare_fb = lsdc_plane_prepare_fb,
	.cleanup_fb = lsdc_plane_cleanup_fb,
	.atomic_check = lsdc_cursor_atomic_check,
	.atomic_update = lsdc_cursor_atomic_update,
	.atomic_disable = lsdc_cursor_atomic_disable,
};

static int lsdc_plane_get_default_zpos(enum drm_plane_type type)
{
	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		return 0;
	case DRM_PLANE_TYPE_OVERLAY:
		return 1;
	case DRM_PLANE_TYPE_CURSOR:
		return 7;
	}
	return 0;
}

static void lsdc_plane_reset(struct drm_plane *plane)
{
	drm_atomic_helper_plane_reset(plane);

	plane->state->zpos = lsdc_plane_get_default_zpos(plane->type);

	drm_dbg(plane->dev, "%s reset\n", plane->name);
}

static const struct drm_plane_funcs lsdc_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = lsdc_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

int lsdc_plane_init(struct lsdc_device *ldev,
		    struct drm_plane *plane,
		    enum drm_plane_type type,
		    unsigned int index)
{
	struct drm_device *ddev = &ldev->drm;
	int zpos = lsdc_plane_get_default_zpos(type);
	unsigned int format_count;
	const u32 *formats;
	const char *name;
	int ret;

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		formats = lsdc_primary_formats;
		format_count = ARRAY_SIZE(lsdc_primary_formats);
		name = "primary-%u";
		break;
	case DRM_PLANE_TYPE_CURSOR:
		formats = lsdc_cursor_formats;
		format_count = ARRAY_SIZE(lsdc_cursor_formats);
		name = "cursor-%u";
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		drm_err(ddev, "overlay plane is not supported\n");
		break;
	}

	ret = drm_universal_plane_init(ddev, plane, 1 << index,
				       &lsdc_plane_funcs,
				       formats, format_count,
				       lsdc_fb_format_modifiers,
				       type, name, index);
	if (ret) {
		drm_err(ddev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		drm_plane_helper_add(plane, &lsdc_primary_plane_helpers);
		drm_plane_create_zpos_property(plane, zpos, 0, 6);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		drm_plane_helper_add(plane, &lsdc_cursor_plane_helpers);
		drm_plane_create_zpos_immutable_property(plane, zpos);
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		drm_err(ddev, "overlay plane is not supported\n");
		break;
	}

	drm_plane_create_alpha_property(plane);

	return 0;
}
