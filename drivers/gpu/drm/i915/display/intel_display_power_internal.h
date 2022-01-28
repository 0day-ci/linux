/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_POWER_INTERNAL_H__
#define __INTEL_DISPLAY_POWER_INTERNAL_H__

#include "i915_reg_defs.h"

#include "intel_display.h"
#include "intel_display_power.h"

struct i915_power_well_regs;

/* Power well structure for haswell */
struct i915_power_well_desc {
	const char *name;
	bool always_on;
	u64 domains;
	/* unique identifier for this power well */
	enum i915_power_well_id id;
	/*
	 * Arbitraty data associated with this power well. Platform and power
	 * well specific.
	 */
	union {
		struct {
			/*
			 * request/status flag index in the PUNIT power well
			 * control/status registers.
			 */
			u8 idx;
		} vlv;
		struct {
			enum dpio_phy phy;
		} bxt;
		struct {
			/*
			 * request/status flag index in the power well
			 * constrol/status registers.
			 */
			u8 idx;
			/* Mask of pipes whose IRQ logic is backed by the pw */
			u8 irq_pipe_mask;
			/*
			 * Instead of waiting for the status bit to ack enables,
			 * just wait a specific amount of time and then consider
			 * the well enabled.
			 */
			u16 fixed_enable_delay;
			/* The pw is backing the VGA functionality */
			bool has_vga:1;
			bool has_fuses:1;
			/*
			 * The pw is for an ICL+ TypeC PHY port in
			 * Thunderbolt mode.
			 */
			bool is_tc_tbt:1;
		} hsw;
	};
	const struct i915_power_well_ops *ops;
};

struct i915_power_well {
	const struct i915_power_well_desc *desc;
	/* power well enable/disable usage count */
	int count;
	/* cached hw enabled state */
	bool hw_enabled;
};

/* intel_display_power.c */
extern const struct i915_power_well_ops i9xx_always_on_power_well_ops;
extern const struct i915_power_well_ops chv_pipe_power_well_ops;
extern const struct i915_power_well_ops chv_dpio_cmn_power_well_ops;
extern const struct i915_power_well_ops i830_pipes_power_well_ops;
extern const struct i915_power_well_ops hsw_power_well_ops;
extern const struct i915_power_well_ops hsw_power_well_ops;
extern const struct i915_power_well_ops gen9_dc_off_power_well_ops;
extern const struct i915_power_well_ops bxt_dpio_cmn_power_well_ops;
extern const struct i915_power_well_ops vlv_display_power_well_ops;
extern const struct i915_power_well_ops vlv_dpio_cmn_power_well_ops;
extern const struct i915_power_well_ops vlv_dpio_power_well_ops;
extern const struct i915_power_well_ops icl_ddi_power_well_ops;
extern const struct i915_power_well_ops icl_aux_power_well_ops;
extern const struct i915_power_well_ops tgl_tc_cold_off_ops;

/* intel_display_power_map.c */
int intel_init_power_wells(struct i915_power_domains *power_domains);
void intel_cleanup_power_wells(struct i915_power_domains *power_domains);

#endif
