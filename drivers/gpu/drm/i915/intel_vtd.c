// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_print.h>

#include "intel_vtd.h"

void
intel_vtd_print_iommu_status(struct drm_i915_private *i915, struct drm_printer *p)
{
	drm_printf(p, "iommu: %s\n", enableddisabled(intel_vtd_active(i915)));
}
