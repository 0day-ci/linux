// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#include <linux/types.h>

#include <drm/drm_print.h>

#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "guc_capture_fwif.h"
#include "intel_guc_fwif.h"
#include "i915_drv.h"
#include "i915_memcpy.h"

/*
 * Define all device tables of GuC error capture register lists
 * NOTE: For engine-registers, GuC only needs the register offsets
 *       from the engine-mmio-base
 */
/* XE_LPD - Global */
static struct __guc_mmio_reg_descr xe_lpd_global_regs[] = {
	{GEN12_RING_FAULT_REG,     0,      0, "GEN12_RING_FAULT_REG"}
};

/* XE_LPD - Render / Compute Per-Class */
static struct __guc_mmio_reg_descr xe_lpd_rc_class_regs[] = {
	{EIR,                      0,      0, "EIR"}
};

/* XE_LPD - Render / Compute Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_rc_inst_regs[] = {
	{RING_HEAD(0),             0,      0, "RING_HEAD"},
	{RING_TAIL(0),             0,      0, "RING_TAIL"},
};

/* XE_LPD - Media Decode/Encode Per-Class */
static struct __guc_mmio_reg_descr xe_lpd_vd_class_regs[] = {
};

/* XE_LPD - Media Decode/Encode Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_vd_inst_regs[] = {
	{RING_HEAD(0),             0,      0, "RING_HEAD"},
	{RING_TAIL(0),             0,      0, "RING_TAIL"},
};

/* XE_LPD - Video Enhancement Per-Class */
static struct __guc_mmio_reg_descr xe_lpd_vec_class_regs[] = {
};

/* XE_LPD - Video Enhancement Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_vec_inst_regs[] = {
	{RING_HEAD(0),             0,      0, "RING_HEAD"},
	{RING_TAIL(0),             0,      0, "RING_TAIL"},
};

#define TO_GCAP_DEF_OWNER(x) (GUC_CAPTURE_LIST_INDEX_##x)
#define TO_GCAP_DEF_TYPE(x) (GUC_CAPTURE_LIST_TYPE_##x)
#define MAKE_REGLIST(regslist, regsowner, regstype, class) \
	{ \
		.list = regslist, \
		.num_regs = ARRAY_SIZE(regslist), \
		.owner = TO_GCAP_DEF_OWNER(regsowner), \
		.type = TO_GCAP_DEF_TYPE(regstype), \
		.engine = class, \
	}

/* List of lists */
static struct __guc_mmio_reg_descr_group xe_lpd_lists[] = {
	MAKE_REGLIST(xe_lpd_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_lpd_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_vd_class_regs, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vec_class_regs, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	{}
};

static struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (IS_TIGERLAKE(i915) || IS_ROCKETLAKE(i915) ||
	    IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915)) {
		/*
		 * For certain engine classes, there are slice and subslice
		 * level registers requiring steering. We allocate and populate
		 * these at init time based on hw config add it as an extension
		 * list at the end of the pre-populated render list.
		 */
		return xe_lpd_lists;
	}

	return NULL;
}

static struct __guc_mmio_reg_descr_group *
guc_capture_get_one_list(struct __guc_mmio_reg_descr_group *reglists, u32 owner, u32 type, u32 id)
{
	int i;

	if (!reglists)
		return NULL;

	for (i = 0; reglists[i].list; i++) {
		if (reglists[i].owner == owner && reglists[i].type == type &&
		    (reglists[i].engine == id || reglists[i].type == GUC_CAPTURE_LIST_TYPE_GLOBAL))
		return &reglists[i];
	}

	return NULL;
}

static const char *
guc_capture_stringify_owner(u32 owner)
{
	switch (owner) {
	case GUC_CAPTURE_LIST_INDEX_PF:
		return "PF";
	case GUC_CAPTURE_LIST_INDEX_VF:
		return "VF";
	default:
		return "unknown";
	}

	return "";
}

