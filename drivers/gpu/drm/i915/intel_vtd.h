/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_VTD_H__
#define __INTEL_VTD_H__

#include <linux/device.h>
#include <linux/types.h>
#include <asm/hypervisor.h>

#include "i915_drv.h"

struct drm_printer;

static inline bool intel_vtd_run_as_guest(void)
{
	return !hypervisor_is_type(X86_HYPER_NATIVE);
}

static inline bool intel_vtd_active(struct drm_i915_private *i915)
{
	if (device_iommu_mapped(i915->drm.dev))
		return true;

	/* Running as a guest, we assume the host is enforcing VT'd */
	return intel_vtd_run_as_guest();
}

static inline bool intel_vtd_scanout_needs_wa(struct drm_i915_private *i915)
{
	return GRAPHICS_VER(i915) >= 6 && intel_vtd_active(i915);
}

static inline bool
intel_vtd_ggtt_update_needs_wa(struct drm_i915_private *i915)
{
	return IS_BROXTON(i915) && intel_vtd_active(i915);
}

static inline bool
intel_vtd_vm_no_concurrent_access_wa(struct drm_i915_private *i915)
{
	return IS_CHERRYVIEW(i915) || intel_vtd_ggtt_update_needs_wa(i915);
}

void
intel_vtd_print_iommu_status(struct drm_i915_private *i915, struct drm_printer *p);

#endif /* __INTEL_VTD_H__ */
