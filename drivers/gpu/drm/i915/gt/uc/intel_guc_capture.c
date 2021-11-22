// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i915_drv.h"
#include "i915_memcpy.h"
#include "gt/intel_gt.h"
#include "gt/intel_lrc_reg.h"

#include "intel_guc_fwif.h"
#include "intel_guc_capture.h"

/*
 * Define all device tables of GuC error capture register lists
 * NOTE: For engine-registers, GuC only needs the register offsets
 *       from the engine-mmio-base
 */
#define COMMON_GEN12BASE_GLOBAL() \
	{GEN12_FAULT_TLB_DATA0,    0,      0, "GEN12_FAULT_TLB_DATA0"}, \
	{GEN12_FAULT_TLB_DATA1,    0,      0, "GEN12_FAULT_TLB_DATA1"}, \
	{FORCEWAKE_MT,             0,      0, "FORCEWAKE_MT"}, \
	{DERRMR,                   0,      0, "DERRMR"}, \
	{GEN12_AUX_ERR_DBG,        0,      0, "GEN12_AUX_ERR_DBG"}, \
	{GEN12_GAM_DONE,           0,      0, "GEN12_GAM_DONE"}, \
	{GEN11_GUC_SG_INTR_ENABLE, 0,      0, "GEN11_GUC_SG_INTR_ENABLE"}, \
	{GEN11_CRYPTO_RSVD_INTR_ENABLE, 0, 0, "GEN11_CRYPTO_RSVD_INTR_ENABLE"}, \
	{GEN11_GUNIT_CSME_INTR_ENABLE, 0,  0, "GEN11_GUNIT_CSME_INTR_ENABLE"}, \
	{GEN12_RING_FAULT_REG,     0,      0, "GEN12_RING_FAULT_REG"}

