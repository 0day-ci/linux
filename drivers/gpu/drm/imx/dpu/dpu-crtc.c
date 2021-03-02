// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <linux/component.h>
#include <linux/irq.h>
#include <linux/irqflags.h>
#include <linux/pm_runtime.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_color_mgmt.h>

#include "dpu.h"
#include "dpu-crtc.h"
#include "dpu-dprc.h"
#include "dpu-drv.h"
#include "dpu-plane.h"

#define DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(_name)			\
do {									\
	unsigned long ret;						\
	ret = wait_for_completion_timeout(&dpu_crtc->_name, HZ);	\
	if (ret == 0)							\
		dpu_crtc_err(crtc, "%s: wait for " #_name " timeout\n",	\
							__func__);	\
} while (0)

#define DPU_CRTC_WAIT_FOR_FRAMEGEN_FRAME_CNT_MOVING(fg)			\
do {									\
	if (dpu_fg_wait_for_frame_counter_moving(fg))			\
		dpu_crtc_err(crtc,					\
			"%s: FrameGen frame counter isn't moving\n",	\
							__func__);	\
} while (0)

#define DPU_CRTC_CHECK_FRAMEGEN_FIFO(fg)				\
do {									\
	if (dpu_fg_secondary_requests_to_read_empty_fifo(fg)) {		\
		dpu_fg_secondary_clear_channel_status(fg);		\
		dpu_crtc_err(crtc, "%s: FrameGen FIFO empty\n",		\
							__func__);	\
	}								\
} while (0)

#define DPU_CRTC_WAIT_FOR_FRAMEGEN_SECONDARY_SYNCUP(fg)			\
do {									\
	if (dpu_fg_wait_for_secondary_syncup(fg))			\
		dpu_crtc_err(crtc,					\
			"%s: FrameGen secondary channel isn't syncup\n",\
							__func__);	\
} while (0)

static u32 dpu_crtc_get_vblank_counter(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	if (pm_runtime_active(dpu_crtc->dev->parent))
		return dpu_fg_get_frame_index(dpu_crtc->fg);
	else
		return (u32)drm_crtc_vblank_count(crtc);
}

static int dpu_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	enable_irq(dpu_crtc->dec_frame_complete_irq);

	return 0;
}

static void dpu_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	disable_irq_nosync(dpu_crtc->dec_frame_complete_irq);
}