static const char *
guc_capture_stringify_type(u32 type)
{
	switch (type) {
	case GUC_CAPTURE_LIST_TYPE_GLOBAL:
		return "Global";
	case GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS:
		return "Class";
	case GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE:
		return "Instance";
	default:
		return "unknown";
	}

	return "";
}

static const char *
guc_capture_stringify_engclass(u32 class)
{
	switch (class) {
	case GUC_RENDER_CLASS:
		return "Render";
	case GUC_VIDEO_CLASS:
		return "Video";
	case GUC_VIDEOENHANCE_CLASS:
		return "VideoEnhance";
	case GUC_BLITTER_CLASS:
		return "Blitter";
	case GUC_RESERVED_CLASS:
		return "Reserved";
	default:
		return "unknown";
	}

	return "";
}

static void
guc_capture_warn_with_list_info(struct drm_i915_private *i915, char *msg,
				u32 owner, u32 type, u32 classid)
{
	if (type == GUC_CAPTURE_LIST_TYPE_GLOBAL)
		drm_dbg(&i915->drm, "GuC-capture: %s for %s %s-Registers.\n", msg,
			guc_capture_stringify_owner(owner), guc_capture_stringify_type(type));
	else
		drm_dbg(&i915->drm, "GuC-capture: %s for %s %s-Registers on %s-Engine\n", msg,
			guc_capture_stringify_owner(owner), guc_capture_stringify_type(type),
			guc_capture_stringify_engclass(classid));
}

static int
guc_capture_list_init(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
		      struct guc_mmio_reg *ptr, u16 num_entries)
{
	u32 j = 0;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct __guc_mmio_reg_descr_group *reglists = guc->capture.priv->reglists;
	struct __guc_mmio_reg_descr_group *match;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, classid);
	if (match) {
		for (j = 0; j < num_entries && j < match->num_regs; ++j) {
			ptr[j].offset = match->list[j].reg.reg;
			ptr[j].value = 0xDEADF00D;
			ptr[j].flags = match->list[j].flags;
			ptr[j].mask = match->list[j].mask;
		}
		return 0;
	}

	guc_capture_warn_with_list_info(i915, "Missing register list init", owner, type,
					classid);

	return -ENODATA;
}

static int
guc_capture_fill_reglist(struct intel_guc *guc, struct guc_ads *ads,
			 u32 owner, int type, int classid, u16 numregs,
			 u8 **p_virt, u32 *p_ggtt, u32 null_ggtt)
{
	struct guc_debug_capture_list *listnode;
	u32 *p_capturelist_ggtt;
	int size = 0;

	/*
	 * For enabled capture lists, we not only need to call capture module to help
	 * populate the list-descriptor into the correct ads capture structures, but
	 * we also need to increment the virtual pointers and ggtt offsets so that
	 * caller has the subsequent gfx memory location.
	 */
	size = PAGE_ALIGN((sizeof(struct guc_debug_capture_list)) +
			  (numregs * sizeof(struct guc_mmio_reg)));
	/* if caller hasn't allocated ADS blob, return size and counts, we're done */
	if (!ads)
		return size;

	/*
	 * If caller allocated ADS blob, populate the capture register descriptors into
	 * the designated ADS location based on list-owner, list-type and engine-classid
	 */
	if (type == GUC_CAPTURE_LIST_TYPE_GLOBAL)
		p_capturelist_ggtt = &ads->capture_global[owner];
	else if (type == GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS)
		p_capturelist_ggtt = &ads->capture_class[owner][classid];
	else /*GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE*/
		p_capturelist_ggtt = &ads->capture_instance[owner][classid];

	if (!numregs) {
		*p_capturelist_ggtt = null_ggtt;
	} else {
		/* get ptr and populate header info: */
		*p_capturelist_ggtt = *p_ggtt;
		listnode = (struct guc_debug_capture_list *)*p_virt;
		*p_ggtt += sizeof(struct guc_debug_capture_list);
		*p_virt += sizeof(struct guc_debug_capture_list);
		listnode->header.info = FIELD_PREP(GUC_CAPTURELISTHDR_NUMDESCR, numregs);

		/* get ptr and populate register descriptor list: */
		guc_capture_list_init(guc, owner, type, classid,
				      (struct guc_mmio_reg *)*p_virt,
				      numregs);

		/* increment ptrs for that header: */
		*p_ggtt += size - sizeof(struct guc_debug_capture_list);
		*p_virt += size - sizeof(struct guc_debug_capture_list);
	}

	return size;
}

