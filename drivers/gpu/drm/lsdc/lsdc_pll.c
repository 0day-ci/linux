// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Loongson Corporation
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
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */
#include <drm/drm_print.h>

#include "lsdc_drv.h"
#include "lsdc_regs.h"
#include "lsdc_pll.h"

/* device dependent pixpll regs */

/* u64 */
struct ls7a1000_pixpll_bitmap {
	/* Byte 0 ~ Byte 3 */
	unsigned div_out      : 7;   /*  0 : 6     output clock divider  */
	unsigned reserved_1   : 14;  /*  7 : 20                          */
	unsigned loopc        : 9;   /* 21 : 29                          */
	unsigned reserved_2   : 2;   /* 30 : 31                          */

	/* Byte 4 ~ Byte 7 */
	unsigned div_ref      : 7;   /*  0 : 6     input clock divider   */
	unsigned locked       : 1;   /*  7         PLL locked flag       */
	unsigned sel_out      : 1;   /*  8         output clk selector   */
	unsigned reserved_3   : 2;   /*  9 : 10    reserved              */
	unsigned set_param    : 1;   /*  11        set pll param         */
	unsigned bypass       : 1;   /*  12                              */
	unsigned powerdown    : 1;   /*  13                              */
	unsigned reserved_4   : 18;  /*  14 : 31                         */
};


/* u128 */
struct ls2k1000_pixpll_bitmap {
	/* Byte 0 ~ Byte 3 */
	unsigned sel_out      :  1;  /*  0      select this PLL          */
	unsigned reserved_1   :  1;  /*  1                               */
	unsigned sw_adj_en    :  1;  /*  2      allow software adjust    */
	unsigned bypass       :  1;  /*  3      bypass L1 PLL            */
	unsigned reserved_2   :  3;  /*  4:6                             */
	unsigned lock_en      :  1;  /*  7      enable lock L1 PLL       */
	unsigned reserved_3   :  2;  /*  8:9                             */
	unsigned lock_check   :  2;  /* 10:11   precision check          */
	unsigned reserved_4   :  4;  /* 12:15                            */

	unsigned locked       :  1;  /* 16      Is L1 PLL locked, RO     */
	unsigned reserved_5   :  2;  /* 17:18                            */
	unsigned powerdown    :  1;  /* 19      powerdown the pll if set */
	unsigned reserved_6   :  6;  /* 20:25                            */
	unsigned div_ref      :  6;  /* 26:31   L1 Prescaler             */

	/* Byte 4 ~ Byte 7 */
	unsigned loopc        : 10;  /* 32:41   Clock Multiplier         */
	unsigned l1_div       :  6;  /* 42:47   not used                 */
	unsigned reserved_7   : 16;  /* 48:63                            */

	/* Byte 8 ~ Byte 15 */
	unsigned div_out      :  6;  /* 0 : 5   output clock divider     */
	unsigned reserved_8   : 26;  /* 6 : 31                           */
	unsigned reserved_9   : 32;  /* 70: 127                          */
};


/* u32 */
struct ls2k0500_pixpll_bitmap {
	/* Byte 0 ~ Byte 1 */
	unsigned sel_out      : 1;
	unsigned reserved_1   : 2;
	unsigned sw_adj_en    : 1;   /* allow software adjust              */
	unsigned bypass       : 1;   /* bypass L1 PLL                      */
	unsigned powerdown    : 1;   /* write 1 to powerdown the PLL       */
	unsigned reserved_2   : 1;
	unsigned locked       : 1;   /*  7     Is L1 PLL locked, read only */
	unsigned div_ref      : 6;   /*  8:13  ref clock divider           */
	unsigned reserved_3   : 2;   /* 14:15                              */
	/* Byte 2 ~ Byte 3 */
	unsigned loopc        : 8;   /* 16:23   Clock Multiplier           */
	unsigned div_out      : 6;   /* 24:29   output clock divider       */
	unsigned reserved_4   : 2;   /* 30:31                              */
};


/*
 * NOTE: All loongson's cpu is little endian.
 * The length of this structure should be 8 bytes
 */
union lsdc_pix_pll_param {
	struct ls7a1000_pixpll_bitmap ls7a1000;
	struct ls2k1000_pixpll_bitmap ls2k1000;
	struct ls2k0500_pixpll_bitmap ls2k0500;