static irqreturn_t
dpu_crtc_dec_frame_complete_irq_handler(int irq, void *dev_id)
{
	struct dpu_crtc *dpu_crtc = dev_id;
	struct drm_crtc *crtc = &dpu_crtc->base;
	unsigned long flags;

	drm_crtc_handle_vblank(crtc);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (dpu_crtc->event) {
		drm_crtc_send_vblank_event(crtc, dpu_crtc->event);
		dpu_crtc->event = NULL;
		drm_crtc_vblank_put(crtc);
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t dpu_crtc_common_irq_handler(int irq, void *dev_id)
{
	struct dpu_crtc *dpu_crtc = dev_id;
	struct drm_crtc *crtc = &dpu_crtc->base;

	if (irq == dpu_crtc->dec_seq_complete_irq) {
		complete(&dpu_crtc->dec_seq_complete_done);
	} else if (irq == dpu_crtc->dec_shdld_irq) {
		complete(&dpu_crtc->dec_shdld_done);
	} else if (irq == dpu_crtc->ed_cont_shdld_irq) {
		complete(&dpu_crtc->ed_cont_shdld_done);
	} else if (irq == dpu_crtc->ed_safe_shdld_irq) {
		complete(&dpu_crtc->ed_safe_shdld_done);
	} else {
		dpu_crtc_err(crtc, "invalid CRTC irq(%u)\n", irq);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static const struct drm_crtc_funcs dpu_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.get_vblank_counter	= dpu_crtc_get_vblank_counter,
	.enable_vblank		= dpu_crtc_enable_vblank,
	.disable_vblank		= dpu_crtc_disable_vblank,
	.get_vblank_timestamp	= drm_crtc_vblank_helper_get_vblank_timestamp,
};

static void dpu_crtc_queue_state_event(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc));
		WARN_ON(dpu_crtc->event);
		dpu_crtc->event = crtc->state->event;
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static enum drm_mode_status
dpu_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	if (mode->crtc_clock > DPU_FRAMEGEN_MAX_CLOCK)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int dpu_crtc_pm_runtime_get_sync(struct dpu_crtc *dpu_crtc)
{
	int ret;

	ret = pm_runtime_get_sync(dpu_crtc->dev->parent);
	if (ret < 0) {
		pm_runtime_put_noidle(dpu_crtc->dev->parent);
		dpu_crtc_err(&dpu_crtc->base,
			     "failed to get parent device RPM sync: %d\n", ret);
	}

	return ret;
}

static int dpu_crtc_pm_runtime_put(struct dpu_crtc *dpu_crtc)
{
	int ret;

	ret = pm_runtime_put(dpu_crtc->dev->parent);
	if (ret < 0)
		dpu_crtc_err(&dpu_crtc->base,
			     "failed to put parent device RPM: %d\n", ret);

	return ret;
}

static void dpu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct drm_display_mode *adj = &crtc->state->adjusted_mode;
	enum dpu_link_id cf_link;

	dpu_crtc_dbg(crtc, "mode " DRM_MODE_FMT "\n", DRM_MODE_ARG(adj));

	/* request power-on when we start to set mode for CRTC */
	dpu_crtc_pm_runtime_get_sync(dpu_crtc);

	dpu_fg_displaymode(dpu_crtc->fg, FG_DM_SEC_ON_TOP);
	dpu_fg_panic_displaymode(dpu_crtc->fg, FG_DM_CONSTCOL);
	dpu_fg_cfg_videomode(dpu_crtc->fg, adj);

	dpu_tcon_cfg_videomode(dpu_crtc->tcon, adj);
	dpu_tcon_set_fmt(dpu_crtc->tcon);

	dpu_cf_framedimensions(dpu_crtc->cf_cont,
			       adj->crtc_hdisplay, adj->crtc_vdisplay);
	dpu_cf_framedimensions(dpu_crtc->cf_safe,
			       adj->crtc_hdisplay, adj->crtc_vdisplay);
	/* constframe in content stream shows black frame - CRTC background */
	dpu_cf_constantcolor_black(dpu_crtc->cf_cont);
	/* constframe in safety stream shows blue frame */
	dpu_cf_constantcolor_blue(dpu_crtc->cf_safe);

	cf_link = dpu_cf_get_link_id(dpu_crtc->cf_safe);
	dpu_ed_pec_src_sel(dpu_crtc->ed_safe, cf_link);

	cf_link = dpu_cf_get_link_id(dpu_crtc->cf_cont);
	dpu_ed_pec_src_sel(dpu_crtc->ed_cont, cf_link);
}

static int dpu_crtc_atomic_check_gamma(struct drm_crtc *crtc,
				       struct drm_crtc_state *state)
{
	size_t lut_size;

	if (!state->color_mgmt_changed || !state->gamma_lut)
		return 0;

	if (crtc->state->gamma_lut &&
	    (crtc->state->gamma_lut->base.id == state->gamma_lut->base.id))
		return 0;

	if (state->gamma_lut->length % sizeof(struct drm_color_lut)) {
		dpu_crtc_dbg(crtc, "wrong gamma_lut length\n");
		return -EINVAL;
	}

	lut_size = state->gamma_lut->length / sizeof(struct drm_color_lut);
	if (lut_size != 256) {
		dpu_crtc_dbg(crtc, "gamma_lut size is not 256\n");
		return -EINVAL;
	}

	return 0;
}

static int dpu_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	int ret;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	ret = dpu_crtc_atomic_check_gamma(crtc, crtc_state);
	if (ret)
		return ret;

	/* force a mode set if the CRTC is changed to active */
	if (crtc_state->active_changed && crtc_state->active) {
		/*
		 * If mode_changed is set by us, call
		 * drm_atomic_helper_check_modeset() as it's Kerneldoc requires.
		 */
		if (!crtc_state->mode_changed) {
			crtc_state->mode_changed = true;

			ret = drm_atomic_helper_check_modeset(crtc->dev, state);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static void dpu_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state;
	struct drm_atomic_state *old_state;
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct dpu_plane_state *old_dpstate;
	struct dpu_fetchunit *fu;
	const struct dpu_fetchunit_ops *fu_ops;
	enum dpu_link_id cf_link;
	int i;

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	old_state = old_crtc_state->state;

	/* do nothing if planes keep being disabled */
	if (old_crtc_state->plane_mask == 0 && crtc->state->plane_mask == 0)
		return;

	/* request power-on when any plane starts to be active */
	if (old_crtc_state->plane_mask == 0 && crtc->state->plane_mask != 0)
		dpu_crtc_pm_runtime_get_sync(dpu_crtc);

	/*
	 * Disable relevant planes' resources in SHADOW only.
	 * Whether any of them would be disabled or kept running depends
	 * on new plane states in the new global atomic state.
	 */
	for_each_old_plane_in_state(old_state, plane, old_plane_state, i) {
		old_dpstate = to_dpu_plane_state(old_plane_state);

		if (!old_plane_state->fb)
			continue;

		if (old_plane_state->crtc != crtc)
			continue;

		fu = old_dpstate->source;

		fu_ops = dpu_fu_get_ops(fu);

		fu_ops->disable_src_buf(fu);

		if (old_dpstate->is_top) {
			cf_link = dpu_cf_get_link_id(dpu_crtc->cf_cont);
			dpu_ed_pec_src_sel(dpu_crtc->ed_cont, cf_link);
		}
	}
}

static void dpu_crtc_set_gammacor(struct dpu_crtc *dpu_crtc)
{
	struct drm_crtc *crtc = &dpu_crtc->base;
	struct drm_color_lut *lut;

	lut = (struct drm_color_lut *)crtc->state->gamma_lut->data;

	dpu_gc_enable_rgb_write(dpu_crtc->gc);
	dpu_gc_mode(dpu_crtc->gc, GC_GAMMACOR);

	dpu_gc_start_rgb(dpu_crtc->gc, lut);
	dpu_gc_delta_rgb(dpu_crtc->gc, lut);
}

static void dpu_crtc_set_gammacor_sync(struct dpu_crtc *dpu_crtc)
{
	struct drm_crtc *crtc = &dpu_crtc->base;

	enable_irq(dpu_crtc->dec_shdld_irq);

	dpu_crtc_set_gammacor(dpu_crtc);
	dpu_fg_shdtokgen(dpu_crtc->fg);
	DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_shdld_done);

	disable_irq(dpu_crtc->dec_shdld_irq);
}

static void dpu_crtc_disable_gammacor(struct dpu_crtc *dpu_crtc)
{
	dpu_gc_mode(dpu_crtc->gc, GC_NEUTRAL);
	dpu_gc_disable_rgb_write(dpu_crtc->gc);
}

static void dpu_crtc_disable_gammacor_sync(struct dpu_crtc *dpu_crtc)
{
	struct drm_crtc *crtc = &dpu_crtc->base;

	enable_irq(dpu_crtc->dec_shdld_irq);

	dpu_crtc_disable_gammacor(dpu_crtc);
	dpu_fg_shdtokgen(dpu_crtc->fg);
	DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_shdld_done);

	disable_irq(dpu_crtc->dec_shdld_irq);
}

