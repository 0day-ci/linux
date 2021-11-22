/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#ifndef _INTEL_GUC_CAPTURE_H
#define _INTEL_GUC_CAPTURE_H

#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "intel_guc_fwif.h"

struct intel_guc;

struct __guc_mmio_reg_descr {
	i915_reg_t reg;
	u32 flags;
	u32 mask;
	char *regname;
};

struct __guc_mmio_reg_descr_group {
	struct __guc_mmio_reg_descr *list;
	u32 num_regs;
	u32 owner; /* see enum guc_capture_owner */
	u32 type; /* see enum guc_capture_type */
	u32 engine; /* as per MAX_ENGINE_CLASS */
};

struct intel_guc_state_capture {
	struct __guc_mmio_reg_descr_group *reglists;
	u16 num_instance_regs[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u16 num_class_regs[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u16 num_global_regs[GUC_CAPTURE_LIST_INDEX_MAX];
	int instance_list_size;
	int class_list_size;
	int global_list_size;
};

int intel_guc_capture_list_count(struct intel_guc *guc, u32 owner, u32 type, u32 class,
				 u16 *num_entries);
int intel_guc_capture_list_init(struct intel_guc *guc, u32 owner, u32 type, u32 class,
				struct guc_mmio_reg *ptr, u16 num_entries);
void intel_guc_capture_destroy(struct intel_guc *guc);
int intel_guc_capture_init(struct intel_guc *guc);

#endif /* _INTEL_GUC_CAPTURE_H */
