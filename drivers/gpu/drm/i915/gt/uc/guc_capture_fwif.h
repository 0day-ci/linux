/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#ifndef _INTEL_GUC_CAPTURE_FWIF_H
#define _INTEL_GUC_CAPTURE_FWIF_H

#include <linux/types.h>
#include "intel_guc_fwif.h"

struct intel_guc;

struct __guc_mmio_reg_descr {
	i915_reg_t reg;
	u32 flags;
	u32 mask;
	const char *regname;
};

struct __guc_mmio_reg_descr_group {
	struct __guc_mmio_reg_descr *list;
	u32 num_regs;
	u32 owner; /* see enum guc_capture_owner */
	u32 type; /* see enum guc_capture_type */
	u32 engine; /* as per MAX_ENGINE_CLASS */
};

struct __guc_state_capture_priv {
	struct __guc_mmio_reg_descr_group *reglists;
	u16 num_instance_regs[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u16 num_class_regs[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u16 num_global_regs[GUC_CAPTURE_LIST_INDEX_MAX];
};

#endif /* _INTEL_GUC_CAPTURE_FWIF_H */
