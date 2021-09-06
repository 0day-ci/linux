/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _I915_GEM_TTM_PM_H_
#define _I915_GEM_TTM_PM_H_

#include <linux/types.h>

struct intel_memory_region;
struct drm_i915_gem_object;

int i915_ttm_backup_region(struct intel_memory_region *mr, bool allow_gpu,
			   bool backup_pinned);

void i915_ttm_recover_region(struct intel_memory_region *mr);

int i915_ttm_restore_region(struct intel_memory_region *mr, bool allow_gpu);

/* Internal I915 TTM functions below. */
void i915_ttm_backup_free(struct drm_i915_gem_object *obj);

#endif
