/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_INTF_H
#define _DPU_HW_INTF_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"

struct dpu_hw_intf;

/* intf timing settings */
struct intf_timing_params {
	u32 width;		/* active width */
	u32 height;		/* active height */
	u32 xres;		/* Display panel width */
	u32 yres;		/* Display panel height */

	u32 h_back_porch;
	u32 h_front_porch;
	u32 v_back_porch;
	u32 v_front_porch;
	u32 hsync_pulse_width;
	u32 vsync_pulse_width;
	u32 hsync_polarity;
	u32 vsync_polarity;
	u32 border_clr;
	u32 underflow_clr;
	u32 hsync_skew;
};

struct intf_prog_fetch {
	u8 enable;
	/* vsync counter for the front porch pixel line */
	u32 fetch_start;
};

struct intf_status {
	u8 is_en;		/* interface timing engine is enabled or not */
	u32 frame_count;	/* frame count since timing engine enabled */
	u32 line_count;		/* current line count including blanking */
};

/*
 *  Assumption is these functions will be called after clocks are enabled
 */

/* dpu_hw_intf_setup_timing_engine: programs the timing engine */
void dpu_hw_intf_setup_timing_engine(struct dpu_hw_intf *intf,
		const struct intf_timing_params *p,
		const struct dpu_format *fmt);

/* dpu_hw_intf_setup_prg_fetch : enables/disables the programmable fetch logic */
void dpu_hw_intf_setup_prg_fetch(struct dpu_hw_intf *intf,
		const struct intf_prog_fetch *fetch);

/* dpu_hw_intf_enable_timing_engine: enable/disable timing engine */
void dpu_hw_intf_enable_timing_engine(struct dpu_hw_intf *intf,
		u8 enable);

/* dpu_hw_intf_get_status: returns if timing engine is enabled or not */
void dpu_hw_intf_get_status(struct dpu_hw_intf *intf,
		struct intf_status *status);

/* dpu_hw_intf_get_line_count: reads current vertical line counter */
u32 dpu_hw_intf_get_line_count(struct dpu_hw_intf *intf);

/* dpu_hw_intf_bind_pingpong_blk: enable/disable the connection with pingpong
 * which will feed pixels to this interface */
void dpu_hw_intf_bind_pingpong_blk(struct dpu_hw_intf *intf,
		bool enable,
		const enum dpu_pingpong pp);

struct dpu_hw_intf {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* intf */
	enum dpu_intf idx;
	const struct dpu_intf_cfg *cap;
	const struct dpu_mdss_cfg *mdss;
};

/**
 * to_dpu_hw_intf - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_intf *to_dpu_hw_intf(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_intf, base);
}

/**
 * dpu_hw_intf_init(): Initializes the intf driver for the passed
 * interface idx.
 * @idx:  interface index for which driver object is required
 * @addr: mapped register io address of MDP
 * @m :   pointer to mdss catalog data
 */
struct dpu_hw_intf *dpu_hw_intf_init(enum dpu_intf idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m);

/**
 * dpu_hw_intf_destroy(): Destroys INTF driver context
 * @intf:   Pointer to INTF driver context
 */
void dpu_hw_intf_destroy(struct dpu_hw_intf *intf);

#endif /*_DPU_HW_INTF_H */