static void dpu_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state;
	struct drm_atomic_state *old_state;
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct dpu_plane_state *old_dpstate;
	struct dpu_fetchunit *fu;
	struct dpu_dprc *dprc;
	const struct dpu_fetchunit_ops *fu_ops;
	bool need_modeset = drm_atomic_crtc_needs_modeset(crtc->state);
	unsigned long flags;
	int i;

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	old_state = old_crtc_state->state;

	if (old_crtc_state->plane_mask == 0 && crtc->state->plane_mask == 0) {
		/* Queue a pending vbl event if necessary. */
		if (!need_modeset && crtc->state->active)
			dpu_crtc_queue_state_event(crtc);
		return;
	}

	if (!need_modeset && crtc->state->active)
		enable_irq(dpu_crtc->ed_cont_shdld_irq);

	/*
	 * Don't relinquish CPU until DPRC repeat_en is disabled
	 * and flush is done(if necessary).
	 */
	local_irq_save(flags);
	preempt_disable();

	/*
	 * Scan over old plane fetchunits to determine if we
	 * need to wait for FrameGen frame counter moving in
	 * the next loop prior to DPRC repeat_en disablement
	 * or not.
	 */
	for_each_old_plane_in_state(old_state, plane, old_plane_state, i) {
		old_dpstate = to_dpu_plane_state(old_plane_state);

		if (!old_plane_state->fb)
			continue;

		if (old_plane_state->crtc != crtc)
			continue;

		fu = old_dpstate->source;

		fu_ops = dpu_fu_get_ops(fu);

		/*
		 * Sync with FrameGen frame counter moving so that
		 * we may disable DPRC repeat_en correctly.
		 */
		if (!fu_ops->is_enabled(fu) && !need_modeset &&
		    old_crtc_state->active) {
			DPU_CRTC_WAIT_FOR_FRAMEGEN_FRAME_CNT_MOVING(dpu_crtc->fg);
			break;
		}
	}

	/*
	 * Set no stream id for disabled fetchunits of relevant planes.
	 * Also, disable DPRC repeat_en if necessary.
	 */
	for_each_old_plane_in_state(old_state, plane, old_plane_state, i) {
		old_dpstate = to_dpu_plane_state(old_plane_state);

		if (!old_plane_state->fb)
			continue;

		if (old_plane_state->crtc != crtc)
			continue;

		fu = old_dpstate->source;

		fu_ops = dpu_fu_get_ops(fu);

		if (!fu_ops->is_enabled(fu)) {
			fu_ops->set_no_stream_id(fu);

			dprc = fu_ops->get_dprc(fu);
			dpu_dprc_disable_repeat_en(dprc);
		}
	}

	if (!need_modeset && crtc->state->active) {
		/*
		 * Flush plane(s) update out to display & queue a pending
		 * vbl event if necessary.
		 */
		dpu_ed_pec_sync_trigger(dpu_crtc->ed_cont);

		local_irq_restore(flags);
		preempt_enable();

		if (old_crtc_state->gamma_lut && !crtc->state->gamma_lut)
			dpu_crtc_disable_gammacor_sync(dpu_crtc);
		else if (old_crtc_state->gamma_lut && crtc->state->gamma_lut &&
			 old_crtc_state->gamma_lut->base.id !=
			 crtc->state->gamma_lut->base.id)
			dpu_crtc_set_gammacor_sync(dpu_crtc);

		DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_cont_shdld_done);

		disable_irq(dpu_crtc->ed_cont_shdld_irq);

		DPU_CRTC_CHECK_FRAMEGEN_FIFO(dpu_crtc->fg);

		dpu_crtc_queue_state_event(crtc);
	} else {
		/*
		 * Simply flush and hope that any update takes effect
		 * if CRTC is disabled.  This helps for the case where
		 * migrating plane(s) from a disabled CRTC to the other
		 * CRTC.
		 */
		if (!crtc->state->active)
			dpu_ed_pec_sync_trigger(dpu_crtc->ed_cont);

		local_irq_restore(flags);
		preempt_enable();
	}

	/* request power-off when all planes are off */
	if (old_crtc_state->plane_mask != 0 && crtc->state->plane_mask == 0)
		dpu_crtc_pm_runtime_put(dpu_crtc);
}

