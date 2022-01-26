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
#include "i915_gpu_error.h"
#include "i915_irq.h"
#include "i915_memcpy.h"

/*
 * Define all device tables of GuC error capture register lists
 * NOTE: For engine-registers, GuC only needs the register offsets
 *       from the engine-mmio-base
 */
#define COMMON_BASE_GLOBAL() \
	{FORCEWAKE_MT,             0,      0, "FORCEWAKE"}

#define COMMON_GEN9BASE_GLOBAL() \
	{GEN8_FAULT_TLB_DATA0,     0,      0, "GEN8_FAULT_TLB_DATA0"}, \
	{GEN8_FAULT_TLB_DATA1,     0,      0, "GEN8_FAULT_TLB_DATA1"}, \
	{ERROR_GEN6,               0,      0, "ERROR_GEN6"}, \
	{DONE_REG,                 0,      0, "DONE_REG"}, \
	{HSW_GTT_CACHE_EN,         0,      0, "HSW_GTT_CACHE_EN"}

#define COMMON_GEN12BASE_GLOBAL() \
	{GEN12_FAULT_TLB_DATA0,    0,      0, "GEN12_FAULT_TLB_DATA0"}, \
	{GEN12_FAULT_TLB_DATA1,    0,      0, "GEN12_FAULT_TLB_DATA1"}, \
	{GEN12_AUX_ERR_DBG,        0,      0, "AUX_ERR_DBG"}, \
	{GEN12_GAM_DONE,           0,      0, "GAM_DONE"}, \
	{GEN12_RING_FAULT_REG,     0,      0, "FAULT_REG"}

#define COMMON_BASE_ENGINE_INSTANCE() \
	{RING_PSMI_CTL(0),         0,      0, "RC PSMI"}, \
	{RING_ESR(0),              0,      0, "ESR"}, \
	{RING_DMA_FADD(0),         0,      0, "RING_DMA_FADD_LDW"}, \
	{RING_DMA_FADD_UDW(0),     0,      0, "RING_DMA_FADD_UDW"}, \
	{RING_IPEIR(0),            0,      0, "IPEIR"}, \
	{RING_IPEHR(0),            0,      0, "IPEHR"}, \
	{RING_INSTPS(0),           0,      0, "INSTPS"}, \
	{RING_BBADDR(0),           0,      0, "RING_BBADDR_LOW32"}, \
	{RING_BBADDR_UDW(0),       0,      0, "RING_BBADDR_UP32"}, \
	{RING_BBSTATE(0),          0,      0, "BB_STATE"}, \
	{CCID(0),                  0,      0, "CCID"}, \
	{RING_ACTHD(0),            0,      0, "ACTHD_LDW"}, \
	{RING_ACTHD_UDW(0),        0,      0, "ACTHD_UDW"}, \
	{RING_INSTPM(0),           0,      0, "INSTPM"}, \
	{RING_INSTDONE(0),         0,      0, "INSTDONE"}, \
	{RING_NOPID(0),            0,      0, "RING_NOPID"}, \
	{RING_START(0),            0,      0, "START"}, \
	{RING_HEAD(0),             0,      0, "HEAD"}, \
	{RING_TAIL(0),             0,      0, "TAIL"}, \
	{RING_CTL(0),              0,      0, "CTL"}, \
	{RING_MI_MODE(0),          0,      0, "MODE"}, \
	{RING_CONTEXT_CONTROL(0),  0,      0, "RING_CONTEXT_CONTROL"}, \
	{RING_HWS_PGA(0),          0,      0, "HWS"}, \
	{RING_MODE_GEN7(0),        0,      0, "GFX_MODE"}, \
	{GEN8_RING_PDP_LDW(0, 0),  0,      0, "PDP0_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 0),  0,      0, "PDP0_UDW"}, \
	{GEN8_RING_PDP_LDW(0, 1),  0,      0, "PDP1_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 1),  0,      0, "PDP1_UDW"}, \
	{GEN8_RING_PDP_LDW(0, 2),  0,      0, "PDP2_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 2),  0,      0, "PDP2_UDW"}, \
	{GEN8_RING_PDP_LDW(0, 3),  0,      0, "PDP3_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 3),  0,      0, "PDP3_UDW"}

#define COMMON_BASE_HAS_EU() \
	{EIR,                      0,      0, "EIR"}

