// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "dpu.h"
#include "dpu-crtc.h"
#include "dpu-dprc.h"
#include "dpu-plane.h"

#define FRAC_16_16(mult, div)			(((mult) << 16) / (div))

#define DPU_PLANE_MAX_PITCH			0x10000
#define DPU_PLANE_MAX_PIX_CNT			8192
#define DPU_PLANE_MAX_PIX_CNT_WITH_SCALER	2048

static const uint32_t dpu_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565,

	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
};

static const uint64_t dpu_plane_format_modifiers[] = {
	DRM_FORMAT_MOD_VIVANTE_TILED,
	DRM_FORMAT_MOD_VIVANTE_SUPER_TILED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static unsigned int dpu_plane_get_default_zpos(enum drm_plane_type type)
{
	if (type == DRM_PLANE_TYPE_PRIMARY)
		return 0;
	else if (type == DRM_PLANE_TYPE_OVERLAY)
		return 1;

	return 0;
}

static void dpu_plane_destroy(struct drm_plane *plane)
{
	struct dpu_plane *dpu_plane = to_dpu_plane(plane);

	drm_plane_cleanup(plane);
	kfree(dpu_plane);
}

static void dpu_plane_reset(struct drm_plane *plane)
{
	struct dpu_plane_state *state;

	if (plane->state) {
		__drm_atomic_helper_plane_destroy_state(plane->state);
		kfree(to_dpu_plane_state(plane->state));
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return;

	__drm_atomic_helper_plane_reset(plane, &state->base);

	plane->state->zpos = dpu_plane_get_default_zpos(plane->type);
	plane->state->color_encoding = DRM_COLOR_YCBCR_BT709;
	plane->state->color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;
}

static struct drm_plane_state *
dpu_drm_atomic_plane_duplicate_state(struct drm_plane *plane)
{
	struct dpu_plane_state *state, *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	copy = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->base);
	state = to_dpu_plane_state(plane->state);
	copy->stage = state->stage;
	copy->source = state->source;
	copy->blend = state->blend;
	copy->is_top = state->is_top;

	return &copy->base;
}

static void dpu_drm_atomic_plane_destroy_state(struct drm_plane *plane,
					       struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_dpu_plane_state(state));
}

static bool dpu_drm_plane_format_mod_supported(struct drm_plane *plane,
					       uint32_t format,
					       uint64_t modifier)
{
	switch (format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return modifier == DRM_FORMAT_MOD_LINEAR;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_RGB565:
		return modifier == DRM_FORMAT_MOD_LINEAR ||
		       modifier == DRM_FORMAT_MOD_VIVANTE_TILED ||
		       modifier == DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;
	default:
		return false;
	}
}

static const struct drm_plane_funcs dpu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= dpu_plane_destroy,
	.reset			= dpu_plane_reset,
	.atomic_duplicate_state	= dpu_drm_atomic_plane_duplicate_state,
	.atomic_destroy_state	= dpu_drm_atomic_plane_destroy_state,
	.format_mod_supported	= dpu_drm_plane_format_mod_supported,
};

static inline dma_addr_t
drm_plane_state_to_baseaddr(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *cma_obj;
	unsigned int x = state->src.x1 >> 16;
	unsigned int y = state->src.y1 >> 16;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);

	if (fb->flags & DRM_MODE_FB_INTERLACED)
		y /= 2;

	return cma_obj->paddr + fb->offsets[0] + fb->pitches[0] * y +
	       fb->format->cpp[0] * x;
}

static inline dma_addr_t
drm_plane_state_to_uvbaseaddr(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *cma_obj;
	int x = state->src.x1 >> 16;
	int y = state->src.y1 >> 16;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 1);

	x /= fb->format->hsub;
	y /= fb->format->vsub;

	if (fb->flags & DRM_MODE_FB_INTERLACED)
		y /= 2;

	return cma_obj->paddr + fb->offsets[1] + fb->pitches[1] * y +
	       fb->format->cpp[1] * x;
}

static int dpu_plane_check_no_off_screen(struct drm_plane_state *state,
					 struct drm_crtc_state *crtc_state)
{
	if (state->dst.x1 < 0 || state->dst.y1 < 0 ||
	    (state->dst.x2 > crtc_state->adjusted_mode.hdisplay) ||
	    (state->dst.y2 > crtc_state->adjusted_mode.vdisplay)) {
		dpu_plane_dbg(state->plane, "no off screen\n");
		return -EINVAL;
	}

