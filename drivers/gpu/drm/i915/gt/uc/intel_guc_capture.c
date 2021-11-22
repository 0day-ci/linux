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

#include <linux/circ_buf.h>

#include "intel_guc_fwif.h"
#include "intel_guc_capture.h"
#include "i915_gpu_error.h"

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
 *     --> alloc C: GuC capture interim circular buffer storage in system mem
 *                  Size = 'power_of_two(sizeof(B))' as per kernel circular buffer helper
 *
 * GUC Runtime notify capture:
 * --------------------------
 *     --> G2H STATE_CAPTURE_NOTIFICATION
 *                   L--> intel_guc_capture_store_snapshot
 *                        L--> queue(__guc_capture_store_snapshot_work)
 *                             Copies from B (head->tail) into C
 *
 * GUC --> notify context reset:
 * -----------------------------
 *     --> G2H CONTEXT RESET
 *                   L--> guc_handle_context_reset --> i915_capture_error_state
 *                    --> i915_gpu_coredump --> intel_guc_capture_store_ptr
 *                        L--> keep a ptr to capture_store in
 *                             i915_gpu_coredump struct.
 *
 * User Sysfs / Debugfs
 * --------------------
 *      --> i915_gpu_coredump_copy_to_buffer->
 *                   L--> err_print_to_sgl --> err_print_gt
 *                        L--> error_print_guc_captures
 *                             L--> loop: intel_guc_capture_out_print_next_group
 *
 */

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)

static char *
guc_capture_register_string(const struct intel_guc *guc, u32 owner, u32 type,
			    u32 class, u32 id, u32 offset)
{
	struct __guc_mmio_reg_descr_group *reglists = guc->capture.reglists;
	struct __guc_mmio_reg_descr_group *match;
	int num_regs, j = 0;

	if (!reglists)
		return NULL;

	match = guc_capture_get_one_list(reglists, owner, type, id);
	if (match) {
		num_regs = match->num_regs;
		while (num_regs--) {
			if (offset == match->list[j].reg.reg)
				return match->list[j].regname;
			++j;
		}
	}

	return NULL;
}

static inline int
guc_capture_store_remove_dw(struct guc_capture_out_store *store, u32 *bytesleft,
			    u32 *dw)
{
	int tries = 2;
	int avail = 0;
	u32 *src_data;

	if (!*bytesleft)
		return 0;

	while (tries--) {
		avail = CIRC_CNT_TO_END(store->head, store->tail, store->size);
		if (avail >= sizeof(u32)) {
			src_data = (u32 *)(store->addr + store->tail);
			*dw = *src_data;
			store->tail = (store->tail + 4) & (store->size - 1);
			*bytesleft -= 4;
			return 4;
		}
		if (store->tail == (store->size - 1) && store->head > 0)
			store->tail = 0;
	}

	return 0;
}

static int
capture_store_get_group_hdr(const struct intel_guc *guc,
			    struct guc_capture_out_store *store, u32 *bytesleft,
			    struct intel_guc_capture_out_group_header *group)
{
	int read = 0;
	int fullsize = sizeof(struct intel_guc_capture_out_group_header);

	if (fullsize > *bytesleft)
		return -1;

	if (CIRC_CNT_TO_END(store->head, store->tail, store->size) >= fullsize) {
		    memcpy(group, (store->addr + store->tail), fullsize);
			store->tail = (store->tail + fullsize) & (store->size - 1);
			*bytesleft -= fullsize;
		return 0;
	}

	read += guc_capture_store_remove_dw(store, bytesleft, &group->reserved1);
	read += guc_capture_store_remove_dw(store, bytesleft, &group->info);
	if (read != sizeof(*group))
		return -1;

	return 0;
}

static int
capture_store_get_data_hdr(const struct intel_guc *guc,
			   struct guc_capture_out_store *store, u32 *bytesleft,
			   struct intel_guc_capture_out_data_header *data)
{
	int read = 0;
	int fullsize = sizeof(struct intel_guc_capture_out_data_header);