static int
guc_capture_list_count(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
		       u16 *num_entries)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct __guc_mmio_reg_descr_group *reglists = guc->capture.priv->reglists;
	struct __guc_mmio_reg_descr_group *match;

	if (!reglists)
		return -ENODEV;

	match = guc_capture_get_one_list(reglists, owner, type, classid);
	if (!match) {
		guc_capture_warn_with_list_info(i915, "Missing register list size",
						owner, type, classid);
		return -ENODATA;
	}

	*num_entries = match->num_regs;
	return 0;
}

static void
guc_capture_fill_engine_enable_masks(struct intel_gt *gt,
				     struct guc_gt_system_info *info)
{
	info->engine_enabled_masks[GUC_RENDER_CLASS] = 1;
	info->engine_enabled_masks[GUC_BLITTER_CLASS] = 1;
	info->engine_enabled_masks[GUC_VIDEO_CLASS] = VDBOX_MASK(gt);
	info->engine_enabled_masks[GUC_VIDEOENHANCE_CLASS] = VEBOX_MASK(gt);
}

int intel_guc_capture_prep_lists(struct intel_guc *guc, struct guc_ads *blob, u32 blob_ggtt,
				 u32 capture_offset, struct guc_gt_system_info *sysinfo)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct guc_gt_system_info *info, local_info;
	struct guc_debug_capture_list *listnode;
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct __guc_state_capture_priv *gc = guc->capture.priv;
	int i, j, size;
	u32 ggtt, null_ggtt, alloc_size = 0;
	u16 tmpnumreg = 0;
	u8 *ptr = NULL;

	GEM_BUG_ON(!gc);

	if (blob) {
		ptr = ((u8 *)blob) + capture_offset;
		ggtt = blob_ggtt + capture_offset;
		GEM_BUG_ON(!sysinfo);
		info = sysinfo;
	} else {
		memset(&local_info, 0, sizeof(local_info));
		info = &local_info;
		guc_capture_fill_engine_enable_masks(gt, info);
	}

	/* first, set aside the first page for a capture_list with zero descriptors */
	alloc_size = PAGE_SIZE;
	if (blob) {
		listnode = (struct guc_debug_capture_list *)ptr;
		listnode->header.info = FIELD_PREP(GUC_CAPTURELISTHDR_NUMDESCR, 0);
		null_ggtt = ggtt;
		ggtt += PAGE_SIZE;
		ptr +=  PAGE_SIZE;
	}