	return 0;
}

static int dpu_plane_check_max_source_resolution(struct drm_plane_state *state)
{
	u32 src_w = drm_rect_width(&state->src) >> 16;
	u32 src_h = drm_rect_height(&state->src) >> 16;
	u32 dst_w = drm_rect_width(&state->dst);
	u32 dst_h = drm_rect_height(&state->dst);

	if (src_w == dst_w || src_h == dst_h) {
		/* without scaling */
		if (src_w > DPU_PLANE_MAX_PIX_CNT ||
		    src_h > DPU_PLANE_MAX_PIX_CNT) {
			dpu_plane_dbg(state->plane,
				      "invalid source resolution\n");
			return -EINVAL;
		}
	} else {
		/* with scaling */
		if (src_w > DPU_PLANE_MAX_PIX_CNT_WITH_SCALER ||
		    src_h > DPU_PLANE_MAX_PIX_CNT_WITH_SCALER) {
			dpu_plane_dbg(state->plane,
				      "invalid source resolution with scale\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int dpu_plane_check_source_alignment(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	bool fb_is_interlaced = !!(fb->flags & DRM_MODE_FB_INTERLACED);
	u32 src_w = drm_rect_width(&state->src) >> 16;
	u32 src_h = drm_rect_height(&state->src) >> 16;
	u32 src_x = state->src.x1 >> 16;
	u32 src_y = state->src.y1 >> 16;

	if (fb->format->hsub == 2) {
		if (src_w % 2) {
			dpu_plane_dbg(state->plane, "bad uv width\n");
			return -EINVAL;
		}
		if (src_x % 2) {
			dpu_plane_dbg(state->plane, "bad uv xoffset\n");
			return -EINVAL;
		}
	}
	if (fb->format->vsub == 2) {
		if (src_h % (fb_is_interlaced ? 4 : 2)) {
			dpu_plane_dbg(state->plane, "bad uv height\n");
			return -EINVAL;
		}
		if (src_y % (fb_is_interlaced ? 4 : 2)) {
			dpu_plane_dbg(state->plane, "bad uv yoffset\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int dpu_plane_check_fb_modifier(struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_framebuffer *fb = state->fb;

	if ((fb->flags & DRM_MODE_FB_MODIFIERS) &&
	    !plane->funcs->format_mod_supported(plane, fb->format->format,
						fb->modifier)) {
		dpu_plane_dbg(plane, "invalid modifier 0x%016llx",
								fb->modifier);
		return -EINVAL;
	}

	return 0;
}

/* for tile formats, framebuffer has to be tile aligned */
static int dpu_plane_check_tiled_fb_alignment(struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_framebuffer *fb = state->fb;

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_VIVANTE_TILED:
		if (fb->width % 4) {
			dpu_plane_dbg(plane, "bad fb width for VIVANTE tile\n");
			return -EINVAL;
		}
		if (fb->height % 4) {
			dpu_plane_dbg(plane, "bad fb height for VIVANTE tile\n");
			return -EINVAL;
		}
		break;
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		if (fb->width % 64) {
			dpu_plane_dbg(plane,
				      "bad fb width for VIVANTE super tile\n");
			return -EINVAL;
		}
		if (fb->height % 64) {
			dpu_plane_dbg(plane,
				      "bad fb height for VIVANTE super tile\n");
			return -EINVAL;
		}
		break;
	}

	return 0;
}

static int dpu_plane_check_no_bt709_full_range(struct drm_plane_state *state)
{
	if (state->fb->format->is_yuv &&
	    state->color_encoding == DRM_COLOR_YCBCR_BT709 &&
	    state->color_range == DRM_COLOR_YCBCR_FULL_RANGE) {
		dpu_plane_dbg(state->plane, "no BT709 full range support\n");
		return -EINVAL;
	}

	return 0;
}

static int dpu_plane_check_fb_plane_1(struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t baseaddr = drm_plane_state_to_baseaddr(state);
	int bpp;

	/* base address alignment */
	switch (fb->format->format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		bpp = 16;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		bpp = 8;
		break;
	default:
		bpp = fb->format->cpp[0] * 8;
		break;
	}
	if (((bpp == 32) && (baseaddr & 0x3)) ||
	    ((bpp == 16) && (baseaddr & 0x1))) {
		dpu_plane_dbg(plane, "%dbpp fb bad baddr alignment\n", bpp);
		return -EINVAL;
	}
	switch (bpp) {
	case 32:
		if (baseaddr & 0x3) {
			dpu_plane_dbg(plane, "32bpp fb bad baddr alignment\n");
			return -EINVAL;
		}
		break;
	case 16:
		if (fb->modifier) {
			if (baseaddr & 0x1) {
				dpu_plane_dbg(plane,
					"16bpp tile fb bad baddr alignment\n");
				return -EINVAL;
			}
		} else {
			if (baseaddr & 0x7) {
				dpu_plane_dbg(plane,
					"16bpp fb bad baddr alignment\n");
				return -EINVAL;
			}
		}
		break;
	}

	/* pitches[0] range */
	if (fb->pitches[0] > DPU_PLANE_MAX_PITCH) {
		dpu_plane_dbg(plane, "fb pitches[0] is out of range\n");
		return -EINVAL;
	}

	/* pitches[0] alignment */
	if (((bpp == 32) && (fb->pitches[0] & 0x3)) ||
	    ((bpp == 16) && (fb->pitches[0] & 0x1))) {
		dpu_plane_dbg(plane, "%dbpp fb bad pitches[0] alignment\n", bpp);
		return -EINVAL;
	}

	return 0;
}

/* UV planar check, assuming 16bpp */
static int dpu_plane_check_fb_plane_2(struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_framebuffer *fb = state->fb;
	dma_addr_t uv_baseaddr = drm_plane_state_to_uvbaseaddr(state);

	/* base address alignment */
	if (uv_baseaddr & 0x7) {
		dpu_plane_dbg(plane, "bad uv baddr alignment\n");
		return -EINVAL;
	}

	/* pitches[1] range */
	if (fb->pitches[1] > DPU_PLANE_MAX_PITCH) {
		dpu_plane_dbg(plane, "fb pitches[1] is out of range\n");
		return -EINVAL;
	}

	/* pitches[1] alignment */
	if (fb->pitches[1] & 0x1) {
		dpu_plane_dbg(plane, "fb bad pitches[1] alignment\n");
		return -EINVAL;
	}

	return 0;
}

static int dpu_plane_check_dprc(struct drm_plane_state *state)
{
	struct dpu_plane_state *dpstate = to_dpu_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	const struct dpu_fetchunit_ops *fu_ops;
	struct dpu_dprc *dprc;
	dma_addr_t baseaddr, uv_baseaddr = 0;
	u32 src_w = drm_rect_width(&state->src) >> 16;
	u32 src_x = state->src.x1 >> 16;

	fu_ops = dpu_fu_get_ops(dpstate->source);
	dprc = fu_ops->get_dprc(dpstate->source);

	if (!dpu_dprc_rtram_width_supported(dprc, src_w)) {
		dpu_plane_dbg(state->plane, "bad RTRAM width for DPRC\n");
		return -EINVAL;
	}

	baseaddr = drm_plane_state_to_baseaddr(state);
	if (fb->format->num_planes > 1)
		uv_baseaddr = drm_plane_state_to_uvbaseaddr(state);

	if (!dpu_dprc_stride_supported(dprc, fb->pitches[0], fb->pitches[1],
				       src_w, src_x, fb->format, fb->modifier,
				       baseaddr, uv_baseaddr)) {
		dpu_plane_dbg(state->plane, "bad fb pitches for DPRC\n");
		return -EINVAL;
	}

	return 0;
}

static int dpu_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state =
				drm_atomic_get_new_plane_state(state, plane);
	struct dpu_plane_state *new_dpstate =
				to_dpu_plane_state(new_plane_state);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc_state *crtc_state;
	int min_scale, ret;

	/* ok to disable */
	if (!fb) {
		new_dpstate->source = NULL;
		new_dpstate->stage.ptr = NULL;
		new_dpstate->blend = NULL;
		return 0;
	}

	if (!new_plane_state->crtc) {
		dpu_plane_dbg(plane, "no CRTC in plane state\n");
		return -EINVAL;
	}

	crtc_state =
		drm_atomic_get_existing_crtc_state(state, new_plane_state->crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	min_scale = FRAC_16_16(1, DPU_PLANE_MAX_PIX_CNT_WITH_SCALER);
	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  min_scale,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, false);
	if (ret) {
		dpu_plane_dbg(plane, "failed to check plane state: %d\n", ret);
		return ret;
	}

	ret = dpu_plane_check_no_off_screen(new_plane_state, crtc_state);
	if (ret)
		return ret;

	ret = dpu_plane_check_max_source_resolution(new_plane_state);
	if (ret)
		return ret;

	ret = dpu_plane_check_source_alignment(new_plane_state);
	if (ret)
		return ret;

	ret = dpu_plane_check_fb_modifier(new_plane_state);
	if (ret)
		return ret;

	ret = dpu_plane_check_tiled_fb_alignment(new_plane_state);
	if (ret)
		return ret;

	ret = dpu_plane_check_no_bt709_full_range(new_plane_state);
	if (ret)
		return ret;

	ret = dpu_plane_check_fb_plane_1(new_plane_state);
	if (ret)
		return ret;

	if (fb->format->num_planes > 1) {
		ret = dpu_plane_check_fb_plane_2(new_plane_state);
		if (ret)
			return ret;
	}

	return dpu_plane_check_dprc(new_plane_state);
}

static void dpu_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct dpu_plane *dplane = to_dpu_plane(plane);
	struct drm_plane_state *new_state = plane->state;
	struct dpu_plane_state *new_dpstate = to_dpu_plane_state(new_state);
	struct dpu_plane_grp *grp = dplane->grp;
	struct dpu_crtc *dpu_crtc;
	struct drm_framebuffer *fb = new_state->fb;
	struct dpu_fetchunit *fu = new_dpstate->source;
	struct dpu_layerblend *lb = new_dpstate->blend;
	struct dpu_dprc *dprc;
	const struct dpu_fetchunit_ops *fu_ops;
	dma_addr_t baseaddr, uv_baseaddr;
	enum dpu_link_id fu_link;
	enum dpu_link_id lb_src_link, stage_link;
	enum dpu_link_id vs_link;
	unsigned int src_w, src_h, src_x, src_y, dst_w, dst_h;
	unsigned int mt_w = 0, mt_h = 0;	/* micro-tile width/height */
	int bpp;
	bool prefetch_start = false;
	bool need_fetcheco = false, need_hscaler = false, need_vscaler = false;
	bool need_modeset;
	bool fb_is_interlaced;

	/*
	 * Do nothing since the plane is disabled by
	 * crtc_func->atomic_begin/flush.
	 */
	if (!fb)
		return;

	/* Do nothing if CRTC is inactive. */
	if (!new_state->crtc->state->active)
		return;

	need_modeset = drm_atomic_crtc_needs_modeset(new_state->crtc->state);

	fb_is_interlaced = !!(fb->flags & DRM_MODE_FB_INTERLACED);

	src_w = drm_rect_width(&new_state->src) >> 16;
	src_h = drm_rect_height(&new_state->src) >> 16;
	src_x = new_state->src.x1 >> 16;
	src_y = new_state->src.y1 >> 16;
	dst_w = drm_rect_width(&new_state->dst);
	dst_h = drm_rect_height(&new_state->dst);

	switch (fb->format->format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		bpp = 16;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		bpp = 8;
		break;
	default:
		bpp = fb->format->cpp[0] * 8;
		break;
	}

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_VIVANTE_TILED:
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		mt_w = (bpp == 16) ? 8 : 4;
		mt_h = 4;
		break;
	}

	if (fb->format->num_planes > 1)
		need_fetcheco = true;

	if (src_w != dst_w)
		need_hscaler = true;

	if ((src_h != dst_h) || fb_is_interlaced)
		need_vscaler = true;

	baseaddr = drm_plane_state_to_baseaddr(new_state);
	if (need_fetcheco)
		uv_baseaddr = drm_plane_state_to_uvbaseaddr(new_state);

	dpu_crtc = to_dpu_crtc(new_state->crtc);

	fu_ops = dpu_fu_get_ops(fu);

	if (!fu_ops->has_stream_id(fu) || need_modeset)
		prefetch_start = true;

	fu_ops->set_layerblend(fu, lb);

	fu_ops->set_burstlength(fu, src_x, mt_w, bpp, baseaddr);
	fu_ops->set_src_stride(fu, src_w, src_w, mt_w, bpp, fb->pitches[0],
			       baseaddr);
	fu_ops->set_src_buf_dimensions(fu, src_w, src_h, fb->format,
				       fb_is_interlaced);
	fu_ops->set_fmt(fu, fb->format, new_state->color_encoding,
			new_state->color_range, fb_is_interlaced);
	fu_ops->set_pixel_blend_mode(fu, new_state->pixel_blend_mode,
				     new_state->alpha, fb->format->has_alpha);
	fu_ops->enable_src_buf(fu);
	fu_ops->set_framedimensions(fu, src_w, src_h, fb_is_interlaced);
	fu_ops->set_baseaddress(fu, src_w, src_x, src_y, mt_w, mt_h, bpp,
				baseaddr);
	fu_ops->set_stream_id(fu, dpu_crtc->stream_id);

	fu_link = fu_ops->get_link_id(fu);
	lb_src_link = fu_link;

	dpu_plane_dbg(plane, "uses %s\n", fu_ops->get_name(fu));

	if (need_fetcheco) {
		struct dpu_fetchunit *fe = fu_ops->get_fetcheco(fu);
		const struct dpu_fetchunit_ops *fe_ops;

		fe_ops = dpu_fu_get_ops(fe);

		fu_ops->set_pec_dynamic_src_sel(fu, fe_ops->get_link_id(fe));

		fe_ops->set_burstlength(fe, src_x, mt_w, bpp, uv_baseaddr);
		fe_ops->set_src_stride(fe, src_w, src_x, mt_w, bpp,
				       fb->pitches[1], uv_baseaddr);
		fe_ops->set_fmt(fe, fb->format, new_state->color_encoding,
				new_state->color_range, fb_is_interlaced);
		fe_ops->set_src_buf_dimensions(fe, src_w, src_h,
					       fb->format, fb_is_interlaced);
		fe_ops->set_framedimensions(fe, src_w, src_h, fb_is_interlaced);
		fe_ops->set_baseaddress(fe, src_w, src_x, src_y / 2,
					mt_w, mt_h, bpp, uv_baseaddr);
		fe_ops->enable_src_buf(fe);

		dpu_plane_dbg(plane, "uses %s\n", fe_ops->get_name(fe));
	} else {
		if (fu_ops->set_pec_dynamic_src_sel)
			fu_ops->set_pec_dynamic_src_sel(fu, LINK_ID_NONE);
	}

	/* VScaler comes first */
	if (need_vscaler) {
		struct dpu_vscaler *vs = fu_ops->get_vscaler(fu);

		dpu_vs_pec_dynamic_src_sel(vs, fu_link);
		dpu_vs_pec_clken(vs, CLKEN_AUTOMATIC);
		dpu_vs_setup1(vs, src_h, new_state->crtc_h, fb_is_interlaced);
		dpu_vs_setup2(vs, fb_is_interlaced);
		dpu_vs_setup3(vs, fb_is_interlaced);
		dpu_vs_output_size(vs, dst_h);
		dpu_vs_field_mode(vs, fb_is_interlaced ?
						SCALER_ALWAYS0 : SCALER_INPUT);
		dpu_vs_filter_mode(vs, SCALER_LINEAR);
		dpu_vs_scale_mode(vs, SCALER_UPSCALE);
		dpu_vs_mode(vs, SCALER_ACTIVE);

		vs_link = dpu_vs_get_link_id(vs);
		lb_src_link = vs_link;

		dpu_plane_dbg(plane, "uses VScaler%u\n", dpu_vs_get_id(vs));
	}

	/* and then, HScaler */
	if (need_hscaler) {
		struct dpu_hscaler *hs = fu_ops->get_hscaler(fu);

		dpu_hs_pec_dynamic_src_sel(hs, need_vscaler ? vs_link : fu_link);
		dpu_hs_pec_clken(hs, CLKEN_AUTOMATIC);
		dpu_hs_setup1(hs, src_w, dst_w);
		dpu_hs_output_size(hs, dst_w);
		dpu_hs_filter_mode(hs, SCALER_LINEAR);
		dpu_hs_scale_mode(hs, SCALER_UPSCALE);
		dpu_hs_mode(hs, SCALER_ACTIVE);

		lb_src_link = dpu_hs_get_link_id(hs);

		dpu_plane_dbg(plane, "uses HScaler%u\n", dpu_hs_get_id(hs));
	}

	dprc = fu_ops->get_dprc(fu);

	dpu_dprc_configure(dprc, dpu_crtc->stream_id,
			   src_w, src_h, src_x, src_y,
			   fb->pitches[0], fb->format, fb->modifier,
			   baseaddr, uv_baseaddr,
			   prefetch_start, fb_is_interlaced);

	if (new_state->normalized_zpos == 0)
		stage_link = dpu_cf_get_link_id(new_dpstate->stage.cf);
	else
		stage_link = dpu_lb_get_link_id(new_dpstate->stage.lb);

	dpu_lb_pec_dynamic_prim_sel(lb, stage_link);
	dpu_lb_pec_dynamic_sec_sel(lb, lb_src_link);
	dpu_lb_mode(lb, LB_BLEND);
	dpu_lb_blendcontrol(lb, new_state->normalized_zpos,
			    new_state->pixel_blend_mode, new_state->alpha);
	dpu_lb_pec_clken(lb, CLKEN_AUTOMATIC);
	dpu_lb_position(lb, new_state->dst.x1, new_state->dst.y1);

	dpu_plane_dbg(plane, "uses LayerBlend%u\n", dpu_lb_get_id(lb));

	if (new_dpstate->is_top)
		dpu_ed_pec_src_sel(grp->ed[dpu_crtc->stream_id],
				   dpu_lb_get_link_id(lb));
}

static const struct drm_plane_helper_funcs dpu_plane_helper_funcs = {
	.prepare_fb	= drm_gem_plane_helper_prepare_fb,
	.atomic_check	= dpu_plane_atomic_check,
	.atomic_update	= dpu_plane_atomic_update,
};

struct dpu_plane *dpu_plane_initialize(struct drm_device *drm,
				       unsigned int possible_crtcs,
				       struct dpu_plane_grp *grp,
				       enum drm_plane_type type)
{
	struct dpu_plane *dpu_plane;
	struct drm_plane *plane;
	unsigned int zpos = dpu_plane_get_default_zpos(type);
	int ret;

	dpu_plane = kzalloc(sizeof(*dpu_plane), GFP_KERNEL);
	if (!dpu_plane)
		return ERR_PTR(-ENOMEM);

	dpu_plane->grp = grp;

	plane = &dpu_plane->base;

	ret = drm_universal_plane_init(drm, plane, possible_crtcs,
				       &dpu_plane_funcs,
				       dpu_plane_formats,
				       ARRAY_SIZE(dpu_plane_formats),
				       dpu_plane_format_modifiers, type, NULL);
	if (ret) {
		/*
		 * The plane is not added to the global plane list, so
		 * free it manually.
		 */
		kfree(dpu_plane);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(plane, &dpu_plane_helper_funcs);

	ret = drm_plane_create_zpos_property(plane,
					     zpos, 0, grp->hw_plane_cnt - 1);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_plane_create_alpha_property(plane);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_plane_create_blend_mode_property(plane,
					BIT(DRM_MODE_BLEND_PIXEL_NONE) |
					BIT(DRM_MODE_BLEND_PREMULTI) |
					BIT(DRM_MODE_BLEND_COVERAGE));
	if (ret)
		return ERR_PTR(ret);

	ret = drm_plane_create_color_properties(plane,
					BIT(DRM_COLOR_YCBCR_BT601) |
					BIT(DRM_COLOR_YCBCR_BT709),
					BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
					BIT(DRM_COLOR_YCBCR_FULL_RANGE),
					DRM_COLOR_YCBCR_BT709,
					DRM_COLOR_YCBCR_LIMITED_RANGE);
	if (ret)
		return ERR_PTR(ret);

	return dpu_plane;
}