#define COMMON_GEN12BASE_ENGINE_INSTANCE() \
	{RING_PSMI_CTL(0),         0,      0, "RING_PSMI_CTL"}, \
	{RING_ESR(0),              0,      0, "RING_ESR"}, \
	{RING_ESR(0),              0,      0, "RING_ESR"}, \
	{RING_DMA_FADD(0),         0,      0, "RING_DMA_FADD_LOW32"}, \
	{RING_DMA_FADD_UDW(0),     0,      0, "RING_DMA_FADD_UP32"}, \
	{RING_IPEIR(0),            0,      0, "RING_IPEIR"}, \
	{RING_IPEHR(0),            0,      0, "RING_IPEHR"}, \
	{RING_INSTPS(0),           0,      0, "RING_INSTPS"}, \
	{RING_BBADDR(0),           0,      0, "RING_BBADDR_LOW32"}, \
	{RING_BBADDR_UDW(0),       0,      0, "RING_BBADDR_UP32"}, \
	{RING_BBSTATE(0),          0,      0, "RING_BBSTATE"}, \
	{CCID(0),                  0,      0, "CCID"}, \
	{RING_ACTHD(0),            0,      0, "RING_ACTHD_LOW32"}, \
	{RING_ACTHD_UDW(0),        0,      0, "RING_ACTHD_UP32"}, \
	{RING_INSTPM(0),           0,      0, "RING_INSTPM"}, \
	{RING_NOPID(0),            0,      0, "RING_NOPID"}, \
	{RING_START(0),            0,      0, "RING_START"}, \
	{RING_HEAD(0),             0,      0, "RING_HEAD"}, \
	{RING_TAIL(0),             0,      0, "RING_TAIL"}, \
	{RING_CTL(0),              0,      0, "RING_CTL"}, \
	{RING_MI_MODE(0),          0,      0, "RING_MI_MODE"}, \
	{RING_CONTEXT_CONTROL(0),  0,      0, "RING_CONTEXT_CONTROL"}, \
	{RING_INSTDONE(0),         0,      0, "RING_INSTDONE"}, \
	{RING_HWS_PGA(0),          0,      0, "RING_HWS_PGA"}, \
	{RING_MODE_GEN7(0),        0,      0, "RING_MODE_GEN7"}, \
	{GEN8_RING_PDP_LDW(0, 0),  0,      0, "GEN8_RING_PDP0_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 0),  0,      0, "GEN8_RING_PDP0_UDW"}, \
	{GEN8_RING_PDP_LDW(0, 1),  0,      0, "GEN8_RING_PDP1_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 1),  0,      0, "GEN8_RING_PDP1_UDW"}, \
	{GEN8_RING_PDP_LDW(0, 2),  0,      0, "GEN8_RING_PDP2_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 2),  0,      0, "GEN8_RING_PDP2_UDW"}, \
	{GEN8_RING_PDP_LDW(0, 3),  0,      0, "GEN8_RING_PDP3_LDW"}, \
	{GEN8_RING_PDP_UDW(0, 3),  0,      0, "GEN8_RING_PDP3_UDW"}

#define COMMON_GEN12BASE_HAS_EU() \
	{EIR,                      0,      0, "EIR"}

#define COMMON_GEN12BASE_RENDER() \
	{GEN7_SC_INSTDONE,         0,      0, "GEN7_SC_INSTDONE"}, \
	{GEN12_SC_INSTDONE_EXTRA,  0,      0, "GEN12_SC_INSTDONE_EXTRA"}, \
	{GEN12_SC_INSTDONE_EXTRA2, 0,      0, "GEN12_SC_INSTDONE_EXTRA2"}

#define COMMON_GEN12BASE_VEC() \
	{GEN11_VCS_VECS_INTR_ENABLE, 0,    0, "GEN11_VCS_VECS_INTR_ENABLE"}, \
	{GEN12_SFC_DONE(0),        0,      0, "GEN12_SFC_DONE0"}, \
	{GEN12_SFC_DONE(1),        0,      0, "GEN12_SFC_DONE1"}, \
	{GEN12_SFC_DONE(2),        0,      0, "GEN12_SFC_DONE2"}, \
	{GEN12_SFC_DONE(3),        0,      0, "GEN12_SFC_DONE3"}

/********************************* Gen12 LP  *********************************/
/************** GLOBAL *************/
struct __guc_mmio_reg_descr gen12lp_global_regs[] = {
	COMMON_GEN12BASE_GLOBAL(),
	{GEN7_ROW_INSTDONE,        0,      0, "GEN7_ROW_INSTDONE"},
};

/********** RENDER/COMPUTE *********/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_rc_class_regs[] = {
	COMMON_GEN12BASE_HAS_EU(),
	COMMON_GEN12BASE_RENDER(),
	{GEN11_RENDER_COPY_INTR_ENABLE, 0, 0, "GEN11_RENDER_COPY_INTR_ENABLE"},
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_rc_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE(),
};

/************* MEDIA-VD ************/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_vd_class_regs[] = {
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_vd_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE(),
};

/************* MEDIA-VEC ***********/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_vec_class_regs[] = {
	COMMON_GEN12BASE_VEC(),
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_vec_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE(),
};

/************* BLITTER ***********/
/* Per-Class */
struct __guc_mmio_reg_descr gen12lp_blt_class_regs[] = {
};

/* Per-Engine-Instance */
struct __guc_mmio_reg_descr gen12lp_blt_inst_regs[] = {
	COMMON_GEN12BASE_ENGINE_INSTANCE(),
};

#define TO_GCAP_DEF(x) (GUC_CAPTURE_LIST_##x)
#define MAKE_GCAP_REGLIST_DESCR(regslist, regsowner, regstype, class) \
	{ \
		.list = (regslist), \
		.num_regs = (sizeof(regslist) / sizeof(struct __guc_mmio_reg_descr)), \
		.owner = TO_GCAP_DEF(regsowner), \
		.type = TO_GCAP_DEF(regstype), \
		.engine = class, \
		.num_ext = 0, \
		.ext = NULL, \
	}


/********** List of lists **********/
struct __guc_mmio_reg_descr_group xe_lpd_lists[] = {
	MAKE_GCAP_REGLIST_DESCR(gen12lp_global_regs, INDEX_PF, TYPE_GLOBAL, 0),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_rc_class_regs, INDEX_PF, TYPE_ENGINE_CLASS, GUC_RENDER_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_rc_inst_regs, INDEX_PF, TYPE_ENGINE_INSTANCE, GUC_RENDER_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_vd_class_regs, INDEX_PF, TYPE_ENGINE_CLASS, GUC_VIDEO_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_vd_inst_regs, INDEX_PF, TYPE_ENGINE_INSTANCE, GUC_VIDEO_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_vec_class_regs, INDEX_PF, TYPE_ENGINE_CLASS, GUC_VIDEOENHANCE_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_vec_inst_regs, INDEX_PF, TYPE_ENGINE_INSTANCE, GUC_VIDEOENHANCE_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_blt_class_regs, INDEX_PF, TYPE_ENGINE_CLASS, GUC_BLITTER_CLASS),
	MAKE_GCAP_REGLIST_DESCR(gen12lp_blt_inst_regs, INDEX_PF, TYPE_ENGINE_INSTANCE, GUC_BLITTER_CLASS),
	{NULL, 0, 0, 0, 0}
};

/************* Populate additional registers / device tables *************/

static inline struct __guc_mmio_reg_descr **
guc_capture_get_ext_list_ptr(struct __guc_mmio_reg_descr_group * lists, u32 owner, u32 type, u32 class)
{
	while(lists->list){
		if (lists->owner == owner && lists->type == type && lists->engine == class)
			break;
		++lists;
	}
	if (!lists->list)
		return NULL;

	return &(lists->ext);
}