	if (fullsize > *bytesleft)
		return -1;

	if (CIRC_CNT_TO_END(store->head, store->tail, store->size) >= fullsize) {
		    memcpy(data, (store->addr + store->tail), fullsize);
			store->tail = (store->tail + fullsize) & (store->size - 1);
			*bytesleft -= fullsize;
		return 0;
	}

	read += guc_capture_store_remove_dw(store, bytesleft, &data->reserved1);
	read += guc_capture_store_remove_dw(store, bytesleft, &data->info);
	read += guc_capture_store_remove_dw(store, bytesleft, &data->lrca);
	read += guc_capture_store_remove_dw(store, bytesleft, &data->guc_ctx_id);
	read += guc_capture_store_remove_dw(store, bytesleft, &data->num_mmios);
	if (read != sizeof(*data))
		return -1;

	return 0;
}

static int
capture_store_get_register(const struct intel_guc *guc,
			   struct guc_capture_out_store *store, u32 *bytesleft,
			   struct guc_mmio_reg *reg)
{
	int read = 0;
	int fullsize = sizeof(struct guc_mmio_reg);

	if (fullsize > *bytesleft)
		return -1;

	if (CIRC_CNT_TO_END(store->head, store->tail, store->size) >= fullsize) {
		    memcpy(reg, (store->addr + store->tail), fullsize);
			store->tail = (store->tail + fullsize) & (store->size - 1);
			*bytesleft -= fullsize;
		return 0;
	}

	read += guc_capture_store_remove_dw(store, bytesleft, &reg->offset);
	read += guc_capture_store_remove_dw(store, bytesleft, &reg->value);
	read += guc_capture_store_remove_dw(store, bytesleft, &reg->flags);
	read += guc_capture_store_remove_dw(store, bytesleft, &reg->mask);
	if (read != sizeof(*reg))
		return -1;

	return 0;
}

static void guc_capture_store_drop_data(struct guc_capture_out_store *store,
					unsigned long sampled_head)
{
	if (sampled_head == 0)
		store->tail = store->size - 1;
	else
		store->tail = sampled_head - 1;
}

#ifdef CONFIG_DRM_I915_DEBUG_GUC
#define guc_capt_err_print(a, b, ...) \
	do { \
		drm_warn(a, __VA_ARGS__); \
		if (b) \
			i915_error_printf(b, __VA_ARGS__); \
	} while (0)
#else
#define guc_capt_err_print(a, b, ...) \
	do { \
		if (b) \
			i915_error_printf(b, __VA_ARGS__); \
	} while (0)
#endif

static struct intel_engine_cs *
guc_lookup_engine(struct intel_guc *guc, u8 guc_class, u8 instance)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u8 engine_class = guc_class_to_engine_class(guc_class);

	/* Class index is checked in class converter */
	GEM_BUG_ON(instance > MAX_ENGINE_INSTANCE);

	return gt->engine_class[engine_class][instance];
}

static inline struct intel_context *
guc_context_lookup(struct intel_guc *guc, u32 guc_ctx_id)
{
	struct intel_context *ce;

	if (unlikely(guc_ctx_id >= GUC_MAX_LRC_DESCRIPTORS)) {
		drm_dbg(&guc_to_gt(guc)->i915->drm, "Invalid guc_ctx_id 0x%X, max 0x%X",
			guc_ctx_id, GUC_MAX_LRC_DESCRIPTORS);
		return NULL;
	}

	ce = xa_load(&guc->context_lookup, guc_ctx_id);
	if (unlikely(!ce)) {
		drm_dbg(&guc_to_gt(guc)->i915->drm, "Context is NULL, guc_ctx_id 0x%X",
			guc_ctx_id);
		return NULL;
	}

	return ce;
}


#define PRINT guc_capt_err_print
#define REGSTR guc_capture_register_string

