// SPDX-License-Identifier: GPL-2.0
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */
#include <drm/drm_vblank.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>

#include "lsdc_drv.h"
#include "lsdc_regs.h"
#include "lsdc_pll.h"

static int lsdc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct lsdc_device *ldev = to_lsdc(crtc->dev);
	unsigned int index = drm_crtc_index(crtc);
	struct drm_crtc_state *state = crtc->state;
	u32 val;

	if (state->enable) {
		val = lsdc_reg_read32(ldev, LSDC_INT_REG);

		if (index == 0)
			val |= INT_CRTC0_VS_EN;
		else if (index == 1)
			val |= INT_CRTC1_VS_EN;

		lsdc_reg_write32(ldev, LSDC_INT_REG, val);
	}

	return 0;
}

static void lsdc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct lsdc_device *ldev = to_lsdc(crtc->dev);
	unsigned int index = drm_crtc_index(crtc);
	u32 val;

	val = lsdc_reg_read32(ldev, LSDC_INT_REG);

	if (index == 0)
		val &= ~INT_CRTC0_VS_EN;
	else if (index == 1)
		val &= ~INT_CRTC1_VS_EN;

	lsdc_reg_write32(ldev, LSDC_INT_REG, val);
}

static void lsdc_crtc_reset(struct drm_crtc *crtc)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	unsigned int index = drm_crtc_index(crtc);
	struct lsdc_crtc_state *priv_crtc_state;
	u32 val;

	/* The crtc get soft reset if bit 20 of CRTC*_CFG_REG
	 * is write with falling edge.
	 *
	 * Doing this to switch from soft reset state to working state
	 */
	if (index == 0) {
		val = CFG_RESET_BIT | CFG_OUTPUT_EN_BIT | LSDC_PF_XRGB8888;
		lsdc_reg_write32(ldev, LSDC_CRTC0_CFG_REG, val);
	} else if (index == 1) {
		val = CFG_RESET_BIT | CFG_OUTPUT_EN_BIT | LSDC_PF_XRGB8888;
		lsdc_reg_write32(ldev, LSDC_CRTC1_CFG_REG, val);
	}

	if (crtc->state) {
		priv_crtc_state = to_lsdc_crtc_state(crtc->state);
		__drm_atomic_helper_crtc_destroy_state(&priv_crtc_state->base);
		kfree(priv_crtc_state);
	}

	priv_crtc_state = kzalloc(sizeof(*priv_crtc_state), GFP_KERNEL);
	if (!priv_crtc_state)
		return;

	__drm_atomic_helper_crtc_reset(crtc, &priv_crtc_state->base);

	drm_dbg(ddev, "crtc%u reset\n", index);
}

static void lsdc_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct lsdc_crtc_state *priv_crtc_state = to_lsdc_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&priv_crtc_state->base);

	kfree(priv_crtc_state);
}

static struct drm_crtc_state *lsdc_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct lsdc_crtc_state *new_priv_state;
	struct lsdc_crtc_state *old_priv_state;
	struct drm_device *ddev = crtc->dev;

	if (drm_WARN_ON(ddev, !crtc->state))
		return NULL;

	new_priv_state = kmalloc(sizeof(*new_priv_state), GFP_KERNEL);
	if (!new_priv_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_priv_state->base);

	old_priv_state = to_lsdc_crtc_state(crtc->state);

	memcpy(&new_priv_state->pparams, &old_priv_state->pparams, sizeof(new_priv_state->pparams));

	return &new_priv_state->base;
}

static const struct drm_crtc_funcs lsdc_crtc_funcs = {
	.reset = lsdc_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = lsdc_crtc_atomic_duplicate_state,
	.atomic_destroy_state = lsdc_crtc_atomic_destroy_state,
	.enable_vblank = lsdc_crtc_enable_vblank,
	.disable_vblank = lsdc_crtc_disable_vblank,
};

static enum drm_mode_status
lsdc_crtc_helper_mode_valid(struct drm_crtc *crtc,
			    const struct drm_display_mode *mode)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_chip_desc *desc = ldev->desc;

	if (mode->hdisplay > desc->max_width)
		return MODE_BAD_HVALUE;
	if (mode->vdisplay > desc->max_height)
		return MODE_BAD_VVALUE;

	if (mode->clock > desc->max_pixel_clk) {
		drm_dbg_kms(ddev, "mode %dx%d, pixel clock=%d is too high\n",
			    mode->hdisplay, mode->vdisplay, mode->clock);
		return MODE_CLOCK_HIGH;
	}

	/* The CRTC hardware dma take 256 bytes once a time,
	 * it is a limitation of the CRTC.
	 * TODO: check RGB565 support
	 */
	if ((mode->hdisplay * 4) % desc->stride_alignment) {
		drm_dbg_kms(ddev, "stride is not %u bytes aligned\n", desc->stride_alignment);
		return MODE_BAD;
	}

	return MODE_OK;
}

static int lsdc_crtc_helper_atomic_check(struct drm_crtc *crtc,
					 struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->enable)
		return 0; /* no mode checks if CRTC is being disabled */

	return 0;
}

static void lsdc_update_pixclk(struct drm_crtc *crtc, unsigned int pixclk)
{
	struct lsdc_display_pipe *dispipe = container_of(crtc, struct lsdc_display_pipe, crtc);
	struct lsdc_pll *pixpll = &dispipe->pixpll;
	const struct lsdc_pixpll_funcs *clkfun = pixpll->funcs;
	struct lsdc_crtc_state *priv_state = to_lsdc_crtc_state(crtc->state);

	clkfun->update(pixpll, &priv_state->pparams);
}