#define COMMON_BASE_RENDER() \
	{GEN7_SC_INSTDONE,         0,      0, "GEN7_SC_INSTDONE"}

#define COMMON_GEN12BASE_RENDER() \
	{GEN12_SC_INSTDONE_EXTRA,  0,      0, "GEN12_SC_INSTDONE_EXTRA"}, \
	{GEN12_SC_INSTDONE_EXTRA2, 0,      0, "GEN12_SC_INSTDONE_EXTRA2"}

#define COMMON_GEN12BASE_VEC() \
	{GEN12_SFC_DONE(0),        0,      0, "SFC_DONE[0]"}, \
	{GEN12_SFC_DONE(1),        0,      0, "SFC_DONE[1]"}, \
	{GEN12_SFC_DONE(2),        0,      0, "SFC_DONE[2]"}, \
	{GEN12_SFC_DONE(3),        0,      0, "SFC_DONE[3]"}

/* XE_LPD - Global */
static struct __guc_mmio_reg_descr xe_lpd_global_regs[] = {
	COMMON_BASE_GLOBAL(),
	COMMON_GEN9BASE_GLOBAL(),
	COMMON_GEN12BASE_GLOBAL(),
};

/* XE_LPD - Render / Compute Per-Class */
static struct __guc_mmio_reg_descr xe_lpd_rc_class_regs[] = {
	COMMON_BASE_HAS_EU(),
	COMMON_BASE_RENDER(),
	COMMON_GEN12BASE_RENDER(),
};

/* GEN9/XE_LPD - Render / Compute Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_rc_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE(),
};

/* GEN9/XE_LPD - Media Decode/Encode Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_vd_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE(),
};

/* XE_LPD - Video Enhancement Per-Class */
static struct __guc_mmio_reg_descr xe_lpd_vec_class_regs[] = {
	COMMON_GEN12BASE_VEC(),
};

/* GEN9/XE_LPD - Video Enhancement Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_vec_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE(),
};

/* GEN9/XE_LPD - Blitter Per-Engine-Instance */
static struct __guc_mmio_reg_descr xe_lpd_blt_inst_regs[] = {
	COMMON_BASE_ENGINE_INSTANCE(),
};

/* GEN9 - Global */
static struct __guc_mmio_reg_descr default_global_regs[] = {
	COMMON_BASE_GLOBAL(),
	COMMON_GEN9BASE_GLOBAL(),
};

static struct __guc_mmio_reg_descr default_rc_class_regs[] = {
	COMMON_BASE_HAS_EU(),
	COMMON_BASE_RENDER(),
};

/*
 * Empty lists:
 * GEN9/XE_LPD - Blitter-Class
 * GEN9/XE_LPD - Media Class
 * GEN9 - VEC Class
 */
static struct __guc_mmio_reg_descr empty_regs_list[] = {
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
		.num_ext = 0, \
		.ext = NULL, \
	}