#define GCAP_PRINT_INTEL_ENG_INFO(i915, ebuf, eng) \
	PRINT(&(i915->drm), (ebuf), "    i915-Eng-Name: %s\n", (eng)->name); \
	PRINT(&(i915->drm), (ebuf), "    i915-Eng-Class: 0x%02x\n", (eng)->class); \
	PRINT(&(i915->drm), (ebuf), "    i915-Eng-Inst: 0x%02x\n", (eng)->instance); \
	PRINT(&(i915->drm), (ebuf), "    i915-Eng-LogicalMask: 0x%08x\n", (eng)->logical_mask)

#define GCAP_PRINT_GUC_INST_INFO(i915, ebuf, data) \
	PRINT(&(i915->drm), (ebuf), "    LRCA: 0x%08x\n", (data).lrca); \
	PRINT(&(i915->drm), (ebuf), "    GuC-ContextID: 0x%08x\n", (data).guc_ctx_id); \
	PRINT(&(i915->drm), (ebuf), "    GuC-Engine-Instance: 0x%08x\n", \
	      (uint32_t) FIELD_GET(GUC_CAPTURE_DATAHDR_SRC_INSTANCE, (data).info));

#define GCAP_PRINT_INTEL_CTX_INFO(i915, ebuf, ce) \
	PRINT(&(i915->drm), (ebuf), "    i915-Ctx-Flags: 0x%016lx\n", (ce)->flags); \
	PRINT(&(i915->drm), (ebuf), "    i915-Ctx-GuC-ID: 0x%016x\n", (ce)->guc_id.id);

int intel_guc_capture_out_print_next_group(struct drm_i915_error_state_buf *ebuf,
					   struct intel_gt_coredump *gt)
{
	/* constant qualifier for data-pointers we shouldn't change mid of error dump printing */
	struct intel_guc_state_capture *cap = gt->uc->capture;
	struct intel_guc *guc = container_of(cap, struct intel_guc, capture);
	struct drm_i915_private *i915 = (container_of(guc, struct intel_gt,
						   uc.guc))->i915;
	struct guc_capture_out_store *store = &cap->out_store;
	struct guc_capture_out_store tmpstore;
	struct intel_guc_capture_out_group_header group;
	struct intel_guc_capture_out_data_header data;
	struct guc_mmio_reg reg;
	const char *grptypestr[GUC_STATE_CAPTURE_GROUP_TYPE_MAX] = {"full-capture",
								    "partial-capture"};
	const char *datatypestr[GUC_CAPTURE_LIST_TYPE_MAX] = {"Global", "Engine-Class",
							      "Engine-Instance"};
	enum guc_capture_group_types grptype;
	enum guc_capture_type datatype;
	int numgrps, numregs;
	char *str, noname[16];
	u32 numbytes, engineclass, eng_inst, ret = 0;
	struct intel_engine_cs *eng;
	struct intel_context *ce;

	if (!cap->enabled)
		return -ENODEV;

	mutex_lock(&store->lock);
	smp_mb(); /* sync to get the latest head for the moment */
	/* NOTE1: make a copy of store so we dont have to deal with a changing lower bound of
	 *        occupied-space in this circular buffer.
	 * NOTE2: Higher up the stack from here, we keep calling this function in a loop to
	 *        reading more capture groups as they appear (as the lower bound of occupied-space
	 *        changes) until this circ-buf is empty.
	 */
	memcpy(&tmpstore, store, sizeof(tmpstore));

	PRINT(&i915->drm, ebuf, "global --- GuC Error Capture\n");

	numbytes = CIRC_CNT(tmpstore.head, tmpstore.tail, tmpstore.size);
	if (!numbytes) {
		PRINT(&i915->drm, ebuf, "GuC capture stream empty!\n");
		ret = -ENODATA;
		goto unlock;
	}
	/* everything in GuC output structures are dword aligned */
	if (numbytes & 0x3) {
		PRINT(&i915->drm, ebuf, "GuC capture stream unaligned!\n");
		ret = -EIO;
		goto unlock;
	}