static void dpu_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	unsigned long flags;

	drm_crtc_vblank_on(crtc);

	enable_irq(dpu_crtc->dec_shdld_irq);
	enable_irq(dpu_crtc->ed_cont_shdld_irq);
	enable_irq(dpu_crtc->ed_safe_shdld_irq);

	dpu_fg_enable_clock(dpu_crtc->fg);
	dpu_ed_pec_sync_trigger(dpu_crtc->ed_cont);
	dpu_ed_pec_sync_trigger(dpu_crtc->ed_safe);
	if (crtc->state->gamma_lut)
		dpu_crtc_set_gammacor(dpu_crtc);
	else
		dpu_crtc_disable_gammacor(dpu_crtc);
	dpu_fg_shdtokgen(dpu_crtc->fg);

	/* don't relinquish CPU until TCON is set to operation mode */
	local_irq_save(flags);
	preempt_disable();
	dpu_fg_enable(dpu_crtc->fg);

	/*
	 * TKT320590:
	 * Turn TCON into operation mode as soon as the first dumb
	 * frame is generated by DPU(we don't relinquish CPU to ensure
	 * this).  This makes DPR/PRG be able to evade the frame.
	 */
	DPU_CRTC_WAIT_FOR_FRAMEGEN_FRAME_CNT_MOVING(dpu_crtc->fg);
	dpu_tcon_set_operation_mode(dpu_crtc->tcon);
	local_irq_restore(flags);
	preempt_enable();

	DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_safe_shdld_done);
	DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(ed_cont_shdld_done);
	DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_shdld_done);

	disable_irq(dpu_crtc->ed_safe_shdld_irq);
	disable_irq(dpu_crtc->ed_cont_shdld_irq);
	disable_irq(dpu_crtc->dec_shdld_irq);

	DPU_CRTC_WAIT_FOR_FRAMEGEN_SECONDARY_SYNCUP(dpu_crtc->fg);

	DPU_CRTC_CHECK_FRAMEGEN_FIFO(dpu_crtc->fg);

	dpu_crtc_queue_state_event(crtc);
}