/* List of lists */
static struct __guc_mmio_reg_descr_group default_lists[] = {
	MAKE_REGLIST(default_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(default_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_BLITTER_CLASS),
	MAKE_REGLIST(xe_lpd_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_BLITTER_CLASS),
	{}
};
static struct __guc_mmio_reg_descr_group xe_lpd_lists[] = {
	MAKE_REGLIST(xe_lpd_global_regs, PF, GLOBAL, 0),
	MAKE_REGLIST(xe_lpd_rc_class_regs, PF, ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_REGLIST(xe_lpd_rc_inst_regs, PF, ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vd_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_REGLIST(xe_lpd_vec_class_regs, PF, ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(xe_lpd_vec_inst_regs, PF, ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	MAKE_REGLIST(empty_regs_list, PF, ENGINE_CLASS, GUC_BLITTER_CLASS),
	MAKE_REGLIST(xe_lpd_blt_inst_regs, PF, ENGINE_INSTANCE, GUC_BLITTER_CLASS),
	{}
};

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

static void guc_capture_clear_ext_regs(struct __guc_mmio_reg_descr_group *lists)
{
	while (lists->list) {
		kfree(lists->ext);
		lists->ext = NULL;
		++lists;
	}
}

struct __ext_steer_reg {
	const char *name;
	i915_reg_t reg;
};

static struct __ext_steer_reg xelpd_extregs[] = {
	{"GEN7_SAMPLER_INSTDONE", GEN7_SAMPLER_INSTDONE},
	{"GEN7_ROW_INSTDONE", GEN7_ROW_INSTDONE}
};

static struct __ext_steer_reg xehpg_extregs[] = {
	{"XEHPG_INSTDONE_GEOM_SVG", XEHPG_INSTDONE_GEOM_SVG}
};

static void
guc_capture_alloc_steered_list_xe_lpd_hpg(struct intel_guc *guc,
					  struct __guc_mmio_reg_descr_group *lists,
					  u32 ipver)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct sseu_dev_info *sseu;
	int slice, subslice, i, iter, num_steer_regs, num_tot_regs = 0;
	struct __guc_mmio_reg_descr_group *list;
	struct __guc_mmio_reg_descr *extarray;

	/* In XE_LP / HPG we only have render-class steering registers during error-capture */
	list = guc_capture_get_one_list(lists, GUC_CAPTURE_LIST_INDEX_PF,
					GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS, GUC_RENDER_CLASS);
	if (!list)
		return;

	if (list->ext)
		return; /* already populated */

	num_steer_regs = ARRAY_SIZE(xelpd_extregs);
	if (ipver >= IP_VER(12, 55))
		num_steer_regs += ARRAY_SIZE(xehpg_extregs);

	sseu = &gt->info.sseu;
	if (ipver >= IP_VER(12, 50)) {
		for_each_instdone_gslice_dss_xehp(i915, sseu, iter, slice, subslice) {
			num_tot_regs += num_steer_regs;
		}
	} else {
		for_each_instdone_slice_subslice(i915, sseu, slice, subslice) {
			num_tot_regs += num_steer_regs;
		}
	}

	if (!num_tot_regs)
		return;

	list->ext = kcalloc(num_tot_regs, sizeof(struct __guc_mmio_reg_descr), GFP_KERNEL);
	if (!list->ext)
		return;

	extarray = list->ext;

#define POPULATE_NEXT_EXTREG(ext, list, idx, slicenum, subslicenum) \
	{ \
		(ext)->reg = list[idx].reg; \
		(ext)->flags = FIELD_PREP(GUC_REGSET_STEERING_GROUP, slicenum); \
		(ext)->flags |= FIELD_PREP(GUC_REGSET_STEERING_INSTANCE, subslicenum); \
		(ext)->regname = xelpd_extregs[i].name; \
		++(ext); \
	}
	if (ipver >= IP_VER(12, 50)) {
		for_each_instdone_gslice_dss_xehp(i915, sseu, iter, slice, subslice) {
			for (i = 0; i < ARRAY_SIZE(xelpd_extregs); i++)
				POPULATE_NEXT_EXTREG(extarray, xelpd_extregs, i, slice, subslice)
			for (i = 0; i < ARRAY_SIZE(xehpg_extregs) && ipver >= IP_VER(12, 55);
			     i++)
				POPULATE_NEXT_EXTREG(extarray, xehpg_extregs, i, slice, subslice)
		}
	} else {
		for_each_instdone_slice_subslice(i915, sseu, slice, subslice) {
			for (i = 0; i < num_steer_regs; i++)
				POPULATE_NEXT_EXTREG(extarray, xelpd_extregs, i, slice, subslice)
		}
	}
#undef POPULATE_NEXT_EXTREG

	list->num_ext = num_tot_regs;
}

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
		guc_capture_alloc_steered_list_xe_lpd_hpg(guc, xe_lpd_lists, IP_VER(12, 0));
		return xe_lpd_lists;
	} else if (IS_DG2(i915)) {
		guc_capture_alloc_steered_list_xe_lpd_hpg(guc, xe_lpd_lists, IP_VER(12, 55));
		return xe_lpd_lists;
	}

	/* if GuC submission is enabled on a non-POR platform, just use a common baseline */
	return default_lists;
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
	u32 j = 0, k = 0;
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
		if (match->ext) {
			for (j = match->num_regs, k = 0; j < num_entries &&
			     j < (match->num_regs + match->num_ext); ++j, ++k) {
				ptr[j].offset = match->ext[k].reg.reg;
				ptr[j].value = 0xDEADF00D;
				ptr[j].flags = match->ext[k].flags;
				ptr[j].mask = match->ext[k].mask;
			}
		}
		if (j < num_entries)
			drm_dbg(&i915->drm, "GuC-capture: Init reglist short %d out %d.\n",
				(int)j, (int)num_entries);
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

	*num_entries = match->num_regs + match->num_ext;
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

#define GUC_CAPTURE_OVERBUFFER_MULTIPLIER 3
int intel_guc_capture_output_min_size_est(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int worst_min_size = 0, num_regs = 0;
	u16 tmp = 0;

	if (!guc->capture.priv)
		return -ENODEV;

	/*
	 * If every single engine-instance suffered a failure in quick succession but
	 * were all unrelated, then a burst of multiple error-capture events would dump
	 * registers for every one engine instance, one at a time. In this case, GuC
	 * would even dump the global-registers repeatedly.
	 *
	 * For each engine instance, there would be 1 x guc_state_capture_group_t output
	 * followed by 3 x guc_state_capture_t lists. The latter is how the register
	 * dumps are split across different register types (where the '3' are global vs class
	 * vs instance). Finally, let's multiply the whole thing by 3x (just so we are
	 * not limited to just 1 round of data in a worst case full register dump log)
	 *
	 * NOTE: intel_guc_log that allocates the log buffer would round this size up to
	 * a power of two.
	 */

	for_each_engine(engine, gt, id) {
		worst_min_size += sizeof(struct guc_state_capture_group_header_t) +
				  (3 * sizeof(struct guc_state_capture_header_t));

		if (!guc_capture_list_count(guc, 0, GUC_CAPTURE_LIST_TYPE_GLOBAL, 0, &tmp))
			num_regs += tmp;

		if (!guc_capture_list_count(guc, 0, GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
					    engine->class, &tmp)) {
			num_regs += tmp;
		}
		if (!guc_capture_list_count(guc, 0, GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
					    engine->class, &tmp)) {
			num_regs += tmp;
		}
	}

	worst_min_size += (num_regs * sizeof(struct guc_mmio_reg));

	return (worst_min_size * GUC_CAPTURE_OVERBUFFER_MULTIPLIER);
}

/*
 * KMD Init time flows:
 * --------------------
 *     --> alloc A: GuC input capture regs lists (registered via ADS)
 *                  List acquired via intel_guc_capture_list_count + intel_guc_capture_list_init
 *                  Size = global-reg-list + (class-reg-list) + (num-instances x instance-reg-list)
 *                  Device tables carry: 1x global, 1x per-class, 1x per-instance)
 *                  Caller needs to call per-class and per-instance multiplie times
 *
 *     --> alloc B: GuC output capture buf (registered via guc_init_params(log_param))
 *                  Size = #define CAPTURE_BUFFER_SIZE (warns if on too-small)
 *                  Note2: 'x 3' to hold multiple capture groups
 *
 *
 * GUC Runtime notify capture:
 * --------------------------
 *     --> G2H STATE_CAPTURE_NOTIFICATION
 *                   L--> intel_guc_capture_store_snapshot
 *                           L--> Loop through B (head..tail) and for each engine instance
 *                                register we find:
 *      --> alloc C: A capture-output-node structure that includes misc capture info along
 *                   with 3 register list dumps (global, engine-class and engine-
 *                   instance). This node id added to a linked list stored in
 *                   guc->capture->priv for matchup and printout when triggered by
 *                   i915_gpu_coredump and err_print_gt (via error capture sysfs) later.
 *
 * GUC --> notify context reset:
 * -----------------------------
 *     --> G2H CONTEXT RESET
 *                   L--> guc_handle_context_reset --> i915_capture_error_state
 *                          L--> i915_gpu_coredump(..IS_GUC_CAPTURE) --> gt_record_engines
 *                               --> capture_engine(..IS_GUC_CAPTURE)
 *                                  L--> detach C from internal linked list and add into
 *                                       intel_engine_coredump struct (if the context and
 *                                       engine of the event notification matches a node
 *                                       in the link list)
 */

static int guc_capture_buf_cnt(struct __guc_capture_bufstate *buf)
{
	if (buf->rd == buf->wr)
		return 0;
	if (buf->wr > buf->rd)
		return (buf->wr - buf->rd);
	return (buf->size - buf->rd) + buf->wr;
}

static int guc_capture_buf_cnt_to_end(struct __guc_capture_bufstate *buf)
{
	if (buf->rd > buf->wr)
		return (buf->size - buf->rd);
	return (buf->wr - buf->rd);
}

static int
guc_capture_log_remove_dw(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			  u32 *dw)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int tries = 2;
	int avail = 0;
	u32 *src_data;

	if (!guc_capture_buf_cnt(buf))
		return 0;

	while (tries--) {
		avail = guc_capture_buf_cnt_to_end(buf);
		if (avail >= sizeof(u32)) {
			src_data = (u32 *)(buf->data + buf->rd);
			*dw = *src_data;
			buf->rd += 4;
			return 4;
		}
		if (avail)
			drm_warn(&i915->drm, "GuC-Cap-Logs not dword aligned, skipping.\n");
		buf->rd = 0;
	}

	return 0;
}

static bool
guc_capture_data_extracted(struct __guc_capture_bufstate *b,
			   int s, void *p)
{
	if (guc_capture_buf_cnt_to_end(b) >= s) {
		memcpy(p, (b->data + b->rd), s);
		b->rd += s;
		return true;
	}
	return false;
}

static int
guc_capture_log_get_group_hdr(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			      struct guc_state_capture_group_header_t *ghdr)
{
	int read = 0;
	int fullsize = sizeof(struct guc_state_capture_group_header_t);

	if (fullsize > guc_capture_buf_cnt(buf))
		return -1;

	if (guc_capture_data_extracted(buf, fullsize, (void *)ghdr))
		return 0;

	read += guc_capture_log_remove_dw(guc, buf, &ghdr->owner);
	read += guc_capture_log_remove_dw(guc, buf, &ghdr->info);
	if (read != fullsize)
		return -1;

	return 0;
}

static int
guc_capture_log_get_data_hdr(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			     struct guc_state_capture_header_t *hdr)
{
	int read = 0;
	int fullsize = sizeof(struct guc_state_capture_header_t);

	if (fullsize > guc_capture_buf_cnt(buf))
		return -1;

	if (guc_capture_data_extracted(buf, fullsize, (void *)hdr))
		return 0;

	read += guc_capture_log_remove_dw(guc, buf, &hdr->owner);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->info);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->lrca);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->guc_id);
	read += guc_capture_log_remove_dw(guc, buf, &hdr->num_mmios);
	if (read != fullsize)
		return -1;

	return 0;
}

