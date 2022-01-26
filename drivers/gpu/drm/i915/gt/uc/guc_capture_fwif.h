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
	int num_ext;
	struct __guc_mmio_reg_descr *ext;
};

struct guc_state_capture_header_t {
	u32 owner;
		#define CAP_HDR_CAPTURE_VFID GENMASK(7, 0)
	u32 info;
		#define CAP_HDR_CAPTURE_TYPE GENMASK(3, 0) /* see enum guc_capture_type */
		#define CAP_HDR_ENGINE_CLASS GENMASK(7, 4) /* see GUC_MAX_ENGINE_CLASSES */
		#define CAP_HDR_ENGINE_INSTANCE GENMASK(11, 8)
	u32 lrca; /* if type-instance, LRCA (address) that hung, else set to ~0 */
	u32 guc_id; /* if type-instance, context index of hung context, else set to ~0 */
	u32 num_mmios;
		#define CAP_HDR_NUM_MMIOS GENMASK(9, 0)
} __packed;

struct guc_state_capture_t {
	struct guc_state_capture_header_t header;
	struct guc_mmio_reg mmio_entries[0];
} __packed;

enum guc_capture_group_types {
	GUC_STATE_CAPTURE_GROUP_TYPE_FULL,
	GUC_STATE_CAPTURE_GROUP_TYPE_PARTIAL,
	GUC_STATE_CAPTURE_GROUP_TYPE_MAX,
};

struct guc_state_capture_group_header_t {
	u32 owner;
		#define CAP_GRP_HDR_CAPTURE_VFID GENMASK(7, 0)
	u32 info;
		#define CAP_GRP_HDR_NUM_CAPTURES GENMASK(7, 0)
		#define CAP_GRP_HDR_CAPTURE_TYPE GENMASK(15, 8) /* guc_capture_group_types */
} __packed;

struct guc_state_capture_group_t {
	struct guc_state_capture_group_header_t grp_header;
	struct guc_state_capture_t capture_entries[0];
} __packed;

struct __guc_capture_parsed_output {
	/*
	 * a single set of 3 capture lists: a global-list
	 * an engine-class-list and an engine-instance list.
	 * outlist in __guc_capture_parsed_output will keep
	 * a linked list of these nodes that will eventually
	 * be detached from outlist and attached into to
	 * i915_gpu_codedump in response to a context reset
	 */
	struct list_head link;
	bool is_partial;
	u32 eng_class;
	u32 eng_inst;
	u32 guc_id;
	u32 lrca;
	struct gcap_reg_list_info {
		u32 vfid;
		u32 num;
		struct guc_mmio_reg *regs;
	} reginfo[GUC_CAPTURE_LIST_TYPE_MAX];
	#define GCAP_PARSED_REGLIST_INDEX_GLOBAL   BIT(GUC_CAPTURE_LIST_TYPE_GLOBAL)
	#define GCAP_PARSED_REGLIST_INDEX_ENGCLASS BIT(GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS)
	#define GCAP_PARSED_REGLIST_INDEX_ENGINST  BIT(GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE)
};

#define MAX_NODE_LINKLIST_THRESHOLD     24
	/* The maximum number of allocated __guc_capture_parsed_output nodes
	 * that we shall keep in outlist. If we receive an error-capture
	 * notification and need to allocate another node but have hit this
	 * threshold, we shall free the oldest entry and add a new one (FIFO).
	 */

struct __guc_capture_bufstate {
	unsigned int size;
	void *data;
	unsigned int rd;
	unsigned int wr;
};

struct __guc_state_capture_priv {
	struct __guc_mmio_reg_descr_group *reglists;
	u16 num_instance_regs[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u16 num_class_regs[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u16 num_global_regs[GUC_CAPTURE_LIST_INDEX_MAX];
	/* An interim linked list of parsed GuC error-capture-output before
	 * reporting with formatting. Each node in this linked list shall
	 * contain a single engine-capture including global, engine-class and
	 * engine-instance register dumps as per guc_capture_parsed_output_node
	 */
	struct list_head outlist;
	int listcount; /* see MAX_NODE_LINKLIST_THRESHOLD */
};

#endif /* _INTEL_GUC_CAPTURE_FWIF_H */