static void dpu_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct dpu_plane_state *old_dpstate;
	struct dpu_fetchunit *fu;
	struct dpu_dprc *dprc;
	const struct dpu_fetchunit_ops *fu_ops;
	unsigned long flags;
	int i;

	enable_irq(dpu_crtc->dec_seq_complete_irq);

	/* don't relinquish CPU until DPRC repeat_en is disabled */
	local_irq_save(flags);
	preempt_disable();
	/*
	 * Sync to FrameGen frame counter moving so that
	 * FrameGen can be disabled in the next frame.
	 */
	DPU_CRTC_WAIT_FOR_FRAMEGEN_FRAME_CNT_MOVING(dpu_crtc->fg);
	dpu_fg_disable(dpu_crtc->fg);
	/*
	 * There is one frame leftover after FrameGen disablement.
	 * Sync to FrameGen frame counter moving so that
	 * DPRC repeat_en can be disabled in the next frame.
	 */
	DPU_CRTC_WAIT_FOR_FRAMEGEN_FRAME_CNT_MOVING(dpu_crtc->fg);

	for_each_old_plane_in_state(state, plane, old_plane_state, i) {
		old_dpstate = to_dpu_plane_state(old_plane_state);

		if (!old_plane_state->fb)
			continue;

		if (old_plane_state->crtc != crtc)
			continue;

		fu = old_dpstate->source;

		fu_ops = dpu_fu_get_ops(fu);

		dprc = fu_ops->get_dprc(fu);
		dpu_dprc_disable_repeat_en(dprc);
	}

	local_irq_restore(flags);
	preempt_enable();

	DPU_CRTC_WAIT_FOR_COMPLETION_TIMEOUT(dec_seq_complete_done);

	disable_irq(dpu_crtc->dec_seq_complete_irq);

	dpu_fg_disable_clock(dpu_crtc->fg);

	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event && !crtc->state->active) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	/* request power-off when CRTC is disabled */
	dpu_crtc_pm_runtime_put(dpu_crtc);
}

