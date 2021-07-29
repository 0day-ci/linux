/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_H__
#define __INTEL_PXP_H__

#include "gt/intel_gt_types.h"
#include "intel_pxp_types.h"

struct drm_i915_gem_object;

static inline struct intel_gt *pxp_to_gt(const struct intel_pxp *pxp)
{
	return container_of(pxp, struct intel_gt, pxp);
}

static inline bool intel_pxp_is_enabled(const struct intel_pxp *pxp)
{
	return pxp->ce;
}

static inline bool intel_pxp_is_active(const struct intel_pxp *pxp)
{
	return pxp->arb_is_valid;
}

#ifdef CONFIG_DRM_I915_PXP
void intel_pxp_init(struct intel_pxp *pxp);
void intel_pxp_fini(struct intel_pxp *pxp);

void intel_pxp_init_hw(struct intel_pxp *pxp);
void intel_pxp_fini_hw(struct intel_pxp *pxp);

void intel_pxp_mark_termination_in_progress(struct intel_pxp *pxp);
int intel_pxp_wait_for_arb_start(struct intel_pxp *pxp);

int intel_pxp_object_add(struct drm_i915_gem_object *obj);
void intel_pxp_object_remove(struct drm_i915_gem_object *obj);

void intel_pxp_invalidate(struct intel_pxp *pxp);
#else
static inline void intel_pxp_init(struct intel_pxp *pxp)
{
}

static inline void intel_pxp_fini(struct intel_pxp *pxp)
{
}

static inline int intel_pxp_wait_for_arb_start(struct intel_pxp *pxp)
{
	return 0;
}

static inline int intel_pxp_object_add(struct drm_i915_gem_object *obj)
{
	return 0;
}
static inline void intel_pxp_object_remove(struct drm_i915_gem_object *obj)
{
}
#endif

#endif /* __INTEL_PXP_H__ */