	if (capture_store_get_group_hdr(guc, &tmpstore, &numbytes, &group)) {
		PRINT(&i915->drm, ebuf, "GuC capture error getting next group-header!\n");
		ret = -EIO;
		goto unlock;
	}

	PRINT(&i915->drm, ebuf, "NumCaptures:  0x%08x\n", (uint32_t)
	      FIELD_GET(GUC_CAPTURE_GRPHDR_SRC_NUMCAPTURES, group.info));
	grptype = FIELD_GET(GUC_CAPTURE_GRPHDR_SRC_CAPTURE_TYPE, group.info);
	PRINT(&i915->drm, ebuf, "Coverage:  0x%08x = %s\n", grptype,
	      grptypestr[grptype % GUC_STATE_CAPTURE_GROUP_TYPE_MAX]);

	numgrps = FIELD_GET(GUC_CAPTURE_GRPHDR_SRC_NUMCAPTURES, group.info);
	while (numgrps--) {
		eng = NULL;
		ce = NULL;

		if (capture_store_get_data_hdr(guc, &tmpstore, &numbytes, &data)) {
			PRINT(&i915->drm, ebuf, "GuC capture error on next data-header!\n");
			ret = -EIO;
			goto unlock;
		}
		datatype = FIELD_GET(GUC_CAPTURE_DATAHDR_SRC_TYPE, data.info);
		PRINT(&i915->drm, ebuf, "  RegListType: %s\n",
		      datatypestr[datatype % GUC_CAPTURE_LIST_TYPE_MAX]);

		engineclass = FIELD_GET(GUC_CAPTURE_DATAHDR_SRC_CLASS, data.info);
		if (datatype != GUC_CAPTURE_LIST_TYPE_GLOBAL) {
			PRINT(&i915->drm, ebuf, "    GuC-Engine-Class: %d\n",
			      engineclass);
			if (engineclass <= GUC_LAST_ENGINE_CLASS)
				PRINT(&i915->drm, ebuf, "    i915-Eng-Class: %d\n",
				      guc_class_to_engine_class(engineclass));

			if (datatype == GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE) {
				GCAP_PRINT_GUC_INST_INFO(i915, ebuf, data);
				eng_inst = FIELD_GET(GUC_CAPTURE_DATAHDR_SRC_INSTANCE, data.info);
				eng = guc_lookup_engine(guc, engineclass, eng_inst);
				if (eng) {
					GCAP_PRINT_INTEL_ENG_INFO(i915, ebuf, eng);
				} else {
					PRINT(&i915->drm, ebuf, "    i915-Eng-Lookup Fail!\n");
				}
				ce = guc_context_lookup(guc, data.guc_ctx_id);
				if (ce) {
					GCAP_PRINT_INTEL_CTX_INFO(i915, ebuf, ce);
				} else {
					PRINT(&i915->drm, ebuf, "    i915-Ctx-Lookup Fail!\n");
				}
			}
		}
		numregs = FIELD_GET(GUC_CAPTURE_DATAHDR_NUM_MMIOS, data.num_mmios);
		PRINT(&i915->drm, ebuf, "     NumRegs: 0x%08x\n", numregs);

		while (numregs--) {
			if (capture_store_get_register(guc, &tmpstore, &numbytes, &reg)) {
				PRINT(&i915->drm, ebuf, "Error getting next register!\n");
				ret = -EIO;
				goto unlock;
			}
			str = REGSTR(guc, GUC_CAPTURE_LIST_INDEX_PF, datatype,
				     engineclass, 0, reg.offset);
			if (!str) {
				snprintf(noname, sizeof(noname), "REG-0x%08x", reg.offset);
				str = noname;
			}
			PRINT(&i915->drm, ebuf, "      %s:  0x%08x\n", str, reg.value);

		}
		if (eng) {
			const struct intel_engine_coredump *ee;
			for (ee = gt->engine; ee; ee = ee->next) {
				const struct i915_vma_coredump *vma;
				if (ee->engine == eng) {
					for (vma = ee->vma; vma; vma = vma->next)
						i915_print_error_vma(ebuf, ee->engine, vma);
				}
			}
		}
	}