static int
guc_capture_log_get_register(struct intel_guc *guc, struct __guc_capture_bufstate *buf,
			     struct guc_mmio_reg *reg)
{
	int read = 0;
	int fullsize = sizeof(struct guc_mmio_reg);

	if (fullsize > guc_capture_buf_cnt(buf))
		return -1;

	if (guc_capture_data_extracted(buf, fullsize, (void *)reg))
		return 0;

	read += guc_capture_log_remove_dw(guc, buf, &reg->offset);
	read += guc_capture_log_remove_dw(guc, buf, &reg->value);
	read += guc_capture_log_remove_dw(guc, buf, &reg->flags);
	read += guc_capture_log_remove_dw(guc, buf, &reg->mask);
	if (read != fullsize)
		return -1;

	return 0;
}

static void
guc_capture_del_all_nodes(struct intel_guc *guc)
{
	int i;

	if (!list_empty(&guc->capture.priv->outlist)) {
		struct __guc_capture_parsed_output *n, *ntmp;

		list_for_each_entry_safe(n, ntmp, &guc->capture.priv->outlist, link) {
			for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
				if (n->reginfo[i].regs)
					kfree(n->reginfo[i].regs);
			}
			list_del(&n->link);
			kfree(n);
		}
	}
	guc->capture.priv->listcount = 0;
}