	u32 word[4];
	u64 qword[2];
};

/*
 * pixel clock to pll parameters translation table
 */
struct pixclk_to_pll_parm {
	/* kHz */
	unsigned int clock;

	/* unrelated information */
	unsigned short width;
	unsigned short height;
	unsigned short vrefresh;

	/* Stores parameters for programming the Hardware PLLs */
	unsigned short div_out;
	unsigned short loopc;
	unsigned short div_ref;
};


/*
 * Small cached value to speed up pll parameter calculation
 */
static const struct pixclk_to_pll_parm pll_param_table[] = {
	{148500, 1920, 1080, 60, 11, 49,  3},   /* 1920x1080@60Hz */
						/* 1920x1080@50Hz */
	{174500, 1920, 1080, 75, 17, 89,  3},   /* 1920x1080@75Hz */
	{181250, 2560, 1080, 75,  8, 58,  4},   /* 2560x1080@75Hz */
	{146250, 1680, 1050, 60, 16, 117, 5},   /* 1680x1050@60Hz */
	{135000, 1280, 1024, 75, 10, 54,  4},   /* 1280x1024@75Hz */

	{108000, 1600, 900,  60, 15, 81,  5},   /* 1600x900@60Hz  */
						/* 1280x1024@60Hz */
						/* 1280x960@60Hz */
						/* 1152x864@75Hz */

	{106500, 1440, 900,  60, 19, 81,  4},   /* 1440x900@60Hz */
	{88750,  1440, 900,  60, 16, 71,  5},   /* 1440x900@60Hz */
	{83500,  1280, 800,  60, 17, 71,  5},   /* 1280x800@60Hz */
	{71000,  1280, 800,  60, 20, 71,  5},   /* 1280x800@60Hz */

	{74250,  1280, 720,  60, 22, 49,  3},   /* 1280x720@60Hz */
						/* 1280x720@50Hz */

	{78750,  1024, 768,  75, 16, 63,  5},   /* 1024x768@75Hz */
	{75000,  1024, 768,  70, 29, 87,  4},   /* 1024x768@70Hz */
	{65000,  1024, 768,  60, 20, 39,  3},   /* 1024x768@60Hz */

	{51200,  1024, 600,  60, 25, 64,  5},   /* 1024x600@60Hz */

	{57284,  832,  624,  75, 24, 55,  4},   /* 832x624@75Hz */
	{49500,  800,  600,  75, 40, 99,  5},   /* 800x600@75Hz */
	{50000,  800,  600,  72, 44, 88,  4},   /* 800x600@72Hz */
	{40000,  800,  600,  60, 30, 36,  3},   /* 800x600@60Hz */
	{36000,  800,  600,  56, 50, 72,  4},   /* 800x600@56Hz */
	{31500,  640,  480,  75, 40, 63,  5},   /* 640x480@75Hz */
						/* 640x480@73Hz */

	{30240,  640,  480,  67, 62, 75,  4},   /* 640x480@67Hz */
	{27000,  720,  576,  50, 50, 54,  4},   /* 720x576@60Hz */
	{25175,  640,  480,  60, 85, 107, 5},   /* 640x480@60Hz */
	{25200,  640,  480,  60, 50, 63,  5},   /* 640x480@60Hz */
						/* 720x480@60Hz */
};


static int lsdc_pixpll_setup(struct lsdc_pll * const this)
{
	this->mmio = ioremap(this->reg_base, this->reg_size);

	drm_info(this->ddev, "PIXPLL%u REG[%x, %u] map to %llx\n",
		this->index, this->reg_base, this->reg_size, (u64)this->mmio);

	return 0;
}


/**
 * lsdc_crtc_init
 *
 * @ddev: point to the drm_device structure
 * @index: hardware crtc index
 *
 * Init CRTC
 */

/*
 * Find a set of pll parameters (to generate pixel clock) from a static
 * local table, which avoid to compute the pll parameter everytime a
 * modeset is triggered.
 *
 * @this: point to the object which calling this function
 * @clock: the desired pixel clock wanted to generate, the unit is kHz
 * @pout: pointer to where hardware pll parameters(if found) to save
 *
 *  Return true if a parameter is found, otherwise return false.
 */
static bool lsdc_pixpll_find(struct lsdc_pll * const this,
			     unsigned int clock,
			     struct lsdc_pll_core_values * const pout)
{
	unsigned int num = ARRAY_SIZE(pll_param_table);
	unsigned int i;