#define COUNT_REGS guc_capture_list_count
#define FILL_REGS guc_capture_fill_reglist
#define TYPE_GLOBAL GUC_CAPTURE_LIST_TYPE_GLOBAL
#define TYPE_CLASS GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS
#define TYPE_INSTANCE GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE
#define OWNER2STR guc_capture_stringify_owner
#define ENGCLS2STR guc_capture_stringify_engclass
#define TYPE2STR guc_capture_stringify_type

	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; i++) {
		for (j = 0; j < GUC_MAX_ENGINE_CLASSES; j++) {
			if (!info->engine_enabled_masks[j]) {
				if (gc->num_class_regs[i][j])
					drm_warn(&i915->drm, "GuC-Cap %s's %s class-"
						 "list enable mismatch was=%d now off\n",
						 OWNER2STR(i), ENGCLS2STR(j),
						 gc->num_class_regs[i][j]);
				if (gc->num_instance_regs[i][j])
					drm_warn(&i915->drm, "GuC-Cap %s's %s inst-"
						 "list enable mismatch was=%d now off!\n",
						 OWNER2STR(i), ENGCLS2STR(j),
						 gc->num_instance_regs[i][j]);
				gc->num_class_regs[i][j] = 0;
				gc->num_instance_regs[i][j] = 0;
				if (blob) {
					blob->capture_class[i][j] = null_ggtt;
					blob->capture_instance[i][j] = null_ggtt;
				}
			} else {
				if (!COUNT_REGS(guc, i, TYPE_CLASS, j, &tmpnumreg)) {
					if (blob && tmpnumreg > gc->num_class_regs[i][j]) {
						drm_warn(&i915->drm, "GuC-Cap %s's %s-%s-list "
							 "count overflow cap from %d to %d",
							 OWNER2STR(i), ENGCLS2STR(j),
							 TYPE2STR(TYPE_CLASS),
							 gc->num_class_regs[i][j], tmpnumreg);
						tmpnumreg = gc->num_class_regs[i][j];
					}
					size = FILL_REGS(guc, blob, i, TYPE_CLASS, j,
							 tmpnumreg, &ptr, &ggtt, null_ggtt);
					alloc_size += size;
					gc->num_class_regs[i][j] = tmpnumreg;
				} else {
					gc->num_class_regs[i][j] = 0;
					if (blob)
						blob->capture_class[i][j] = null_ggtt;
				}
				if (!COUNT_REGS(guc, i, TYPE_INSTANCE, j, &tmpnumreg)) {
					if (blob && tmpnumreg > gc->num_instance_regs[i][j]) {
						drm_warn(&i915->drm, "GuC-Cap %s's %s-%s-list "
							 "count overflow cap from %d to %d",
							 OWNER2STR(i), ENGCLS2STR(j),
							 TYPE2STR(TYPE_INSTANCE),
							 gc->num_instance_regs[i][j], tmpnumreg);
						tmpnumreg = gc->num_instance_regs[i][j];
					}
					size = FILL_REGS(guc, blob, i, TYPE_INSTANCE, j,
							 tmpnumreg, &ptr, &ggtt, null_ggtt);
					alloc_size += size;
					gc->num_instance_regs[i][j] = tmpnumreg;
				} else {
					gc->num_instance_regs[i][j] = 0;
					if (blob)
						blob->capture_instance[i][j] = null_ggtt;
				}
			}
		}
		if (!COUNT_REGS(guc, i, TYPE_GLOBAL, 0, &tmpnumreg)) {
			if (blob && tmpnumreg > gc->num_global_regs[i]) {
				drm_warn(&i915->drm, "GuC-Cap %s's %s-list count increased from %d to %d",
					 OWNER2STR(i), TYPE2STR(TYPE_GLOBAL),
					 gc->num_global_regs[i], tmpnumreg);
				tmpnumreg = gc->num_global_regs[i];
			}
			size = FILL_REGS(guc, blob, i, TYPE_GLOBAL, 0, tmpnumreg,
					 &ptr, &ggtt, null_ggtt);
			alloc_size += size;
			gc->num_global_regs[i] = tmpnumreg;
		} else {
			gc->num_global_regs[i] = 0;
			if (blob)
				blob->capture_global[i] = null_ggtt;
		}
	}

#undef COUNT_REGS
#undef FILL_REGS
#undef TYPE_GLOBAL
#undef TYPE_CLASS
#undef TYPE_INSTANCE
#undef OWNER2STR
#undef ENGCLS2STR
#undef TYPE2STR

	if (guc->ads_capture_size && guc->ads_capture_size != PAGE_ALIGN(alloc_size))
		drm_warn(&i915->drm, "GuC->ADS->Capture alloc size changed from %d to %d\n",
			 guc->ads_capture_size, PAGE_ALIGN(alloc_size));

	return PAGE_ALIGN(alloc_size);
}

void intel_guc_capture_destroy(struct intel_guc *guc)
{
	kfree(guc->capture.priv);
	guc->capture.priv = NULL;
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	guc->capture.priv = kzalloc(sizeof(*guc->capture.priv), GFP_KERNEL);
	if (!guc->capture.priv)
		return -ENOMEM;
	guc->capture.priv->reglists = guc_capture_get_device_reglist(guc);

	return 0;
}