static void
guc_capture_del_node(struct intel_guc *guc, struct __guc_capture_parsed_output *node)
{
	int i;
	struct __guc_capture_parsed_output *found = NULL;

	if (!list_empty(&guc->capture.priv->outlist)) {
		struct __guc_capture_parsed_output *n, *ntmp;

		if (node) {
			found = node;
		} else {
			/* traverse down and get the oldest entry */
			list_for_each_entry_safe(n, ntmp, &guc->capture.priv->outlist, link)
				found = n;
		}
		if (found) {
			for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
				if (found->reginfo[i].regs)
					kfree(found->reginfo[i].regs);
			}
			list_del(&found->link);
			kfree(found);
			--guc->capture.priv->listcount;
		}
	}
}

static void
guc_capture_add_node_to_list(struct intel_guc *guc, struct __guc_capture_parsed_output *node)
{
	GEM_BUG_ON(guc->capture.priv->listcount > MAX_NODE_LINKLIST_THRESHOLD);

	if (guc->capture.priv->listcount == MAX_NODE_LINKLIST_THRESHOLD) {
		/* discard oldest node */
		guc_capture_del_node(guc, NULL);
	}

	++guc->capture.priv->listcount;
	list_add_tail(&node->link, &guc->capture.priv->outlist);
}

