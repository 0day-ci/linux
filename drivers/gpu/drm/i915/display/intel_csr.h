/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CSR_H__
#define __INTEL_CSR_H__

#include <drm/drm_util.h>
#include "intel_wakeref.h"
#include "i915_reg.h"

struct drm_i915_private;

#define CSR_VERSION(major, minor)	((major) << 16 | (minor))
#define CSR_VERSION_MAJOR(version)	((version) >> 16)
#define CSR_VERSION_MINOR(version)	((version) & 0xffff)

void intel_csr_ucode_init(struct drm_i915_private *i915);
void intel_csr_load_program(struct drm_i915_private *i915);
void intel_csr_ucode_fini(struct drm_i915_private *i915);
void intel_csr_ucode_suspend(struct drm_i915_private *i915);
void intel_csr_ucode_resume(struct drm_i915_private *i915);

struct intel_csr {
	struct work_struct work;
	const char *fw_path;
	u32 required_version;
	u32 max_fw_size; /* bytes */
	u32 *dmc_payload;
	u32 dmc_fw_size; /* dwords */
	u32 version;
	u32 mmio_count;
	i915_reg_t mmioaddr[20];
	u32 mmiodata[20];
	u32 dc_state;
	u32 target_dc_state;
	u32 allowed_dc_mask;
	intel_wakeref_t wakeref;
};

bool intel_csr_has_dmc_payload(struct drm_i915_private *dev_priv);

#endif /* __INTEL_CSR_H__ */
