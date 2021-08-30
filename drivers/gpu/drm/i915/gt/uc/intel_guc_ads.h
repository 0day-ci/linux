/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_ADS_H_
#define _INTEL_GUC_ADS_H_

#include <linux/types.h>

struct temp_regset {
	struct guc_mmio_reg *registers;
	u32 used;
	u32 size;
};

struct intel_guc;
struct drm_printer;

void guc_mmio_reg_add(struct temp_regset *regset,
		      u32 offset, u32 flags);
#define GUC_MMIO_REG_ADD(regset, reg, masked) \
	guc_mmio_reg_add(regset, \
			 i915_mmio_reg_offset((reg)), \
			 (masked) ? GUC_REGSET_MASKED : 0)

int intel_guc_ads_create(struct intel_guc *guc);
void intel_guc_ads_destroy(struct intel_guc *guc);
void intel_guc_ads_init_late(struct intel_guc *guc);
void intel_guc_ads_reset(struct intel_guc *guc);
void intel_guc_ads_print_policy_info(struct intel_guc *guc,
				     struct drm_printer *p);

#endif