static struct __guc_capture_parsed_output *
guc_capture_create_node(struct intel_guc *guc, struct __guc_capture_parsed_output *ori,
			u32 keep_reglist_mask)
{
	struct __guc_capture_parsed_output *new;
	int i;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;
	INIT_LIST_HEAD(&new->link);
	if (!ori)
		return new;
	memcpy(new, ori, sizeof(*new));

	/* reallocate individual reg-list pointers */
	for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		new->reginfo[i].regs = NULL;
		new->reginfo[i].num = 0;
	}
	for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		if (keep_reglist_mask & BIT(i)) {
			new->reginfo[i].regs = kcalloc(ori->reginfo[i].num,
						       sizeof(struct guc_mmio_reg), GFP_KERNEL);
			if (!new->reginfo[i].regs)
				goto bail_clone;
			memcpy(new->reginfo[i].regs, ori->reginfo[i].regs, ori->reginfo[i].num *
			       sizeof(struct guc_mmio_reg));
			new->reginfo[i].num = ori->reginfo[i].num;
		}
	}

	return new;

bail_clone:
	for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		if (new->reginfo[i].regs)
			kfree(new->reginfo[i].regs);
	}
	kfree(new);
	return NULL;
}

static int
guc_capture_extract_reglists(struct intel_guc *guc, struct __guc_capture_bufstate *buf)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	struct guc_state_capture_group_header_t ghdr = {0};
	struct guc_state_capture_header_t hdr = {0};
	struct __guc_capture_parsed_output *node = NULL;
	struct guc_mmio_reg *regs = NULL;
	int i, numlists, numreg, ret = 0;
	bool is_partial = false;
	enum guc_capture_type datatype;

	i = guc_capture_buf_cnt(buf);
	if (!i)
		return -ENODATA;
	if (i % sizeof(u32)) {
		drm_warn(&i915->drm, "GuC Capture new entries unaligned\n");
		ret = -EIO;
		goto bailout;
	}

	/* first get the capture group header */
	if (guc_capture_log_get_group_hdr(guc, buf, &ghdr)) {
		ret = -EIO;
		goto bailout;
	}
	/*
	 * we would typically expect a layout as below where n would be expected to be
	 * anywhere between 3 to n where n > 3 if we are seeing multiple dependent engine
	 * instances being reset together.
	 * ____________________________________________
	 * | Capture Group                            |
	 * | ________________________________________ |
	 * | | Capture Group Header:                | |
	 * | |  - num_captures = 5                  | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture1:                            | |
	 * | |  Hdr: GLOBAL, numregs=a              | |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... rega           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture2:                            | |
	 * | |  Hdr: CLASS=RENDER/COMPUTE, numregs=b| |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... regb           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture3:                            | |
	 * | |  Hdr: INSTANCE=RCS, numregs=c        | |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... regc           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture4:                            | |
	 * | |  Hdr: CLASS=RENDER/COMPUTE, numregs=d| |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... regd           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * | ________________________________________ |
	 * | | Capture5:                            | |
	 * | |  Hdr: INSTANCE=CCS0, numregs=e       | |
	 * | | ____________________________________ | |
	 * | | | Reglist                          | | |
	 * | | | - reg1, reg2, ... rege           | | |
	 * | | |__________________________________| | |
	 * | |______________________________________| |
	 * |__________________________________________|
	 */
	is_partial = FIELD_GET(CAP_GRP_HDR_CAPTURE_TYPE, ghdr.info);
	if (is_partial)
		drm_warn(&i915->drm, "GuC Capture group is partial\n");
	numlists = FIELD_GET(CAP_GRP_HDR_NUM_CAPTURES, ghdr.info);
	while (numlists--) {

		numreg = 0;
		regs = NULL;
		if (guc_capture_log_get_data_hdr(guc, buf, &hdr)) {
			ret = -EIO;
			break;
		}

		datatype = FIELD_GET(CAP_HDR_CAPTURE_TYPE, hdr.info);
		if (node) {
			/* Based on the current capture type and what we have so far,
			 * decide if we should add the current node into the internal
			 * linked list for match-up when i915_gpu_coredump calls later
			 * (and alloc a blank node for the next set of reglists)
			 * or continue with the same node or clone the current node
			 * but only retain the global or class registers (such as the
			 * case of dependent engine resets).
			 */
			if (datatype == GUC_CAPTURE_LIST_TYPE_GLOBAL) {
				guc_capture_add_node_to_list(guc, node);
				node = NULL;
			} else if (datatype == GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS &&
				   node->reginfo[GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS].regs) {
				/* Add to list, clone node and duplicate global list */
				guc_capture_add_node_to_list(guc, node);
				node = guc_capture_create_node(guc, node,
							       GCAP_PARSED_REGLIST_INDEX_GLOBAL);
			} else if (datatype == GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE &&
				   node->reginfo[GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE].regs) {
				/* Add to list, clone node and duplicate global + class lists */
				guc_capture_add_node_to_list(guc, node);
				node = guc_capture_create_node(guc, node,
							       (GCAP_PARSED_REGLIST_INDEX_GLOBAL |
							       GCAP_PARSED_REGLIST_INDEX_ENGCLASS));
			}
		}

		if (!node) {
			node = guc_capture_create_node(guc, NULL, 0);
			if (!node) {
				ret = -ENOMEM;
				break;
			}
			if (datatype != GUC_CAPTURE_LIST_TYPE_GLOBAL)
				drm_dbg(&i915->drm, "GuC Capture missing global dump: %08x!\n",
					datatype);
		}
		node->is_partial = is_partial;
		switch (datatype) {
		case GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE:
			node->eng_inst = FIELD_GET(CAP_HDR_ENGINE_INSTANCE, hdr.info);
			node->lrca = hdr.lrca;
			node->guc_id = hdr.guc_id;
			node->eng_class = FIELD_GET(CAP_HDR_ENGINE_CLASS, hdr.info);
			break;
		case GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS:
			node->eng_class = FIELD_GET(CAP_HDR_ENGINE_CLASS, hdr.info);
			break;
		default:
			break;
		}
		regs = NULL;
		numreg = FIELD_GET(CAP_HDR_NUM_MMIOS, hdr.num_mmios);
		if (numreg) {
			regs = kcalloc(numreg, sizeof(*regs), GFP_KERNEL);
			if (!regs) {
				ret = -ENOMEM;
				break;
			}
		}
		node->reginfo[datatype].num = numreg;
		node->reginfo[datatype].regs = regs;
		node->reginfo[datatype].vfid = FIELD_GET(CAP_HDR_CAPTURE_VFID, hdr.info);
		i = 0;
		while (numreg--) {
			if (guc_capture_log_get_register(guc, buf, &regs[i++])) {
				ret = -EIO;
				break;
			}
		}
	}

