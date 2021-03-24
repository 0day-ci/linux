/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_VBIF_H
#define _DPU_HW_VBIF_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

struct dpu_hw_vbif;

/*
 *  Assumption is these functions will be called after clocks are enabled
 */

/**
 * dpu_hw_vbif_set_limit_conf - set transaction limit config
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 * @rd: true for read limit; false for write limit
 * @limit: outstanding transaction limit
 */
void dpu_hw_vbif_set_limit_conf(struct dpu_hw_vbif *vbif,
		u32 xin_id, bool rd, u32 limit);

/**
 * dpu_hw_vbif_get_limit_conf - get transaction limit config
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 * @rd: true for read limit; false for write limit
 * @return: outstanding transaction limit
 */
u32 dpu_hw_vbif_get_limit_conf(struct dpu_hw_vbif *vbif,
		u32 xin_id, bool rd);

/**
 * dpu_hw_vbif_set_halt_ctrl - set halt control
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 * @enable: halt control enable
 */
void dpu_hw_vbif_set_halt_ctrl(struct dpu_hw_vbif *vbif,
		u32 xin_id, bool enable);

/**
 * dpu_hw_vbif_get_halt_ctrl - get halt control
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 * @return: halt control enable
 */
bool dpu_hw_vbif_get_halt_ctrl(struct dpu_hw_vbif *vbif,
		u32 xin_id);

/**
 * dpu_hw_vbif_set_qos_remap - set QoS priority remap
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 * @level: priority level
 * @remap_level: remapped level
 */
void dpu_hw_vbif_set_qos_remap(struct dpu_hw_vbif *vbif,
		u32 xin_id, u32 level, u32 remap_level);

/**
 * dpu_hw_vbif_set_mem_type - set memory type
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 * @value: memory type value
 */
void dpu_hw_vbif_set_mem_type(struct dpu_hw_vbif *vbif,
		u32 xin_id, u32 value);

/**
 * dpu_hw_vbif_clear_errors - clear any vbif errors
 *	This function clears any detected pending/source errors
 *	on the VBIF interface, and optionally returns the detected
 *	error mask(s).
 * @vbif: vbif context driver
 * @pnd_errors: pointer to pending error reporting variable
 * @src_errors: pointer to source error reporting variable
 */
void dpu_hw_vbif_clear_errors(struct dpu_hw_vbif *vbif,
	u32 *pnd_errors, u32 *src_errors);

/**
 * dpu_hw_vbif_set_write_gather_en - set write_gather enable
 * @vbif: vbif context driver
 * @xin_id: client interface identifier
 */
void dpu_hw_vbif_set_write_gather_en(struct dpu_hw_vbif *vbif, u32 xin_id);

struct dpu_hw_vbif {
	/* base */
	struct dpu_hw_blk_reg_map hw;

	/* vbif */
	enum dpu_vbif idx;
	const struct dpu_vbif_cfg *cap;
};

/**
 * dpu_hw_vbif_init - initializes the vbif driver for the passed interface idx
 * @idx:  Interface index for which driver object is required
 * @addr: Mapped register io address of MDSS
 * @m:    Pointer to mdss catalog data
 */
struct dpu_hw_vbif *dpu_hw_vbif_init(enum dpu_vbif idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m);

void dpu_hw_vbif_destroy(struct dpu_hw_vbif *vbif);

#endif /*_DPU_HW_VBIF_H */
