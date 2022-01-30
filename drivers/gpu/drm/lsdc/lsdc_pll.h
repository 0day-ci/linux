/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2020 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/*
 * Authors:
 *	Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifndef __LSDC_PLL_H__
#define __LSDC_PLL_H__

#include <drm/drm_device.h>

/*
 * PIXEL PLL structure
 *
 * refclk: reference frequency, 100 MHz form external oscillator
 * outclk: output frequency desired.
 *
 *
 *               L1       Fref                      Fvco     L2
 * refclk   +-----------+      +------------------+      +---------+   outclk
 * -------> | Prescaler | ---> | Clock Multiplier | ---> | divider | --------->
 *    |     +-----------+      +------------------+      +---------+     ^
 *    |           ^                      ^                    ^          |
 *    |           |                      |                    |          |
 *    |           |                      |                    |          |
 *    |        div_ref                 loopc               div_out       |
 *    |                                                                  |
 *    +------------- sel_out (=1 => bypass above clock) -----------------+
 *
 *
 *  outclk = refclk / div_ref * loopc / div_out;
 *
 * PLL hardware working requirements:
 *
 *  1) 20 MHz <= refclk / div_ref <= 40Mhz
 *  2) 1.2 GHz <= refclk /div_out * loopc <= 3.2 Ghz
 *
 */

struct lsdc_pll_core_values {
	unsigned short div_ref;
	unsigned short loopc;
	unsigned short div_out;
};

struct lsdc_pll;

struct lsdc_pixpll_funcs {
	int (*setup)(struct lsdc_pll * const this);
	bool (*compute)(struct lsdc_pll * const this,
		       unsigned int clock,
		       bool verbose,
		       struct lsdc_pll_core_values * const params_out);
	int (*update)(struct lsdc_pll * const this,
		      const struct lsdc_pll_core_values * const params_in);

	void (*print)(struct lsdc_pll * const this, unsigned int clock);

	unsigned int (*get_clock_rate)(struct lsdc_pll * const this);
};


struct lsdc_pll {
	const struct lsdc_pixpll_funcs *funcs;
	struct drm_device *ddev;
	void __iomem *mmio;

	/* Reg offset of the PLL */
	u32 reg_base;
	u32 reg_size;

	struct lsdc_pll_core_values core_params;

	/* Stores parameters for programming the PLLs */
	unsigned int ref_clock; /* 100000kHz, fixed */

	unsigned short index;
};


int lsdc_pixpll_init(struct lsdc_pll * const this,
		     struct drm_device *ddev,
		     unsigned int index);

#endif
