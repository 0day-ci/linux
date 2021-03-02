// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-prv.h"

#define FGSTCTRL		0x8
#define  FGSYNCMODE_MASK	0x6
#define  FGSYNCMODE(n)		((n) << 6)

#define HTCFG1			0xc
#define  HTOTAL(n)		((((n) - 1) & 0x3fff) << 16)
#define  HACT(n)		((n) & 0x3fff)

#define HTCFG2			0x10
#define  HSEN			BIT(31)
#define  HSBP(n)		((((n) - 1) & 0x3fff) << 16)
#define  HSYNC(n)		(((n) - 1) & 0x3fff)

#define VTCFG1			0x14
#define  VTOTAL(n)		((((n) - 1) & 0x3fff) << 16)
#define  VACT(n)		((n) & 0x3fff)

#define VTCFG2			0x18
#define  VSEN			BIT(31)
#define  VSBP(n)		((((n) - 1) & 0x3fff) << 16)
#define  VSYNC(n)		(((n) - 1) & 0x3fff)

#define INTCONFIG(n)		(0x1c + 4 * (n))
#define  EN			BIT(31)
#define  ROW(n)			(((n) & 0x3fff) << 16)
#define  COL(n)			((n) & 0x3fff)

#define PKICKCONFIG		0x2c
#define SKICKCONFIG		0x30
#define SECSTATCONFIG		0x34
#define FGSRCR1			0x38
#define FGSRCR2			0x3c
#define FGSRCR3			0x40
#define FGSRCR4			0x44
#define FGSRCR5			0x48
#define FGSRCR6			0x4c
#define FGKSDR			0x50

#define PACFG			0x54
#define SACFG			0x58
#define  STARTX(n)		(((n) + 1) & 0x3fff)
#define  STARTY(n)		(((((n) + 1) & 0x3fff)) << 16)

#define FGINCTRL		0x5c
#define FGINCTRLPANIC		0x60
#define  FGDM_MASK		0x7
#define  ENPRIMALPHA		BIT(3)
#define  ENSECALPHA		BIT(4)

#define FGCCR			0x64
#define  CCALPHA(a)		(((a) & 0x1) << 30)
#define  CCRED(r)		(((r) & 0x3ff) << 20)
#define  CCGREEN(g)		(((g) & 0x3ff) << 10)
#define  CCBLUE(b)		((b) & 0x3ff)

#define FGENABLE		0x68
#define  FGEN			BIT(0)

#define FGSLR			0x6c
#define  SHDTOKGEN		BIT(0)

#define FGENSTS			0x70
#define  ENSTS			BIT(0)

#define FGTIMESTAMP		0x74
#define  FRAMEINDEX_SHIFT	14
#define  FRAMEINDEX_MASK	(DPU_FRAMEGEN_MAX_FRAME_INDEX << \
				 FRAMEINDEX_SHIFT)
#define  LINEINDEX_MASK		0x3fff

#define FGCHSTAT		0x78
#define  SECSYNCSTAT		BIT(24)
#define  SFIFOEMPTY		BIT(16)

#define FGCHSTATCLR		0x7c
#define  CLRSECSTAT		BIT(16)

#define FGSKEWMON		0x80
#define FGSFIFOMIN		0x84
#define FGSFIFOMAX		0x88
#define FGSFIFOFILLCLR		0x8c
#define FGSREPD			0x90
#define FGSRFTD			0x94

#define KHZ			1000
#define MIN_PLL_RATE		648000000	/* assume 648MHz */

struct dpu_framegen {
	void __iomem *base;
	struct clk *clk_pll;
	struct clk *clk_disp;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	bool inuse;
	struct dpu_soc *dpu;
};

static inline u32 dpu_fg_read(struct dpu_framegen *fg, unsigned int offset)
{
	return readl(fg->base + offset);
}

static inline void dpu_fg_write(struct dpu_framegen *fg,
				unsigned int offset, u32 value)
{
	writel(value, fg->base + offset);
}

static inline void dpu_fg_write_mask(struct dpu_framegen *fg,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_fg_read(fg, offset);
	tmp &= ~mask;
	dpu_fg_write(fg, offset, tmp | value);
}

