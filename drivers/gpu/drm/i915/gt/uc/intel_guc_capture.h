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
	int num_ext;
	struct __guc_mmio_reg_descr * ext;
};

struct intel_guc_capture_out_data_header {
	u32 reserved1;
	u32 info;
		#define GUC_CAPTURE_DATAHDR_SRC_TYPE GENMASK(3, 0) /* as per enum guc_capture_type */
		#define GUC_CAPTURE_DATAHDR_SRC_CLASS GENMASK(7, 4) /* as per GUC_MAX_ENGINE_CLASSES */
		#define GUC_CAPTURE_DATAHDR_SRC_INSTANCE GENMASK(11, 8)
	u32 lrca; /* if type-instance, LRCA (address) that hung, else set to ~0 */
	u32 guc_ctx_id; /* if type-instance, context index of hung context, else set to ~0 */
	u32 num_mmios;
		#define GUC_CAPTURE_DATAHDR_NUM_MMIOS GENMASK(9, 0)
};

struct intel_guc_capture_out_data {
	struct intel_guc_capture_out_data_header capture_header;
	struct guc_mmio_reg capture_list[0];
};

enum guc_capture_group_types {
	GUC_STATE_CAPTURE_GROUP_TYPE_FULL,
	GUC_STATE_CAPTURE_GROUP_TYPE_PARTIAL,
	GUC_STATE_CAPTURE_GROUP_TYPE_MAX,
};

struct intel_guc_capture_out_group_header {
	u32 reserved1;
	u32 info;
		#define GUC_CAPTURE_GRPHDR_SRC_NUMCAPTURES GENMASK(7, 0)
		#define GUC_CAPTURE_GRPHDR_SRC_CAPTURE_TYPE GENMASK(15, 8)
};

struct intel_guc_capture_out_group {
	struct intel_guc_capture_out_group_header group_header;
	struct intel_guc_capture_out_data group_lists[0];
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