	store->tail = tmpstore.tail;
unlock:
	/* if we have a stream error, just drop everything */
	if (ret == -EIO) {
		drm_warn(&i915->drm, "Skip GuC capture data print due to stream error\n");
		guc_capture_store_drop_data(store, tmpstore.head);
	}

	mutex_unlock(&store->lock);

	return ret;
}

#undef REGSTR
#undef PRINT

#endif //CONFIG_DRM_I915_DEBUG_GUC

static void guc_capture_store_insert(struct intel_guc *guc, struct guc_capture_out_store *store,
				     unsigned char *new_data, size_t bytes)
{
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;
	unsigned char *dst_data = store->addr;
	unsigned long h, t;
	size_t tmp;

	h = store->head;
	t = store->tail;
	if (CIRC_SPACE(h, t, store->size) >= bytes) {
		while (bytes) {
			tmp = CIRC_SPACE_TO_END(h, t, store->size);
			if (tmp) {
				tmp = tmp < bytes ? tmp : bytes;
				i915_unaligned_memcpy_from_wc(&dst_data[h], new_data, tmp);
				bytes -= tmp;
				new_data += tmp;
				h = (h + tmp) & (store->size - 1);
			} else {
				drm_err(&dev_priv->drm, "circbuf copy-to ptr-corruption!\n");
				break;
			}
		}
		store->head = h;
	} else {
		drm_err(&dev_priv->drm, "GuC capture interim-store insufficient space!\n");
	}
}

static void __guc_capture_store_snapshot_work(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;
	unsigned int buffer_size, read_offset, write_offset, bytes_to_copy, full_count;
	struct guc_log_buffer_state *log_buf_state;
	struct guc_log_buffer_state log_buf_state_local;
	void *src_data, *dst_data = NULL;
	bool new_overflow;

	/* Lock to get the pointer to GuC capture-log-buffer-state */
	mutex_lock(&guc->log_state[GUC_CAPTURE_LOG_BUFFER].lock);
	log_buf_state = guc->log.buf_addr +
			(sizeof(struct guc_log_buffer_state) * GUC_CAPTURE_LOG_BUFFER);
	src_data = guc->log.buf_addr + guc_get_log_buffer_offset(GUC_CAPTURE_LOG_BUFFER);

	/*
	 * Make a copy of the state structure, inside GuC log buffer
	 * (which is uncached mapped), on the stack to avoid reading
	 * from it multiple times.
	 */
	memcpy(&log_buf_state_local, log_buf_state, sizeof(struct guc_log_buffer_state));
	buffer_size = guc_get_log_buffer_size(GUC_CAPTURE_LOG_BUFFER);
	read_offset = log_buf_state_local.read_ptr;
	write_offset = log_buf_state_local.sampled_write_ptr;
	full_count = log_buf_state_local.buffer_full_cnt;

	/* Bookkeeping stuff */
	guc->log_state[GUC_CAPTURE_LOG_BUFFER].flush += log_buf_state_local.flush_to_file;
	new_overflow = guc_check_log_buf_overflow(guc, &guc->log_state[GUC_CAPTURE_LOG_BUFFER],
						  full_count);

	/* Update the state of shared log buffer */
	log_buf_state->read_ptr = write_offset;
	log_buf_state->flush_to_file = 0;

	mutex_unlock(&guc->log_state[GUC_CAPTURE_LOG_BUFFER].lock);

	dst_data = guc->capture.out_store.addr;
	if (dst_data) {
		mutex_lock(&guc->capture.out_store.lock);

		/* Now copy the actual logs. */
		if (unlikely(new_overflow)) {
			/* copy the whole buffer in case of overflow */
			read_offset = 0;
			write_offset = buffer_size;
		} else if (unlikely((read_offset > buffer_size) ||
					(write_offset > buffer_size))) {
			drm_err(&dev_priv->drm, "invalid GuC log capture buffer state!\n");
			/* copy whole buffer as offsets are unreliable */
			read_offset = 0;
			write_offset = buffer_size;
		}

		/* first copy from the tail end of the GuC log capture buffer */
		if (read_offset > write_offset) {
			guc_capture_store_insert(guc, &guc->capture.out_store, src_data,
						 write_offset);
			bytes_to_copy = buffer_size - read_offset;
		} else {
			bytes_to_copy = write_offset - read_offset;
		}
		guc_capture_store_insert(guc, &guc->capture.out_store, src_data + read_offset,
					 bytes_to_copy);

		mutex_unlock(&guc->capture.out_store.lock);
	}
}