static void dpu_fg_enable_shden(struct dpu_framegen *fg)
{
	dpu_fg_write_mask(fg, FGSTCTRL, SHDEN, SHDEN);
}

void dpu_fg_syncmode(struct dpu_framegen *fg, enum dpu_fg_syncmode mode)
{
	dpu_fg_write_mask(fg, FGSTCTRL, FGSYNCMODE_MASK, FGSYNCMODE(mode));
}

void dpu_fg_cfg_videomode(struct dpu_framegen *fg, struct drm_display_mode *m)
{
	u32 hact, htotal, hsync, hsbp;
	u32 vact, vtotal, vsync, vsbp;
	u32 kick_row, kick_col;
	unsigned long pclk_rate, pll_rate = 0;
	int div = 0;

	hact = m->crtc_hdisplay;
	htotal = m->crtc_htotal;
	hsync = m->crtc_hsync_end - m->crtc_hsync_start;
	hsbp = m->crtc_htotal - m->crtc_hsync_start;

	vact = m->crtc_vdisplay;
	vtotal = m->crtc_vtotal;
	vsync = m->crtc_vsync_end - m->crtc_vsync_start;
	vsbp = m->crtc_vtotal - m->crtc_vsync_start;

	/* video mode */
	dpu_fg_write(fg, HTCFG1, HACT(hact)   | HTOTAL(htotal));
	dpu_fg_write(fg, HTCFG2, HSYNC(hsync) | HSBP(hsbp) | HSEN);
	dpu_fg_write(fg, VTCFG1, VACT(vact)   | VTOTAL(vtotal));
	dpu_fg_write(fg, VTCFG2, VSYNC(vsync) | VSBP(vsbp) | VSEN);

	kick_col = hact + 1;
	kick_row = vact;

	/* pkickconfig */
	dpu_fg_write(fg, PKICKCONFIG, COL(kick_col) | ROW(kick_row) | EN);

	/* skikconfig */
	dpu_fg_write(fg, SKICKCONFIG, COL(kick_col) | ROW(kick_row) | EN);

	/* primary and secondary area position configuration */
	dpu_fg_write(fg, PACFG, STARTX(0) | STARTY(0));
	dpu_fg_write(fg, SACFG, STARTX(0) | STARTY(0));

	/* alpha */
	dpu_fg_write_mask(fg, FGINCTRL,      ENPRIMALPHA | ENSECALPHA, 0);
	dpu_fg_write_mask(fg, FGINCTRLPANIC, ENPRIMALPHA | ENSECALPHA, 0);

	/* constant color is green(used in panic mode)  */
	dpu_fg_write(fg, FGCCR, CCGREEN(0x3ff));

	clk_set_parent(fg->clk_disp, fg->clk_pll);

	pclk_rate = m->clock * KHZ;

	/* find a minimal even divider for PLL */
	do {
		div += 2;
		pll_rate = pclk_rate * div;
	} while (pll_rate < MIN_PLL_RATE);

	clk_set_rate(fg->clk_pll, pll_rate);
	clk_set_rate(fg->clk_disp, pclk_rate);
}

void dpu_fg_displaymode(struct dpu_framegen *fg, enum dpu_fg_dm mode)
{
	dpu_fg_write_mask(fg, FGINCTRL, FGDM_MASK, mode);
}

void dpu_fg_panic_displaymode(struct dpu_framegen *fg, enum dpu_fg_dm mode)
{
	dpu_fg_write_mask(fg, FGINCTRLPANIC, FGDM_MASK, mode);
}

void dpu_fg_enable(struct dpu_framegen *fg)
{
	dpu_fg_write(fg, FGENABLE, FGEN);
}

void dpu_fg_disable(struct dpu_framegen *fg)
{
	dpu_fg_write(fg, FGENABLE, 0);
}

void dpu_fg_shdtokgen(struct dpu_framegen *fg)
{
	dpu_fg_write(fg, FGSLR, SHDTOKGEN);
}

u32 dpu_fg_get_frame_index(struct dpu_framegen *fg)
{
	u32 val = dpu_fg_read(fg, FGTIMESTAMP);

	return (val & FRAMEINDEX_MASK) >> FRAMEINDEX_SHIFT;
}