static bool dpu_crtc_get_scanout_position(struct drm_crtc *crtc,
					  bool in_vblank_irq,
					  int *vpos, int *hpos,
					  ktime_t *stime, ktime_t *etime,
					  const struct drm_display_mode *mode)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	int vdisplay = mode->crtc_vdisplay;
	int vtotal = mode->crtc_vtotal;
	int line;
	bool reliable;

	if (stime)
		*stime = ktime_get();

	if (pm_runtime_active(dpu_crtc->dev->parent)) {
		/* line index starts with 0 for the first active output line */
		line = dpu_fg_get_line_index(dpu_crtc->fg);

		if (line < vdisplay)
			/* active scanout area - positive */
			*vpos = line + 1;
		else
			/* inside vblank - negative */
			*vpos = line - (vtotal - 1);

		reliable = true;
	} else {
		*vpos = 0;
		reliable = false;
	}

	*hpos = 0;

	if (etime)
		*etime = ktime_get();

	return reliable;
}

static const struct drm_crtc_helper_funcs dpu_helper_funcs = {
	.mode_valid		= dpu_crtc_mode_valid,
	.mode_set_nofb		= dpu_crtc_mode_set_nofb,
	.atomic_check		= dpu_crtc_atomic_check,
	.atomic_begin		= dpu_crtc_atomic_begin,
	.atomic_flush		= dpu_crtc_atomic_flush,
	.atomic_enable		= dpu_crtc_atomic_enable,
	.atomic_disable		= dpu_crtc_atomic_disable,
	.get_scanout_position	= dpu_crtc_get_scanout_position,
};

static void dpu_crtc_put_resources(struct dpu_crtc *dpu_crtc)
{
	dpu_cf_cont_put(dpu_crtc->cf_cont);
	dpu_cf_safe_put(dpu_crtc->cf_safe);
	dpu_dec_put(dpu_crtc->dec);
	dpu_ed_cont_put(dpu_crtc->ed_cont);
	dpu_ed_safe_put(dpu_crtc->ed_safe);
	dpu_fg_put(dpu_crtc->fg);
	dpu_gc_put(dpu_crtc->gc);
	dpu_tcon_put(dpu_crtc->tcon);
}

static int dpu_crtc_get_resources(struct dpu_crtc *dpu_crtc)
{
	struct dpu_soc *dpu = dev_get_drvdata(dpu_crtc->dev->parent);
	struct {
		void **dpu_unit;
		void *(*get)(struct dpu_soc *dpu, unsigned int id);
	} resources[] = {
		{(void *) &dpu_crtc->cf_cont,	(void *) dpu_cf_cont_get},
		{(void *) &dpu_crtc->cf_safe,	(void *) dpu_cf_safe_get},
		{(void *) &dpu_crtc->dec,	(void *) dpu_dec_get},
		{(void *) &dpu_crtc->ed_cont,	(void *) dpu_ed_cont_get},
		{(void *) &dpu_crtc->ed_safe,	(void *) dpu_ed_safe_get},
		{(void *) &dpu_crtc->fg,	(void *) dpu_fg_get},
		{(void *) &dpu_crtc->gc,	(void *) dpu_gc_get},
		{(void *) &dpu_crtc->tcon,	(void *) dpu_tcon_get},
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(resources); i++) {
		*resources[i].dpu_unit =
				resources[i].get(dpu, dpu_crtc->stream_id);
		if (IS_ERR(*resources[i].dpu_unit)) {
			ret = PTR_ERR(*resources[i].dpu_unit);
			dpu_crtc_put_resources(dpu_crtc);
			return ret;
		}
	}

	return 0;
}