static void lsdc_crtc_helper_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	unsigned int pixclock = mode->clock;
	unsigned int index = drm_crtc_index(crtc);
	u32 h_sync, v_sync, h_val, v_val;

	/* 26:16 total pixels, 10:0 visiable pixels, in horizontal */
	h_val = (mode->crtc_htotal << 16) | mode->crtc_hdisplay;
	/* 26:16 total pixels, 10:0 visiable pixels, in vertical */
	v_val = (mode->crtc_vtotal << 16) | mode->crtc_vdisplay;
	/* 26:16 hsync end, 10:0 hsync start, bit 30 is hsync enable */
	h_sync = (mode->crtc_hsync_end << 16) | mode->crtc_hsync_start | EN_HSYNC_BIT;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		h_sync |= INV_HSYNC_BIT;

	/* 26:16 vsync end, 10:0 vsync start, bit 30 is vsync enable */
	v_sync = (mode->crtc_vsync_end << 16) | mode->crtc_vsync_start | EN_VSYNC_BIT;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		v_sync |= INV_VSYNC_BIT;

	if (index == 0) {
		lsdc_reg_write32(ldev, LSDC_CRTC0_FB_ORIGIN_REG, 0);
		lsdc_reg_write32(ldev, LSDC_CRTC0_HDISPLAY_REG, h_val);
		lsdc_reg_write32(ldev, LSDC_CRTC0_VDISPLAY_REG, v_val);
		lsdc_reg_write32(ldev, LSDC_CRTC0_HSYNC_REG, h_sync);
		lsdc_reg_write32(ldev, LSDC_CRTC0_VSYNC_REG, v_sync);
	} else if (index == 1) {
		lsdc_reg_write32(ldev, LSDC_CRTC1_FB_ORIGIN_REG, 0);
		lsdc_reg_write32(ldev, LSDC_CRTC1_HDISPLAY_REG, h_val);
		lsdc_reg_write32(ldev, LSDC_CRTC1_VDISPLAY_REG, v_val);
		lsdc_reg_write32(ldev, LSDC_CRTC1_HSYNC_REG, h_sync);
		lsdc_reg_write32(ldev, LSDC_CRTC1_VSYNC_REG, v_sync);
	}

	drm_dbg(ddev, "%s modeset: %ux%u\n", crtc->name, mode->hdisplay, mode->vdisplay);

	lsdc_update_pixclk(crtc, pixclock);
}

static void lsdc_enable_display(struct lsdc_device *ldev, unsigned int index)
{
	u32 val;

	if (index == 0) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC0_CFG_REG);
		val |= CFG_OUTPUT_EN_BIT;
		lsdc_reg_write32(ldev, LSDC_CRTC0_CFG_REG, val);
	} else if (index == 1) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC1_CFG_REG);
		val |= CFG_OUTPUT_EN_BIT;
		lsdc_reg_write32(ldev, LSDC_CRTC1_CFG_REG, val);
	}
}

static void lsdc_disable_display(struct lsdc_device *ldev, unsigned int index)
{
	u32 val;

	if (index == 0) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC0_CFG_REG);
		val &= ~CFG_OUTPUT_EN_BIT;
		lsdc_reg_write32(ldev, LSDC_CRTC0_CFG_REG, val);
	} else if (index == 1) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC1_CFG_REG);
		val &= ~CFG_OUTPUT_EN_BIT;
		lsdc_reg_write32(ldev, LSDC_CRTC1_CFG_REG, val);
	}
}

static void lsdc_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					   struct drm_atomic_state *state)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);

	drm_crtc_vblank_on(crtc);

	lsdc_enable_display(ldev, drm_crtc_index(crtc));

	drm_dbg(ddev, "%s: enabled\n", crtc->name);
}

static void lsdc_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					    struct drm_atomic_state *state)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);

	drm_crtc_vblank_off(crtc);

	lsdc_disable_display(ldev, drm_crtc_index(crtc));

	drm_dbg(ddev, "%s: disabled\n", crtc->name);
}

static void lsdc_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
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

static const struct drm_crtc_helper_funcs lsdc_crtc_helper_funcs = {
	.mode_valid = lsdc_crtc_helper_mode_valid,
	.mode_set_nofb = lsdc_crtc_helper_mode_set_nofb,
	.atomic_enable = lsdc_crtc_helper_atomic_enable,
	.atomic_disable = lsdc_crtc_helper_atomic_disable,
	.atomic_check = lsdc_crtc_helper_atomic_check,
	.atomic_flush = lsdc_crtc_atomic_flush,
};

/**
 * lsdc_crtc_init
 *
 * @ddev: point to the drm_device structure
 * @index: hardware crtc index
 *
 * Init CRTC
 */
int lsdc_crtc_init(struct drm_device *ddev,
		   struct drm_crtc *crtc,
		   unsigned int index,
		   struct drm_plane *primary,
		   struct drm_plane *cursor)
{
	int ret;

	drm_crtc_helper_add(crtc, &lsdc_crtc_helper_funcs);

	ret = drm_mode_crtc_set_gamma_size(crtc, 256);
	if (ret)
		drm_warn(ddev, "set the gamma table size failed\n");

	return drm_crtc_init_with_planes(ddev,
					 crtc,
					 primary,
					 cursor,
					 &lsdc_crtc_funcs,
					 "crtc%d",
					 index);
}
