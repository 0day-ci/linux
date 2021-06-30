// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"

static void try_each_loopc(u32 clk, u32 pstdiv, u32 frefc,
			   struct pix_pll *pll_config)
{
	u32 loopc;
	u32 clk_out;
	u32 precision;
	u32 min = 1000;
	u32 base_clk = 100000L;

	for (loopc = LOOPC_MIN; loopc < LOOPC_MAX; loopc++) {
		if ((loopc < FRE_REF_MIN * frefc) ||
		    (loopc > FRE_REF_MAX * frefc))
			continue;

		clk_out = base_clk * loopc / frefc;
		precision = (clk > clk_out) ? (clk - clk_out) : (clk_out - clk);
		if (precision < min) {
			pll_config->l2_div = pstdiv;
			pll_config->l1_loopc = loopc;
			pll_config->l1_frefc = frefc;
		}
	}
}

static void cal_freq(u32 pixclock, struct pix_pll *pll_config)
{
	u32 pstdiv;
	u32 frefc;
	u32 clk;

	for (pstdiv = 1; pstdiv < PST_DIV_MAX; pstdiv++) {
		clk = pixclock * pstdiv;
		for (frefc = DIV_REF_MIN; frefc <= DIV_REF_MAX; frefc++)
			try_each_loopc(clk, pstdiv, frefc, pll_config);
	}
}

static void config_pll(struct loongson_device *ldev, unsigned long pll_base,
		       struct pix_pll *pll_cfg)
{
	u32 val;
	u32 count = 0;

	/* clear sel_pll_out0 */
	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val &= ~(1UL << 8);
	ls7a_io_wreg(ldev, pll_base + 0x4, val);

	/* set pll_pd */
	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val |= (1UL << 13);
	ls7a_io_wreg(ldev, pll_base + 0x4, val);

	/* clear set_pll_param */
	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val &= ~(1UL << 11);
	ls7a_io_wreg(ldev, pll_base + 0x4, val);

	/* clear old value & config new value */
	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val &= ~(0x7fUL << 0);
	val |= (pll_cfg->l1_frefc << 0); /* refc */
	ls7a_io_wreg(ldev, pll_base + 0x4, val);
	val = ls7a_io_rreg(ldev, pll_base + 0x0);
	val &= ~(0x7fUL << 0);
	val |= (pll_cfg->l2_div << 0); /* div */
	val &= ~(0x1ffUL << 21);
	val |= (pll_cfg->l1_loopc << 21); /* loopc */
	ls7a_io_wreg(ldev, pll_base + 0x0, val);

	/* set set_pll_param */
	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val |= (1UL << 11);
	ls7a_io_wreg(ldev, pll_base + 0x4, val);
	/* clear pll_pd */
	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val &= ~(1UL << 13);
	ls7a_io_wreg(ldev, pll_base + 0x4, val);

	while (!(ls7a_io_rreg(ldev, pll_base + 0x4) & 0x80)) {
		cpu_relax();
		count++;
		if (count >= 1000) {
			DRM_ERROR("loongson-7A PLL lock failed\n");
			break;
		}
	}

	val = ls7a_io_rreg(ldev, pll_base + 0x4);
	val |= (1UL << 8);
	ls7a_io_wreg(ldev, pll_base + 0x4, val);
}