bailout:
	if (node) {
		/* If we have data, add to linked list for match-up when i915_gpu_coredump calls */
		for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
			if (node->reginfo[i].regs) {
				guc_capture_add_node_to_list(guc, node);
				node = NULL;
				break;
			}
		}
		if (node)
			kfree(node);
	}
	return ret;
}

static void __guc_capture_store_snapshot_work(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	unsigned int buffer_size, read_offset, write_offset, full_count;
	struct guc_log_buffer_state *log_buf_state;
	struct guc_log_buffer_state log_buf_state_local;
	void *src_data = NULL;
	bool new_overflow;
	struct __guc_capture_bufstate buf;
	int ret;

	/* Lock to get the pointer to GuC capture-log-buffer-state */
	mutex_lock(&guc->log_state[GUC_CAPTURE_LOG_BUFFER].lock);
	log_buf_state = guc->log.buf_addr +
			(sizeof(struct guc_log_buffer_state) * GUC_CAPTURE_LOG_BUFFER);
	src_data = guc->log.buf_addr + intel_guc_get_log_buffer_offset(GUC_CAPTURE_LOG_BUFFER);

	/*
	 * Make a copy of the state structure, inside GuC log buffer
	 * (which is uncached mapped), on the stack to avoid reading
	 * from it multiple times.
	 */
	memcpy(&log_buf_state_local, log_buf_state, sizeof(struct guc_log_buffer_state));
	buffer_size = intel_guc_get_log_buffer_size(GUC_CAPTURE_LOG_BUFFER);
	read_offset = log_buf_state_local.read_ptr;
	write_offset = log_buf_state_local.sampled_write_ptr;
	full_count = log_buf_state_local.buffer_full_cnt;

	/* Bookkeeping stuff */
	guc->log_state[GUC_CAPTURE_LOG_BUFFER].flush += log_buf_state_local.flush_to_file;
	new_overflow = intel_guc_check_log_buf_overflow(guc,
							&guc->log_state[GUC_CAPTURE_LOG_BUFFER],
							full_count);

	/* Now copy the actual logs. */
	if (unlikely(new_overflow)) {
		/* copy the whole buffer in case of overflow */
		read_offset = 0;
		write_offset = buffer_size;
	} else if (unlikely((read_offset > buffer_size) ||
			(write_offset > buffer_size))) {
		drm_err(&i915->drm, "invalid GuC log capture buffer state!\n");
		/* copy whole buffer as offsets are unreliable */
		read_offset = 0;
		write_offset = buffer_size;
	}

	buf.size = buffer_size;
	buf.rd = read_offset;
	buf.wr = write_offset;
	buf.data = src_data;
	/*  */
	do {
		ret = guc_capture_extract_reglists(guc, &buf);
	} while (ret >= 0);

	/* Update the state of shared log buffer */
	log_buf_state->read_ptr = write_offset;
	log_buf_state->flush_to_file = 0;

	mutex_unlock(&guc->log_state[GUC_CAPTURE_LOG_BUFFER].lock);
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

int intel_guc_capture_print_engine_node(struct drm_i915_error_state_buf *ebuf,
					const struct intel_engine_coredump *ee)
{
	return 0;
}

#endif //CONFIG_DRM_I915_DEBUG_GUC

void intel_guc_capture_free_node(struct intel_engine_coredump *ee)
{
	int i;

	if (!ee)
		return;
	if (!ee->guc_capture_node)
		return;
	for (i = GUC_CAPTURE_LIST_TYPE_GLOBAL; i < GUC_CAPTURE_LIST_TYPE_MAX; ++i) {
		if (ee->guc_capture_node->reginfo[i].regs)
			kfree(ee->guc_capture_node->reginfo[i].regs);
	}
	kfree(ee->guc_capture_node);
	ee->guc_capture_node = NULL;
}

void intel_guc_capture_get_matching_node(struct intel_gt *gt,
					 struct intel_engine_coredump *ee,
					 struct intel_context *ce)
{
	struct intel_guc *guc;
	struct drm_i915_private *i915;

	if (!gt || !ee || !ce)
		return;

	i915 = gt->i915;
	guc = &gt->uc.guc;
	if (!guc->capture.priv)
		return;

	GEM_BUG_ON(ee->guc_capture_node);
	/*
	 * Look for a matching GuC reported error capture node from
	 * the internal output link-list based on lrca, guc-id and engine
	 * identification.
	 */
	if (!list_empty(&guc->capture.priv->outlist)) {
		struct __guc_capture_parsed_output *n, *ntmp;

		list_for_each_entry_safe(n, ntmp, &guc->capture.priv->outlist, link) {
			if (n->eng_inst == GUC_ID_TO_ENGINE_INSTANCE(ee->engine->guc_id) &&
			    n->eng_class == GUC_ID_TO_ENGINE_CLASS(ee->engine->guc_id) &&
			    n->guc_id == ce->guc_id.id &&
			    (n->lrca & CTX_GTT_ADDRESS_MASK) ==
			    (ce->lrc.lrca & CTX_GTT_ADDRESS_MASK)) {
				list_del(&n->link);
				--guc->capture.priv->listcount;
				ee->guc_capture_node = n;
				ee->capture = &guc->capture;
				return;
			}
		}
	}
	drm_warn(&i915->drm, "GuC capture can't match ee to node\n");
}

void intel_guc_capture_store_snapshot(struct intel_guc *guc)
{
	if (guc->capture.priv)
		__guc_capture_store_snapshot_work(guc);
}

void intel_guc_capture_destroy(struct intel_guc *guc)
{
	if (!guc->capture.priv)
		return;

	guc_capture_del_all_nodes(guc);
	guc_capture_clear_ext_regs(guc->capture.priv->reglists);
	kfree(guc->capture.priv);
	guc->capture.priv = NULL;
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	guc->capture.priv = kzalloc(sizeof(*guc->capture.priv), GFP_KERNEL);
	if (!guc->capture.priv)
		return -ENOMEM;
	INIT_LIST_HEAD(&guc->capture.priv->outlist);
	guc->capture.priv->reglists = guc_capture_get_device_reglist(guc);
	return 0;
}