static void guc_capture_store_snapshot_work(struct work_struct *work)
{
	struct intel_guc_state_capture *capture =
		container_of(work, struct intel_guc_state_capture, store_work);
	struct intel_guc *guc =
		container_of(capture, struct intel_guc, capture);

	__guc_capture_store_snapshot_work(guc);
}

void  intel_guc_capture_store_snapshot(struct intel_guc *guc)
{
	if (guc->capture.enabled)
		queue_work(system_highpri_wq, &guc->capture.store_work);
}

void intel_guc_capture_store_snapshot_immediate(struct intel_guc *guc)
{
	if (guc->capture.enabled)
		__guc_capture_store_snapshot_work(guc);
}

static void guc_capture_store_destroy(struct intel_guc *guc)
{
	mutex_destroy(&guc->capture.out_store.lock);
	mutex_destroy(&guc->capture.out_store.lock);
	guc->capture.out_store.size = 0;
	kfree(guc->capture.out_store.addr);
	guc->capture.out_store.addr = NULL;
}

static int guc_capture_store_create(struct intel_guc *guc)
{
	/*
	 * Make this interim buffer 3x the GuC capture output buffer so that we can absorb
	 * a little delay when processing the raw capture dumps into text friendly logs
	 * for the i915_gpu_coredump output
	 */
	size_t max_dump_size;
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;

	GEM_BUG_ON(guc->capture.out_store.addr);

	max_dump_size = PAGE_ALIGN(intel_guc_capture_output_min_size_est(guc));
	max_dump_size = roundup_pow_of_two(max_dump_size);

	guc->capture.out_store.addr = kzalloc(max_dump_size, GFP_KERNEL);
	if (!guc->capture.out_store.addr) {
		drm_warn(&dev_priv->drm, "GuC-capture interim-store populated at init!\n");
		return -ENOMEM;
	}
	guc->capture.out_store.size = max_dump_size;
	mutex_init(&guc->capture.out_store.lock);
	mutex_init(&guc->capture.out_store.lock);

	return 0;
}

void intel_guc_capture_destroy(struct intel_guc *guc)
{
	if (!guc->capture.enabled)
		return;

	guc->capture.enabled = false;

	intel_synchronize_irq(guc_to_gt(guc)->i915);
	flush_work(&guc->capture.store_work);
	guc_capture_store_destroy(guc);
	guc_capture_clear_ext_regs(guc->capture.reglists);
}

struct intel_guc_state_capture *
intel_guc_capture_store_ptr(struct intel_guc *guc)
{
	if (!guc->capture.enabled)
		return NULL;
	return &guc->capture;
}

int intel_guc_capture_init(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = (guc_to_gt(guc))->i915;
	int ret;

	guc->capture.reglists = guc_capture_get_device_reglist(dev_priv);
	/*
	 * allocate interim store at init time so we dont require memory
	 * allocation whilst in the midst of the reset + capture
	 */
	ret = guc_capture_store_create(guc);
	if (ret) {
		guc_capture_clear_ext_regs(guc->capture.reglists);
		return ret;
	}

	INIT_WORK(&guc->capture.store_work, guc_capture_store_snapshot_work);
	guc->capture.enabled = true;

	return 0;
}
