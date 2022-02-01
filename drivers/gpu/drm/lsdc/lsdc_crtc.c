// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <drm/drm_print.h>
#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_atomic.h>
#include <drm/drm_vblank.h>
#include <drm/drm_drv.h>


#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_damage_helper.h>

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
		val = lsdc_reg_read32(ldev, LSDC_CRTC0_CFG_REG);
		val |= CFG_RESET_BIT | CFG_OUTPUT_EN_BIT;
		lsdc_reg_write32(ldev, LSDC_CRTC0_CFG_REG, val);
	} else if (index == 1) {
		val = lsdc_reg_read32(ldev, LSDC_CRTC1_CFG_REG);
		val |= CFG_RESET_BIT | CFG_OUTPUT_EN_BIT;
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

	priv_crtc_state->pix_fmt = val & CFG_PIX_FMT_MASK;

	__drm_atomic_helper_crtc_reset(crtc, &priv_crtc_state->base);

	drm_info(ddev, "crtc%u reset\n", index);
}


static void lsdc_crtc_atomic_destroy_state(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
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

	memcpy(&new_priv_state->pparams, &old_priv_state->pparams,
		sizeof(new_priv_state->pparams));

	new_priv_state->pix_fmt = old_priv_state->pix_fmt;

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

	/* the crtc hardware dma take 256 bytes once a time
	 * TODO: check RGB565 support
	 */
	if ((mode->hdisplay * 4) % desc->stride_alignment) {
		drm_dbg_kms(ddev, "stride is not %u bytes aligned\n",
				desc->stride_alignment);
		return MODE_BAD;
	}

	return MODE_OK;
}


static void lsdc_update_pixclk(struct drm_crtc *crtc, unsigned int pixclk, bool verbose)
{
	struct lsdc_display_pipe *dispipe;
	struct lsdc_pll *pixpll;
	const struct lsdc_pixpll_funcs *clkfun;
	struct lsdc_crtc_state *priv_crtc_state;

	priv_crtc_state = to_lsdc_crtc_state(crtc->state);

	dispipe = container_of(crtc, struct lsdc_display_pipe, crtc);
	pixpll = &dispipe->pixpll;
	clkfun = pixpll->funcs;

	/* config the pixel pll */
	clkfun->update(pixpll, &priv_crtc_state->pparams);

	if (verbose)
		clkfun->print(pixpll, pixclk);
}


static void lsdc_crtc_helper_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	unsigned int hr = mode->hdisplay;
	unsigned int hss = mode->hsync_start;
	unsigned int hse = mode->hsync_end;
	unsigned int hfl = mode->htotal;
	unsigned int vr = mode->vdisplay;
	unsigned int vss = mode->vsync_start;
	unsigned int vse = mode->vsync_end;
	unsigned int vfl = mode->vtotal;
	unsigned int pixclock = mode->clock;
	unsigned int index = drm_crtc_index(crtc);


	if (index == 0) {
		/* CRTC 0 */
		u32 hsync, vsync;

		lsdc_reg_write32(ldev, LSDC_CRTC0_FB_ORIGIN_REG, 0);

		/* 26:16 total pixels, 10:0 visiable pixels, in horizontal */
		lsdc_reg_write32(ldev, LSDC_CRTC0_HDISPLAY_REG,
			(mode->crtc_htotal << 16) | mode->crtc_hdisplay);

		/* 26:16 total pixels, 10:0 visiable pixels, in vertical */
		lsdc_reg_write32(ldev, LSDC_CRTC0_VDISPLAY_REG,
			(mode->crtc_vtotal << 16) | mode->crtc_vdisplay);

		/* 26:16 hsync end, 10:0 hsync start */
		hsync = (mode->crtc_hsync_end << 16) | mode->crtc_hsync_start;

		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			hsync |= INV_HSYNC_BIT;

		lsdc_reg_write32(ldev, LSDC_CRTC0_HSYNC_REG, EN_HSYNC_BIT | hsync);

		/* 26:16 vsync end, 10:0 vsync start */
		vsync = (mode->crtc_vsync_end << 16) | mode->crtc_vsync_start;

		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			vsync |= INV_VSYNC_BIT;

		lsdc_reg_write32(ldev, LSDC_CRTC0_VSYNC_REG, EN_VSYNC_BIT | vsync);

	} else if (index == 1) {
		/* CRTC 1 */
		u32 hsync, vsync;

		lsdc_reg_write32(ldev, LSDC_CRTC1_FB_ORIGIN_REG, 0);

		/* 26:16 total pixels, 10:0 visiable pixels, in horizontal */
		lsdc_reg_write32(ldev, LSDC_CRTC1_HDISPLAY_REG,
			(mode->crtc_htotal << 16) | mode->crtc_hdisplay);

		/* 26:16 total pixels, 10:0 visiable pixels, in vertical */
		lsdc_reg_write32(ldev, LSDC_CRTC1_VDISPLAY_REG,
			(mode->crtc_vtotal << 16) | mode->crtc_vdisplay);

		/* 26:16 hsync end, 10:0 hsync start */
		hsync = (mode->crtc_hsync_end << 16) | mode->crtc_hsync_start;

		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			hsync |= INV_HSYNC_BIT;

		lsdc_reg_write32(ldev, LSDC_CRTC1_HSYNC_REG, EN_HSYNC_BIT | hsync);

		/* 26:16 vsync end, 10:0 vsync start */
		vsync = (mode->crtc_vsync_end << 16) | mode->crtc_vsync_start;

		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			vsync |= INV_VSYNC_BIT;

		lsdc_reg_write32(ldev, LSDC_CRTC1_VSYNC_REG, EN_VSYNC_BIT | vsync);
	}

	drm_dbg_kms(ddev, "hdisplay=%d, hsync_start=%d, hsync_end=%d, htotal=%d\n",
			hr, hss, hse, hfl);

	drm_dbg_kms(ddev, "vdisplay=%d, vsync_start=%d, vsync_end=%d, vtotal=%d\n",
			vr, vss, vse, vfl);

	drm_dbg_kms(ddev, "%s modeset: %ux%u\n", crtc->name, hr, vr);

	lsdc_update_pixclk(crtc, pixclock, false);
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

	drm_dbg_kms(ddev, "%s: enabled\n", crtc->name);
}


static void lsdc_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					    struct drm_atomic_state *state)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);

	drm_crtc_vblank_off(crtc);

	lsdc_disable_display(ldev, drm_crtc_index(crtc));

	drm_dbg_kms(ddev, "%s: disabled\n", crtc->name);
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