	for (i = 0; i < num; i++) {
		if (clock != pll_param_table[i].clock)
			continue;

		pout->div_ref = pll_param_table[i].div_ref;
		pout->loopc   = pll_param_table[i].loopc;
		pout->div_out = pll_param_table[i].div_out;

		return true;
	}

	drm_dbg(this->ddev, "pixel clock %u: miss\n", clock);

	return false;
}

/*
 * Find a set of pll parameters which have minimal difference with desired
 * clock frequency. It does that by computing the all of pll parameters
 * combines possible and compare the diff and find the minimal.
 *
 *  clock_out = refclk / div_ref * loopc / div_out
 *
 *  refclk is fixed as 100MHz in ls7a1000, ls2k1000 and ls2k0500
 *
 * @this: point to the object which calling this function
 * @clk: the desired pixel clock wanted to generate, the unit is kHz
 * @verbose: print the pll parameter and the actual pixel clock.
 * @pout: pointer to where hardware pll parameters(if found) to save
 *
 *  Return true if a parameter is found, otherwise return false.
 */
static bool lsdc_pixpll_compute(struct lsdc_pll * const this,
				unsigned int clk,
				bool verbose,
				struct lsdc_pll_core_values * const pout)
{
	unsigned int refclk = this->ref_clock;
	const unsigned int tolerance = 1000;
	unsigned int min = tolerance;
	unsigned int div_out, loopc, div_ref;

	if (lsdc_pixpll_find(this, clk, pout))
		return true;

	for (div_out = 6; div_out < 64; div_out++) {
		for (div_ref = 3; div_ref < 6; div_ref++) {
			for (loopc = 6; loopc < 161; loopc++) {
				int diff;

				if (loopc < 12 * div_ref)
					continue;
				if (loopc > 32 * div_ref)
					continue;

				diff = clk * div_out - refclk * loopc / div_ref;

				if (diff < 0)
					diff = -diff;

				if (diff < min) {
					min = diff;
					pout->div_ref = div_ref;
					pout->div_out = div_out;
					pout->loopc = loopc;

					if (diff == 0)
						return true;
				}
			}
		}
	}

	if (verbose) {
		unsigned int clk_out;

		clk_out = refclk / pout->div_ref * pout->loopc / pout->div_out;

		drm_info(this->ddev, "pixpll%u\n", this->index);

		drm_info(this->ddev, "div_ref=%u, loopc=%u, div_out=%u\n",
				pout->div_ref, pout->loopc, pout->div_out);

		drm_info(this->ddev, "desired clk=%u, actual out=%u, diff=%d\n",
				clk, clk_out, clk_out - clk);
	}

	return min < tolerance;
}

/*
 * Update the pll parameters to hardware
 *
 * @this: point to the object which calling this function
 * @param: pointer to where the parameters pass in
 *
 *  Return true if a parameter is found, otherwise return false.
 */
static int ls7a1000_pixpll_param_update(struct lsdc_pll * const this,
			const struct lsdc_pll_core_values * const param)
{
	u32 val;
	unsigned int counter = 0;
	void __iomem *reg = this->mmio;
	bool locked;


	/* clear sel_pll_out0 */
	val = readl(reg + 0x4);
	val &= ~(1 << 8);
	writel(val, reg + 0x4);

	/* set pll_pd */
	val = readl(reg + 0x4);
	val |= (1 << 13);
	writel(val, reg + 0x4);

	/* clear set_pll_param */
	val = readl(reg + 0x4);
	val &= ~(1 << 11);
	writel(val, reg + 0x4);

	/* clear old value & config new value */
	val = readl(reg + 0x04);
	val &= ~0x7F;

	val |= param->div_ref;        /* div_ref */
	writel(val, reg + 0x4);

	val = readl(reg);
	val &= ~(0x7f << 0);
	val |= param->div_out;        /* div_out */
	val &= ~(0x1ffUL << 21);
	val |= param->loopc << 21;    /* loopc */
	writel(val, reg);

	/* set set_pll_param */
	val = readl(reg + 0x4);
	val |= (1 << 11);
	writel(val, reg + 0x4);

	/* clear pll_pd */
	val = readl(reg + 0x4);
	val &= ~(1 << 13);
	writel(val, reg + 0x4);

	/* wait pll lock */
	do {
		val = readl(reg + 0x4);
		locked = val & 0x80;
		counter++;
	} while (locked == false);

	drm_dbg_kms(this->ddev, "%u loop waited\n", counter);

	/* set sel_pll_out0 */
	val = readl(reg + 0x4);
	val |= (1UL << 8);
	writel(val, reg + 0x4);

	return 0;
}