void guc_capture_clear_ext_regs(struct __guc_mmio_reg_descr_group * lists)
{
	while(lists->list){
		if (lists->ext) {
			kfree(lists->ext);
			lists->ext = NULL;
		}
		++lists;
	}
	return;
}

static void
xelpd_alloc_steered_ext_list(struct drm_i915_private *i915,
			     struct __guc_mmio_reg_descr_group * lists)
{
	struct intel_gt *gt = &i915->gt;
	struct sseu_dev_info *sseu;
	int slice, subslice, i, num_tot_regs = 0;
	struct __guc_mmio_reg_descr **ext;
	static char * const strings[] = {
		[0] = "GEN7_SAMPLER_INSTDONE",
		[1] = "GEN7_ROW_INSTDONE",
	};

	/* In XE_LP we only care about render-class steering registers during error-capture */
	ext = guc_capture_get_ext_list_ptr(lists, GUC_CAPTURE_LIST_INDEX_PF,
					   GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS, GUC_RENDER_CLASS);
	if (!ext)
		return;
	if (*ext)
		return; /* already populated */

	sseu = &gt->info.sseu;
	for_each_instdone_slice_subslice(i915, sseu, slice, subslice) {
		num_tot_regs += 2; /* two registers of interest for now */
	}
	if (!num_tot_regs)
		return;

	*ext = kzalloc(2 * num_tot_regs * sizeof(struct __guc_mmio_reg_descr), GFP_KERNEL);
	if (!*ext) {
		drm_warn(&i915->drm, "GuC-capture: Fail to allocate for extended registers\n");
		return;
	}

	for_each_instdone_slice_subslice(i915, sseu, slice, subslice) {
		for (i = 0; i < 2; i++) {
			if (i == 0)
				(*ext)->reg = GEN7_SAMPLER_INSTDONE;
			else
				(*ext)->reg = GEN7_ROW_INSTDONE;
			(*ext)->flags = FIELD_PREP(GUC_REGSET_STEERING_GROUP, slice);
			(*ext)->flags |= FIELD_PREP(GUC_REGSET_STEERING_INSTANCE, subslice);
			(*ext)->regname = strings[i];
			(*ext)++;
		}
	}
}

static struct __guc_mmio_reg_descr_group *
guc_capture_get_device_reglist(struct drm_i915_private *dev_priv)
{
	if (IS_TIGERLAKE(dev_priv) || IS_ROCKETLAKE(dev_priv) ||
	    IS_ALDERLAKE_S(dev_priv) || IS_ALDERLAKE_P(dev_priv)) {
		/*
		* For certain engine classes, there are slice and subslice
		* level registers requiring steering. We allocate and populate
		* these at init time based on hw config add it as an extension
		* list at the end of the pre-populated render list.
		*/
		xelpd_alloc_steered_ext_list(dev_priv, xe_lpd_lists);
		return xe_lpd_lists;
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

int intel_guc_capture_output_min_size_est(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int worst_min_size = 0, num_regs = 0;
	u16 tmp = 0;

	/*
	 * If every single engine-instance suffered a failure in quick succession but
	 * were all unrelated, then a burst of multiple error-capture events would dump
	 * registers for every one engine instance, one at a time. In this case, GuC
	 * would even dump the global-registers repeatedly.
	 *
	 * For each engine instance, there would be 1 x intel_guc_capture_out_group output
	 * followed by 3 x intel_guc_capture_out_data lists. The latter is how the register
	 * dumps are split across different register types (where the '3' are global vs class
	 * vs instance). Finally, let's multiply the whole thing by 3x (just so we are
	 * not limited to just 1 rounds of data in a  worst case full register dump log)
	 *
	 * NOTE: intel_guc_log that allocates the log buffer would round this size up to
	 * a power of two.
	 */

	for_each_engine(engine, gt, id) {
		worst_min_size += sizeof(struct intel_guc_capture_out_group_header) +
				  (3 * sizeof(struct intel_guc_capture_out_data_header));

		if (!intel_guc_capture_list_count(guc, 0, GUC_CAPTURE_LIST_TYPE_GLOBAL, 0, &tmp))
			num_regs += tmp;

		if (!intel_guc_capture_list_count(guc, 0, GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
						  engine->class, &tmp)) {
			num_regs += tmp;
		}
		if (!intel_guc_capture_list_count(guc, 0, GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
						  engine->class, &tmp)) {
			num_regs += tmp;
		}
	}

	worst_min_size += (num_regs * sizeof(struct guc_mmio_reg));

	return (worst_min_size * 3);
}

void intel_guc_capture_destroy(struct intel_guc *guc)
{
	guc_capture_clear_ext_regs(guc->capture.reglists);
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;

	guc->capture.reglists = guc_capture_get_device_reglist(dev_priv);
	return 0;
}
