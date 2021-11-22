// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i915_drv.h"
#include "i915_memcpy.h"
#include "gt/intel_gt.h"

#include "intel_guc_fwif.h"
#include "intel_guc_capture.h"

/* Define all device tables of GuC error capture register lists */

/********************************* Gen12 LP  *********************************/
/************** GLOBAL *************/
struct __guc_mmio_reg_descr gen12lp_global_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/********** RENDER/COMPUTE *********/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_rc_class_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_rc_inst_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/************* MEDIA-VD ************/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_vd_class_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_vd_inst_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/************* MEDIA-VEC ***********/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_vec_class_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_vec_inst_regs[] = {
	{SWF_ILK(0),               0,      0, "SWF_ILK0"},
	/* Add additional register list */
};

/********** List of lists **********/
struct __guc_mmio_reg_descr_group gen12lp_lists[] = {
	{
		.list = gen12lp_global_regs,
		.num_regs = (sizeof(gen12lp_global_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_GLOBAL,
		.engine = 0
	},
	{
		.list = gen12lp_rc_class_regs,
		.num_regs = (sizeof(gen12lp_rc_class_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
		.engine = RENDER_CLASS
	},
	{
		.list = gen12lp_rc_inst_regs,
		.num_regs = (sizeof(gen12lp_rc_inst_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
		.engine = RENDER_CLASS
	},
	{
		.list = gen12lp_vd_class_regs,
		.num_regs = (sizeof(gen12lp_vd_class_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
		.engine = VIDEO_DECODE_CLASS
	},
	{
		.list = gen12lp_vd_inst_regs,
		.num_regs = (sizeof(gen12lp_vd_inst_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
		.engine = VIDEO_DECODE_CLASS
	},
	{
		.list = gen12lp_vec_class_regs,
		.num_regs = (sizeof(gen12lp_vec_class_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
		.engine = VIDEO_ENHANCEMENT_CLASS
	},
	{
		.list = gen12lp_vec_inst_regs,
		.num_regs = (sizeof(gen12lp_vec_inst_regs) / sizeof(struct __guc_mmio_reg_descr)),
		.owner = GUC_CAPTURE_LIST_INDEX_PF,
		.type = GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
		.engine = VIDEO_ENHANCEMENT_CLASS
	},
	{NULL, 0, 0, 0, 0}
};

/************ FIXME: Populate tables for other devices in subsequent patch ************/

static struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct drm_i915_private *dev_priv)
{
	if (IS_TIGERLAKE(dev_priv) || IS_ROCKETLAKE(dev_priv) ||
	    IS_ALDERLAKE_S(dev_priv) || IS_ALDERLAKE_P(dev_priv)) {
		return gen12lp_lists;
	}

	return NULL;
}

static inline struct __guc_mmio_reg_descr_group *
guc_capture_get_one_list(struct __guc_mmio_reg_descr_group *reglists, u32 owner, u32 type, u32 id)
{
	int i = 0;

	if (!reglists)
		return NULL;
	while (reglists[i].list) {
		if (reglists[i].owner == owner &&
		    reglists[i].type == type) {
			if (reglists[i].type == GUC_CAPTURE_LIST_TYPE_GLOBAL ||
			    reglists[i].engine == id) {
				return &reglists[i];
			}
		}
		++i;
	}
	return NULL;
}

static inline void
warn_with_capture_list_identifier(struct drm_i915_private *i915, char *msg,
				  u32 owner, u32 type, u32 classid)
{
	const char *ownerstr[GUC_CAPTURE_LIST_INDEX_MAX] = {"PF", "VF"};
	const char *typestr[GUC_CAPTURE_LIST_TYPE_MAX - 1] = {"Class", "Instance"};
	const char *classstr[GUC_LAST_ENGINE_CLASS + 1] = {"Render", "Video", "VideoEnhance",
							   "Blitter", "Reserved"};
	static const char unknownstr[] = "unknown";

	if (type == GUC_CAPTURE_LIST_TYPE_GLOBAL)
		drm_warn(&i915->drm, "GuC-capture: %s for %s Global-Registers.\n", msg,
			 (owner < GUC_CAPTURE_LIST_INDEX_MAX) ? ownerstr[owner] : unknownstr);
	else
		drm_warn(&i915->drm, "GuC-capture: %s for %s %s-Registers on %s-Engine\n", msg,
			 (owner < GUC_CAPTURE_LIST_INDEX_MAX) ? ownerstr[owner] : unknownstr,
			 (type < GUC_CAPTURE_LIST_TYPE_MAX) ? typestr[type - 1] :  unknownstr,
			 (classid < GUC_LAST_ENGINE_CLASS + 1) ? classstr[classid] : unknownstr);
}

int intel_guc_capture_list_count(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
				 u16 *num_entries)
{
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;
	struct __guc_mmio_reg_descr_group *reglists = guc->capture.reglists;
	struct __guc_mmio_reg_descr_group *match;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, classid);
	if (match) {
		*num_entries = match->num_regs;
		return 0;
	}

	warn_with_capture_list_identifier(dev_priv, "Missing register list size", owner, type,
					  classid);

	return -ENODATA;
}

int intel_guc_capture_list_init(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
				struct guc_mmio_reg *ptr, u16 num_entries)
{
	u32 j = 0;
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;
	struct __guc_mmio_reg_descr_group *reglists = guc->capture.reglists;
	struct __guc_mmio_reg_descr_group *match;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, classid);
	if (match) {
		while (j < num_entries && j < match->num_regs) {
			ptr[j].offset = match->list[j].reg.reg;
			ptr[j].value = 0xDEADF00D;
			ptr[j].flags = match->list[j].flags;
			ptr[j].mask = match->list[j].mask;
			++j;
		}
		return 0;
	}

	warn_with_capture_list_identifier(dev_priv, "Missing register list init", owner, type,
					  classid);

	return -ENODATA;
}

void intel_guc_capture_destroy(struct intel_guc *guc)
{
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;

	guc->capture.reglists = guc_capture_get_device_reglist(dev_priv);
	return 0;
}