/*
 * the PIX PLL be software configurable when SYS_CLKSEL[1:0] is 10b
 */
static int ls2k1000_pixpll_param_update(struct lsdc_pll * const this,
			const struct lsdc_pll_core_values * const param)
{
	void __iomem *reg = this->mmio;
	u64 val = readq(reg);
	bool locked;
	unsigned int counter = 0;

	val &= ~(1 << 0);    /* Bypass the PLL, using refclk directly */
	val |= (1 << 19);    /* powerdown the PLL */
	val &= ~(1 << 2);    /* don't use the software configure param */
	writeq(val, reg);

	val = (1L << 7) | (1L << 42) | (3L << 10);   /* allow L1 PLL locked */
	val |= (unsigned long)param->loopc << 32;    /* set loopc   */
	val |= (unsigned long)param->div_ref << 26;  /* set div_ref */
	writeq(val, reg);
	writeq(param->div_out, reg + 8);             /* set div_out */

	val = readq(reg);
	val |= (1 << 2);     /* use the software configure param */
	val &= ~(1 << 19);   /* powerup the PLL */
	writeq(val, reg);

	/* wait pll setup and locked */
	do {
		val = readl(reg);
		locked = val & 0x10000;
		counter++;
	} while (locked == false);

	drm_dbg_kms(this->ddev, "%u loop waited\n", counter);

	val = readq(reg);
	val |= (1 << 0);    /* switch to the software configured pll */
	writeq(val, reg);

	return 0;
}


static int ls2k0500_pixpll_param_update(struct lsdc_pll * const this,
			const struct lsdc_pll_core_values * const param)
{
	void __iomem *reg = this->mmio;
	u64 val;

	/* set sel_pll_out0 0 */
	val = readq(reg);
	val &= ~(1UL << 0);
	writeq(val, reg);

	/* pll_pd 1 */
	val = readq(reg);
	val |= (1UL << 5);
	writeq(val, reg);

	val = (param->div_out << 24) |
	      (param->loopc << 16) |
	      (param->div_ref << 8);

	writeq(val, reg);
	val |= (1U << 3);
	writeq(val, reg);

	while (!(readq(reg) & 0x80))
		;
	writeq((val | 1), reg);

	return 0;
}


#define LSDC_PIXPLL_BITMAP(type,var,parms) \
		struct type ## _pixpll_bitmap *var = &parms.type

#define LSDC_PIXPLL_PRINT_CODE_BLOCK(ddev,var,index,refclk)                \
{                                                                          \
	out_clk = refclk / var->div_ref * var->loopc / var->div_out;       \
	drm_info(ddev, "div_ref=%u, loopc=%u, div_out=%u\n",               \
			var->div_ref, var->loopc, var->div_out);           \
	drm_info(ddev, "locked: %s\n", var->locked ? "Yes" : "No");        \
	drm_info(ddev, "bypass: %s\n", var->bypass ? "Yes" : "No");        \
	drm_info(ddev, "powerdown: %s\n", var->powerdown ? "Yes" : "No");  \
	drm_info(ddev, "set_out: %s\n", var->sel_out ? "Yes" : "No");      \
	drm_info(ddev, "pixpll%u generate %ukHz\n", index, out_clk);       \
	drm_info(ddev, "\n");                                              \
}

/* lsdc_print_clock - print clock related parameters
 *
 * clock_out = refclk / div_ref * loopc / divout
 *
 * also print the precision information
 */
