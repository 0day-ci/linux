// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */
#include <linux/workqueue.h>
#include "intel_pxp.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "gem/i915_gem_context.h"
#include "gt/intel_context.h"
#include "i915_drv.h"

/* KCR register definitions */
#define KCR_INIT _MMIO(0x320f0)

/* Setting KCR Init bit is required after system boot */
#define KCR_INIT_ALLOW_DISPLAY_ME_WRITES REG_BIT(14)

static void kcr_pxp_enable(struct intel_gt *gt)
{
	intel_uncore_write(gt->uncore, KCR_INIT,
			   _MASKED_BIT_ENABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES));
}

static void kcr_pxp_disable(struct intel_gt *gt)
{
	intel_uncore_write(gt->uncore, KCR_INIT,
			   _MASKED_BIT_DISABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES));
}

static int create_vcs_context(struct intel_pxp *pxp)
{
	static struct lock_class_key pxp_lock;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct intel_engine_cs *engine;
	struct intel_context *ce;

	/*
	 * Find the first VCS engine present. We're guaranteed there is one
	 * if we're in this function due to the check in has_pxp
	 */
	for (engine = gt->engine_class[VIDEO_DECODE_CLASS][0]; !engine; engine++);
	GEM_BUG_ON(!engine || engine->class != VIDEO_DECODE_CLASS);

	ce = intel_engine_create_pinned_context(engine, I915_GEM_HWS_PXP_ADDR,
						SZ_4K, NULL, &pxp_lock,
						"pxp_context");
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "failed to create VCS ctx for PXP\n");
		return PTR_ERR(ce);
	}

	pxp->ce = ce;

	return 0;
}

static void destroy_vcs_context(struct intel_pxp *pxp)
{
	intel_engine_destroy_pinned_context(fetch_and_zero(&pxp->ce));
}

void intel_pxp_init(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	int ret;

	if (!HAS_PXP(gt->i915))
		return;

	spin_lock_init(&pxp->lock);
	INIT_LIST_HEAD(&pxp->protected_objects);

	/*
	 * we'll use the completion to check if there is a termination pending,
	 * so we start it as completed and we reinit it when a termination
	 * is triggered.
	 */
	init_completion(&pxp->termination);
	complete_all(&pxp->termination);

	INIT_WORK(&pxp->session_work, intel_pxp_session_work);

	ret = create_vcs_context(pxp);
	if (ret)
		return;

	ret = intel_pxp_tee_component_init(pxp);
	if (ret)
		goto out_context;

	drm_info(&gt->i915->drm, "Protected Xe Path (PXP) protected content support initialized\n");

	return;

out_context:
	destroy_vcs_context(pxp);
}

void intel_pxp_fini(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_tee_component_fini(pxp);

	destroy_vcs_context(pxp);
}

void intel_pxp_mark_termination_in_progress(struct intel_pxp *pxp)
{
	pxp->arb_is_valid = false;
	reinit_completion(&pxp->termination);
}

static void intel_pxp_queue_termination(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);

	/*
	 * We want to get the same effect as if we received a termination
	 * interrupt, so just pretend that we did.
	 */
	spin_lock_irq(&gt->irq_lock);
	intel_pxp_mark_termination_in_progress(pxp);
	pxp->session_events |= PXP_TERMINATION_REQUEST;
	queue_work(system_unbound_wq, &pxp->session_work);
	spin_unlock_irq(&gt->irq_lock);
}

/*
 * the arb session is restarted from the irq work when we receive the
 * termination completion interrupt
 */
int intel_pxp_wait_for_arb_start(struct intel_pxp *pxp)
{
	int ret;

	if (!intel_pxp_is_enabled(pxp))
		return 0;

	ret = wait_for_completion_timeout(&pxp->termination,
					  msecs_to_jiffies(100));

	/* the wait returns 0 on failure */
	if (ret)
		ret = 0;
	else
		return -ETIMEDOUT;

	if (!pxp->arb_is_valid)
		return -EIO;

	return 0;
}

void intel_pxp_init_hw(struct intel_pxp *pxp)
{
	kcr_pxp_enable(pxp_to_gt(pxp));
	intel_pxp_irq_enable(pxp);

	/*
	 * the session could've been attacked while we weren't loaded, so
	 * handle it as if it was and re-create it.
	 */
	intel_pxp_queue_termination(pxp);
}

void intel_pxp_fini_hw(struct intel_pxp *pxp)
{
	kcr_pxp_disable(pxp_to_gt(pxp));

	intel_pxp_irq_disable(pxp);
}

int intel_pxp_object_add(struct drm_i915_gem_object *obj)
{
	struct intel_pxp *pxp = &to_i915(obj->base.dev)->gt.pxp;

	if (!intel_pxp_is_enabled(pxp))
		return -ENODEV;

	if (!list_empty(&obj->pxp_link))
		return -EEXIST;

	spin_lock_irq(&pxp->lock);
	list_add(&obj->pxp_link, &pxp->protected_objects);
	spin_unlock_irq(&pxp->lock);

	return 0;
}

void intel_pxp_object_remove(struct drm_i915_gem_object *obj)
{
	struct intel_pxp *pxp = &to_i915(obj->base.dev)->gt.pxp;

	if (!intel_pxp_is_enabled(pxp))
		return;

	spin_lock_irq(&pxp->lock);
	list_del_init(&obj->pxp_link);
	spin_unlock_irq(&pxp->lock);
}

void intel_pxp_invalidate(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct drm_i915_gem_object *obj, *tmp;
	struct i915_gem_context *ctx, *cn;

	/* delete objects that have been used with the invalidated session */
	spin_lock_irq(&pxp->lock);
	list_for_each_entry_safe(obj, tmp, &pxp->protected_objects, pxp_link) {
		if (i915_gem_object_has_pages(obj))
			list_del_init(&obj->pxp_link);
	}
	spin_unlock_irq(&pxp->lock);

	/* ban all contexts marked as protected */
	spin_lock_irq(&i915->gem.contexts.lock);
	list_for_each_entry_safe(ctx, cn, &i915->gem.contexts.list, link) {
		struct i915_gem_engines_iter it;
		struct intel_context *ce;

		if (!kref_get_unless_zero(&ctx->ref))
			continue;

		if (likely(!i915_gem_context_uses_protected_content(ctx)) ||
		    i915_gem_context_invalidated(ctx)) {
			i915_gem_context_put(ctx);
			continue;
		}

		spin_unlock_irq(&i915->gem.contexts.lock);

		/*
		 * Note that by the time we get here the HW keys are already
		 * long gone, so any batch using them that's already on the
		 * engines is very likely a lost cause (and it has probably
		 * already hung the engine). Therefore, we skip attempting to
		 * pull the running context out of the HW and we prioritize
		 * bringing the session back as soon as possible.
		 */
		for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
			/* only invalidate if at least one ce was allocated */
			if (test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
				intel_context_set_banned(ce);
				i915_gem_context_set_invalid(ctx);
			}
		}
		i915_gem_context_unlock_engines(ctx);

		spin_lock_irq(&i915->gem.contexts.lock);
		list_safe_reset_next(ctx, cn, link);
		i915_gem_context_put(ctx);
	}
	spin_unlock_irq(&i915->gem.contexts.lock);
}