static int dpu_crtc_request_irq(struct dpu_crtc *dpu_crtc,
				unsigned int *crtc_irq,
				unsigned int dpu_irq,
				irqreturn_t (*irq_handler)(int irq,
							   void *dev_id))
{
	struct drm_crtc *crtc = &dpu_crtc->base;
	struct dpu_soc *dpu = dev_get_drvdata(dpu_crtc->dev->parent);
	int ret;

	*crtc_irq = dpu_map_irq(dpu, dpu_irq);
	irq_set_status_flags(*crtc_irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_irq(dpu_crtc->dev, *crtc_irq, irq_handler,
			       0, dev_name(dpu_crtc->dev), dpu_crtc);
	if (ret < 0) {
		dpu_crtc_err(crtc, "failed to request irq(%u): %d\n",
							*crtc_irq, ret);
		return ret;
	}
	disable_irq(*crtc_irq);

	return 0;
}

static int dpu_crtc_request_irqs(struct dpu_crtc *dpu_crtc,
				 struct dpu_client_platformdata *pdata)
{
	struct {
		unsigned int *crtc_irq;
		unsigned int dpu_irq;
		irqreturn_t (*irq_handler)(int irq, void *dev_id);
	} irqs[] = {
		{
			&dpu_crtc->dec_frame_complete_irq,
			pdata->dec_frame_complete_irq,
			dpu_crtc_dec_frame_complete_irq_handler,
		}, {
			&dpu_crtc->dec_seq_complete_irq,
			pdata->dec_seq_complete_irq,
			dpu_crtc_common_irq_handler,
		}, {
			&dpu_crtc->dec_shdld_irq,
			pdata->dec_shdld_irq,
			dpu_crtc_common_irq_handler,
		}, {
			&dpu_crtc->ed_cont_shdld_irq,
			pdata->ed_cont_shdld_irq,
			dpu_crtc_common_irq_handler,
		}, {
			&dpu_crtc->ed_safe_shdld_irq,
			pdata->ed_safe_shdld_irq,
			dpu_crtc_common_irq_handler,
		},
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		ret = dpu_crtc_request_irq(dpu_crtc,
			irqs[i].crtc_irq, irqs[i].dpu_irq, irqs[i].irq_handler);
		if (ret)
			return ret;
	}

	return 0;
}

static int dpu_crtc_init(struct dpu_crtc *dpu_crtc,
			 struct dpu_client_platformdata *pdata,
			 struct dpu_drm_device *dpu_drm)
{
	struct drm_device *drm = &dpu_drm->base;
	struct drm_crtc *crtc = &dpu_crtc->base;
	struct dpu_plane *dpu_plane;
	struct dpu_crtc_grp *crtc_grp = pdata->crtc_grp;
	struct dpu_plane_grp *plane_grp = crtc_grp->plane_grp;
	unsigned int stream_id = pdata->stream_id;
	unsigned int crtc_cnt;
	int i, ret;

	init_completion(&dpu_crtc->dec_seq_complete_done);
	init_completion(&dpu_crtc->dec_shdld_done);
	init_completion(&dpu_crtc->ed_cont_shdld_done);
	init_completion(&dpu_crtc->ed_safe_shdld_done);

	dpu_crtc->grp = crtc_grp;
	dpu_crtc->stream_id = stream_id;
	dpu_crtc->hw_plane_cnt = plane_grp->hw_plane_cnt;

	ret = dpu_crtc_get_resources(dpu_crtc);
	if (ret) {
		drm_err(drm, "failed to get HW resources for CRTC: %d\n", ret);
		return ret;
	}

	plane_grp->cf[stream_id] = dpu_crtc->cf_cont;
	plane_grp->ed[stream_id] = dpu_crtc->ed_cont;

	/* each CRTC has a primary plane */
	dpu_plane = dpu_plane_initialize(drm, 0, plane_grp,
					 DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(dpu_plane)) {
		ret = PTR_ERR(dpu_plane);
		drm_err(drm, "failed to init primary plane: %d\n", ret);
		goto err_put_resources;
	}

	drm_crtc_helper_add(crtc, &dpu_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, crtc, &dpu_plane->base,
					NULL, &dpu_crtc_funcs, NULL);
	if (ret) {
		drm_err(drm, "failed to add CRTC: %d\n", ret);
		goto err_put_resources;
	}

	/* X server assumes 256 element gamma table so let's use that. */
	ret = drm_mode_crtc_set_gamma_size(crtc, 256);
	if (ret) {
		dpu_crtc_err(crtc, "failed to set gamma size: %d\n", ret);
		goto err_put_resources;
	}

	drm_crtc_enable_color_mgmt(crtc, 0, false, 256);

	dpu_crtc->encoder->possible_crtcs = drm_crtc_mask(crtc);
	crtc_grp->crtc_mask |= drm_crtc_mask(crtc);
	crtc_cnt = hweight32(crtc_grp->crtc_mask);

	/* initialize shared overlay planes for CRTCs in a CRTC group */
	if (crtc_cnt == DPU_CRTC_CNT_IN_GRP) {
		/*
		 * All HW planes in a plane group are shared by CRTCs in a
		 * CRTC group.  They will be assigned to either primary plane
		 * or overlay plane dynamically in runtime.  Considering a
		 * CRTC consumes all HW planes and primary plane takes one
		 * HW plane, so overlay plane count for a CRTC group should
		 * be plane_grp->hw_plane_cnt - 1.
		 */
		for (i = 1; i < plane_grp->hw_plane_cnt; i++) {
			dpu_plane =
				dpu_plane_initialize(drm, crtc_grp->crtc_mask,
						     plane_grp,
						     DRM_PLANE_TYPE_OVERLAY);
			if (IS_ERR(dpu_plane)) {
				ret = PTR_ERR(dpu_plane);
				dpu_crtc_err(crtc,
					"failed to init overlay plane(%d): %d\n",
									i, ret);
				goto err_put_resources;
			}
		}
	}

	ret = dpu_crtc_pm_runtime_get_sync(dpu_crtc);
	if (ret < 0)
		goto err_put_resources;

	ret = dpu_crtc_request_irqs(dpu_crtc, pdata);
	if (ret)
		goto err_put_pm_runtime;

	ret = dpu_crtc_pm_runtime_put(dpu_crtc);
	if (ret < 0)
		dpu_crtc_put_resources(dpu_crtc);

	return ret;

err_put_pm_runtime:
	pm_runtime_put(dpu_crtc->dev->parent);
err_put_resources:
	dpu_crtc_put_resources(dpu_crtc);

	return ret;
}

static int dpu_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct dpu_client_platformdata *pdata = dev->platform_data;
	struct dpu_drm_device *dpu_drm = data;
	struct dpu_crtc *dpu_crtc;
	bool found = false;
	int ret;

	list_for_each_entry(dpu_crtc, &dpu_drm->crtc_list, node) {
		if (dpu_crtc->np == dev->of_node) {
			found = true;
			break;
		}
	}

	if (!found) {
		drm_err(&dpu_drm->base, "failed to find CRTC OF node\n");
		return -ENODEV;
	}

	dpu_crtc->dev = dev;

	ret = dpu_crtc_init(dpu_crtc, pdata, dpu_drm);
	if (ret)
		return ret;

	dev_set_drvdata(dev, dpu_crtc);

	return ret;
}

static void dpu_crtc_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct dpu_crtc *dpu_crtc = dev_get_drvdata(dev);

	dpu_crtc_put_resources(dpu_crtc);
}

static const struct component_ops dpu_crtc_ops = {
	.bind = dpu_crtc_bind,
	.unbind = dpu_crtc_unbind,
};

static int dpu_crtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->platform_data)
		return -EINVAL;

	return component_add(dev, &dpu_crtc_ops);
}

static int dpu_crtc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dpu_crtc_ops);
	return 0;
}

struct platform_driver dpu_crtc_driver = {
	.driver = {
		.name = "imx-dpu-crtc",
	},
	.probe = dpu_crtc_probe,
	.remove = dpu_crtc_remove,
};