static void lsdc_pixpll_print(struct lsdc_pll * const this, unsigned int pixclk)
{
	struct drm_device *ddev = this->ddev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_chip_desc * const ip = ldev->desc;
	unsigned short index = this->index;
	unsigned int refclk = this->ref_clock;
	unsigned int out_clk;
	union lsdc_pix_pll_param parms;

	if (ip->chip == LSDC_CHIP_7A1000) {
		/* struct ls7a1000_pixpll_bitmap *obj = &parms.ls7a1000; */
		LSDC_PIXPLL_BITMAP(ls7a1000, obj, parms);
		parms.qword[0] = readq(this->mmio);
		LSDC_PIXPLL_PRINT_CODE_BLOCK(ddev, obj, index, refclk);
	} else if (ip->chip == LSDC_CHIP_2K1000) {
		/* struct ls2k1000_pixpll_bitmap *obj = &parms.ls2k1000; */
		LSDC_PIXPLL_BITMAP(ls2k1000, obj, parms);
		parms.qword[0] = readq(this->mmio);
		parms.qword[1] = readq(this->mmio + 8);
		LSDC_PIXPLL_PRINT_CODE_BLOCK(ddev, obj, index, refclk);
	} else if (ip->chip == LSDC_CHIP_2K0500) {
		/* struct ls2k0500_pixpll_bitmap *obj = &parms.ls2k0500; */
		LSDC_PIXPLL_BITMAP(ls2k0500, obj, parms);
		parms.word[0] = readl(this->mmio);
		LSDC_PIXPLL_PRINT_CODE_BLOCK(ddev, obj, index, refclk);
	} else {
		drm_err(ddev, "unknown chip, the driver need update\n");
		return;
	}
}


static unsigned int lsdc_get_clock_rate(struct lsdc_pll * const this)
{
	struct lsdc_pll_core_values * const parms = &this->core_params;
	unsigned int clk_out = this->ref_clock / parms->div_ref;

	clk_out = clk_out * parms->loopc / parms->div_out;

	return clk_out;
}


static const struct lsdc_pixpll_funcs ls7a1000_pixpll_funcs = {
	.setup = lsdc_pixpll_setup,
	.compute = lsdc_pixpll_compute,
	.update = ls7a1000_pixpll_param_update,
	.get_clock_rate = lsdc_get_clock_rate,

	.print = lsdc_pixpll_print,
};

static const struct lsdc_pixpll_funcs ls2k1000_pixpll_funcs = {
	.setup = lsdc_pixpll_setup,
	.compute = lsdc_pixpll_compute,
	.update = ls2k1000_pixpll_param_update,
	.get_clock_rate = lsdc_get_clock_rate,

	.print = lsdc_pixpll_print,
};

static const struct lsdc_pixpll_funcs ls2k0500_pixpll_funcs = {
	.setup = lsdc_pixpll_setup,
	.compute = lsdc_pixpll_compute,
	.update = ls2k0500_pixpll_param_update,
	.get_clock_rate = lsdc_get_clock_rate,

	.print = lsdc_pixpll_print,
};


int lsdc_pixpll_init(struct lsdc_pll * const this,
		     struct drm_device *ddev,
		     unsigned int index)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_chip_desc *ip = ldev->desc;

	this->ddev = ddev;
	this->index = index;
	this->ref_clock = LSDC_PLL_REF_CLK;

	if (ip->chip == LSDC_CHIP_7A1000) {
		if (index == 0)
			this->reg_base = LS7A1000_CFG_REG_BASE +
					 LS7A1000_PIX_PLL0_REG;
		else if (index == 1)
			this->reg_base = LS7A1000_CFG_REG_BASE +
					 LS7A1000_PIX_PLL1_REG;
		this->reg_size = 8;
		this->funcs = &ls7a1000_pixpll_funcs;
	} else if (ip->chip == LSDC_CHIP_2K1000) {
		if (index == 0)
			this->reg_base = LS2K1000_CFG_REG_BASE +
					 LS2K1000_PIX_PLL0_REG;
		else if (index == 1)
			this->reg_base = LS2K1000_CFG_REG_BASE +
					 LS2K1000_PIX_PLL1_REG;

		this->reg_size = 16;
		this->funcs = &ls2k1000_pixpll_funcs;
	} else if (ip->chip == LSDC_CHIP_2K0500) {
		if (index == 0)
			this->reg_base = LS2K0500_CFG_REG_BASE +
					 LS2K0500_PIX_PLL0_REG;
		else if (index == 1)
			this->reg_base = LS2K0500_CFG_REG_BASE +
					 LS2K0500_PIX_PLL1_REG;

		this->reg_size = 4;
		this->funcs = &ls2k0500_pixpll_funcs;
	} else {
		DRM_ERROR("unknown chip, the driver need update\n");
		return -ENOENT;
	}

	/* call funcs->init only once */
	return this->funcs->setup(this);
}