static void loongson_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct loongson_device *ldev = dev->dev_private;
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	const struct drm_format_info *format;
	struct pix_pll pll_cfg;
	u32 hr, hss, hse, hfl;
	u32 vr, vss, vse, vfl;
	u32 pix_freq;
	u32 reg_offset;

	hr = mode->hdisplay;
	hss = mode->hsync_start;
	hse = mode->hsync_end;
	hfl = mode->htotal;

	vr = mode->vdisplay;
	vss = mode->vsync_start;
	vse = mode->vsync_end;
	vfl = mode->vtotal;

	pix_freq = mode->clock;
	reg_offset = lcrtc->reg_offset;
	format = crtc->primary->state->fb->format;

	ls7a_mm_wreg(ldev, FB_DITCFG_REG + reg_offset, 0);
	ls7a_mm_wreg(ldev, FB_DITTAB_LO_REG + reg_offset, 0);
	ls7a_mm_wreg(ldev, FB_DITTAB_HI_REG + reg_offset, 0);
	ls7a_mm_wreg(ldev, FB_PANCFG_REG + reg_offset, FB_PANCFG_DEF);
	ls7a_mm_wreg(ldev, FB_PANTIM_REG + reg_offset, 0);

	ls7a_mm_wreg(ldev, FB_HDISPLAY_REG + reg_offset, (hfl << 16) | hr);
	ls7a_mm_wreg(ldev, FB_HSYNC_REG + reg_offset,
		     FB_HSYNC_PULSE | (hse << 16) | hss);

	ls7a_mm_wreg(ldev, FB_VDISPLAY_REG + reg_offset, (vfl << 16) | vr);
	ls7a_mm_wreg(ldev, FB_VSYNC_REG + reg_offset,
		     FB_VSYNC_PULSE | (vse << 16) | vss);

	switch (format->format) {
	case DRM_FORMAT_RGB565:
		lcrtc->cfg_reg |= 0x3;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	default:
		lcrtc->cfg_reg |= 0x4;
		break;
	}
	ls7a_mm_wreg(ldev, FB_CFG_REG + reg_offset, lcrtc->cfg_reg);

	cal_freq(pix_freq, &pll_cfg);
	config_pll(ldev, LS7A_PIX_PLL + reg_offset, &pll_cfg);
}

static void loongson_crtc_atomic_enable(struct drm_crtc *crtc,
					struct drm_atomic_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct loongson_device *ldev = dev->dev_private;
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	u32 reg_offset = lcrtc->reg_offset;

	if (lcrtc->cfg_reg & CFG_ENABLE)
		goto vblank_on;

	lcrtc->cfg_reg |= CFG_ENABLE;
	ls7a_mm_wreg(ldev, FB_CFG_REG + reg_offset, lcrtc->cfg_reg);

vblank_on:
	drm_crtc_vblank_on(crtc);
}

static void loongson_crtc_atomic_disable(struct drm_crtc *crtc,
					 struct drm_atomic_state *old_crtc_state)
{
	struct drm_device *dev = crtc->dev;
	struct loongson_device *ldev = dev->dev_private;
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	u32 reg_offset = lcrtc->reg_offset;

	lcrtc->cfg_reg &= ~CFG_ENABLE;
	ls7a_mm_wreg(ldev, FB_CFG_REG + reg_offset, lcrtc->cfg_reg);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	drm_crtc_vblank_off(crtc);
}

static void loongson_crtc_atomic_flush(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_crtc_state)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static enum drm_mode_status loongson_mode_valid(struct drm_crtc *crtc,
		const struct drm_display_mode *mode)
{
	if (mode->hdisplay > 1920)
		return MODE_BAD;
	if (mode->vdisplay > 1080)
		return MODE_BAD;
	if (mode->hdisplay % 64)
		return MODE_BAD;
	if (mode->clock >= 173000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_crtc_helper_funcs loongson_crtc_helper_funcs = {
	.mode_valid = loongson_mode_valid,
	.mode_set_nofb = loongson_crtc_mode_set_nofb,
	.atomic_flush = loongson_crtc_atomic_flush,
	.atomic_enable = loongson_crtc_atomic_enable,
	.atomic_disable = loongson_crtc_atomic_disable,
};

static const struct drm_crtc_funcs loongson_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = loongson_crtc_enable_vblank,
	.disable_vblank = loongson_crtc_disable_vblank,
};

int loongson_crtc_init(struct loongson_device *ldev, int index)
{
	struct loongson_crtc *lcrtc;
	u32 ret;

	lcrtc = kzalloc(sizeof(struct loongson_crtc), GFP_KERNEL);
	if (lcrtc == NULL)
		return -1;

	lcrtc->ldev = ldev;
	lcrtc->reg_offset = index * REG_OFFSET;
	lcrtc->cfg_reg = CFG_RESET;
	lcrtc->crtc_id = index;

	ret = loongson_plane_init(lcrtc);
	if (ret)
		return ret;

	ret = drm_crtc_init_with_planes(ldev->dev, &lcrtc->base, lcrtc->plane,
					NULL, &loongson_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("failed to init crtc %d\n", index);
		drm_plane_cleanup(lcrtc->plane);
		return ret;
	}

	drm_crtc_helper_add(&lcrtc->base, &loongson_crtc_helper_funcs);

	ldev->mode_info[index].crtc = lcrtc;

	return 0;
}

