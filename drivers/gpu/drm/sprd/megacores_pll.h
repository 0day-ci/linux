/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _MEGACORES_PLL_H_
#define _MEGACORES_PLL_H_

#include "sprd_dsi.h"

enum PLL_TIMING {
	NONE,
	REQUEST_TIME,
	PREPARE_TIME,
	SETTLE_TIME,
	ZERO_TIME,
	TRAIL_TIME,
	EXIT_TIME,
	CLKPOST_TIME,
	TA_GET,
	TA_GO,
	TA_SURE,
	TA_WAIT,
};

struct pll_reg {
	union __reg_03__ {
		struct __03 {
			u8 prbs_bist: 1;
			u8 en_lp_treot: 1;
			u8 lpf_sel: 4;
			u8 txfifo_bypass: 1;
			u8 freq_hopping: 1;
		} bits;
		u8 val;
	} _03;
	union __reg_04__ {
		struct __04 {
			u8 div: 3;
			u8 masterof8lane: 1;
			u8 hop_trig: 1;
			u8 cp_s: 2;
			u8 fdk_s: 1;
		} bits;
		u8 val;
	} _04;
	union __reg_06__ {
		struct __06 {
			u8 nint: 7;
			u8 mod_en: 1;
		} bits;
		u8 val;
	} _06;
	union __reg_07__ {
		struct __07 {
			u8 kdelta_h: 8;
		} bits;
		u8 val;
	} _07;
	union __reg_08__ {
		struct __08 {
			u8 vco_band: 1;
			u8 sdm_en: 1;
			u8 refin: 2;
			u8 kdelta_l: 4;
		} bits;
		u8 val;
	} _08;
	union __reg_09__ {
		struct __09 {
			u8 kint_h: 8;
		} bits;
		u8 val;
	} _09;
	union __reg_0a__ {
		struct __0a {
			u8 kint_m: 8;
		} bits;
		u8 val;
	} _0a;
	union __reg_0b__ {
		struct __0b {
			u8 out_sel: 4;
			u8 kint_l: 4;
		} bits;
		u8 val;
	} _0b;
	union __reg_0c__ {
		struct __0c {
			u8 kstep_h: 8;
		} bits;
		u8 val;
	} _0c;
	union __reg_0d__ {
		struct __0d {
			u8 kstep_m: 8;
		} bits;
		u8 val;
	} _0d;
	union __reg_0e__ {
		struct __0e {
			u8 pll_pu_byp: 1;
			u8 pll_pu: 1;
			u8 hsbist_len: 2;
			u8 stopstate_sel: 1;
			u8 kstep_l: 3;
		} bits;
		u8 val;
	} _0e;
	union __reg_0f__ {
		struct __0f {
			u8 det_delay:2;
			u8 kdelta: 4;
			u8 ldo0p4:2;
		} bits;
		u8 val;
	} _0f;
};

struct dphy_pll {
	u8 refin; /* Pre-divider control signal */
	u8 cp_s; /* 00: SDM_EN=1, 10: SDM_EN=0 */
	u8 fdk_s; /* PLL mode control: integer or fraction */
	u8 sdm_en;
	u8 div;
	u8 int_n; /* integer N PLL */
	u32 ref_clk; /* dphy reference clock, unit: MHz */
	u32 freq; /* panel config, unit: KHz */
	u32 fvco;
	u32 potential_fvco;
	u32 nint; /* sigma delta modulator NINT control */
	u32 kint; /* sigma delta modulator KINT control */
	u8 lpf_sel; /* low pass filter control */
	u8 out_sel; /* post divider control */
	u8 vco_band; /* vco range */
	u8 det_delay;

	struct pll_reg reg;
};

struct dsi_context;

int dphy_pll_config(struct dsi_context *ctx);
void dphy_timing_config(struct dsi_context *ctx);

#endif /* _MEGACORES_PLL_H_ */