int dpu_fg_get_line_index(struct dpu_framegen *fg)
{
	u32 val = dpu_fg_read(fg, FGTIMESTAMP);

	return val & LINEINDEX_MASK;
}

int dpu_fg_wait_for_frame_counter_moving(struct dpu_framegen *fg)
{
	u32 frame_index, last_frame_index;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	frame_index = dpu_fg_get_frame_index(fg);
	do {
		last_frame_index = frame_index;
		frame_index = dpu_fg_get_frame_index(fg);
	} while (last_frame_index == frame_index &&
						time_before(jiffies, timeout));

	if (last_frame_index == frame_index) {
		dev_dbg(fg->dpu->dev,
			"failed to wait for FrameGen%d frame counter moving\n",
			fg->id);
		return -ETIMEDOUT;
	}

	dev_dbg(fg->dpu->dev,
		"FrameGen%d frame counter moves - last %u, curr %d\n",
					fg->id, last_frame_index, frame_index);
	return 0;
}

bool dpu_fg_secondary_requests_to_read_empty_fifo(struct dpu_framegen *fg)
{
	u32 val;

	val = dpu_fg_read(fg, FGCHSTAT);

	return !!(val & SFIFOEMPTY);
}

void dpu_fg_secondary_clear_channel_status(struct dpu_framegen *fg)
{
	dpu_fg_write(fg, FGCHSTATCLR, CLRSECSTAT);
}

int dpu_fg_wait_for_secondary_syncup(struct dpu_framegen *fg)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout(fg->base + FGCHSTAT, val,
				     val & SECSYNCSTAT, 5, 100000);
	if (ret) {
		dev_dbg(fg->dpu->dev,
			"failed to wait for FrameGen%u secondary syncup\n",
								fg->id);
		return -ETIMEDOUT;
	}

	dev_dbg(fg->dpu->dev, "FrameGen%u secondary syncup\n", fg->id);

	return 0;
}

void dpu_fg_enable_clock(struct dpu_framegen *fg)
{
	clk_prepare_enable(fg->clk_pll);
	clk_prepare_enable(fg->clk_disp);
}

void dpu_fg_disable_clock(struct dpu_framegen *fg)
{
	clk_disable_unprepare(fg->clk_disp);
	clk_disable_unprepare(fg->clk_pll);
}

struct dpu_framegen *dpu_fg_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_framegen *fg;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->fg_priv); i++) {
		fg = dpu->fg_priv[i];
		if (fg->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->fg_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&fg->mutex);

	if (fg->inuse) {
		mutex_unlock(&fg->mutex);
		return ERR_PTR(-EBUSY);
	}

	fg->inuse = true;

	mutex_unlock(&fg->mutex);

	return fg;
}

void dpu_fg_put(struct dpu_framegen *fg)
{
	if (IS_ERR_OR_NULL(fg))
		return;

	mutex_lock(&fg->mutex);

	fg->inuse = false;

	mutex_unlock(&fg->mutex);
}

void dpu_fg_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_framegen *fg = dpu->fg_priv[index];

	dpu_fg_enable_shden(fg);
	dpu_fg_syncmode(fg, FG_SYNCMODE_OFF);
}

int dpu_fg_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long unused, unsigned long base)
{
	struct dpu_framegen *fg;

	fg = devm_kzalloc(dpu->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;

	dpu->fg_priv[index] = fg;

	fg->base = devm_ioremap(dpu->dev, base, SZ_256);
	if (!fg->base)
		return -ENOMEM;

	fg->clk_pll = devm_clk_get(dpu->dev, id ? "pll1" : "pll0");
	if (IS_ERR(fg->clk_pll))
		return PTR_ERR(fg->clk_pll);

	fg->clk_disp = devm_clk_get(dpu->dev, id ? "disp1" : "disp0");
	if (IS_ERR(fg->clk_disp))
		return PTR_ERR(fg->clk_disp);

	fg->dpu = dpu;
	fg->id = id;
	fg->index = index;

	mutex_init(&fg->mutex);

	return 0;
}
