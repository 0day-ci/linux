/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifndef __LSDC_DRV_H__
#define __LSDC_DRV_H__

#include <drm/drm_print.h>
#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_encoder.h>
#include <drm/drm_drv.h>
#include <drm/drm_atomic.h>

#include "lsdc_regs.h"
#include "lsdc_pll.h"

#define LSDC_NUM_CRTC           2

/* There is only a 1:1 mapping of encoders and connectors for lsdc,
 * Each CRTC have two FB address registers.
 *
 * The display controller in LS2K1000/LS2K0500.
 *       ___________________                                     _________
 *      |            -------|                                   |         |
 *      |  CRTC0 --> | DVO0 ----> Encoder0 ---> Connector0 ---> | Monitor |
 *      |  _   _     -------|        ^              ^           |_________|
 *      | | | | |           |        |              |
 *      | |_| |_|           |     +------+          |
 *      |                   <---->| i2c0 |<---------+
 *      |          LSDC     |     +------+
 *      |  _   _            |     +------+
 *      | | | | |           <---->| i2c1 |----------+
 *      | |_| |_|           |     +------+          |            _________
 *      |            -------|        |              |           |         |
 *      |  CRTC1 --> | DVO1 ----> Encoder1 ---> Connector1 ---> |  Panel  |
 *      |            -------|                                   |_________|
 *      |___________________|
 *
 *
 * The display controller in LS7A1000.
 *       ___________________                                     _________
 *      |            -------|                                   |         |
 *      |  CRTC0 --> | DVO0 ----> Encoder0 ---> Connector0 ---> | Monitor |
 *      |  _   _     -------|        ^             ^            |_________|
 *      | | | | |    -------|        |             |
 *      | |_| |_|    | i2c0 <--------+-------------+
 *      |            -------|
 *      |  _   _     -------|
 *      | | | | |    | i2c1 <--------+-------------+
 *      | |_| |_|    -------|        |             |             _________
 *      |            -------|        |             |            |         |
 *      |  CRTC1 --> | DVO1 ----> Encoder1 ---> Connector1 ---> |  Panel  |
 *      |            -------|                                   |_________|
 *      |___________________|
 */

enum loongson_dc_family {
	LSDC_CHIP_UNKNOWN = 0,
	LSDC_CHIP_2K1000 = 1,  /* 2-Core Mips64r2 SoC, 64-bit */
	LSDC_CHIP_7A1000 = 2,  /* North bridge of LS3A3000/LS3A4000/LS3A5000 */
	LSDC_CHIP_2K0500 = 3,  /* Reduced version of LS2K1000, single core */
	LSDC_CHIP_LAST,
};

enum lsdc_pixel_format {
	LSDC_PF_NONE = 0,
	LSDC_PF_ARGB4444 = 1,  /* ARGB A:4 bits R/G/B: 4 bits each [16 bits] */
	LSDC_PF_ARGB1555 = 2,  /* ARGB A:1 bit RGB:15 bits [16 bits] */
	LSDC_PF_RGB565 = 3,    /* RGB [16 bits] */
	LSDC_PF_XRGB8888 = 4,  /* XRGB [32 bits] */
};

struct lsdc_chip_desc {
	enum loongson_dc_family chip;
	u32 num_of_crtc;
	u32 max_pixel_clk;
	u32 max_width;
	u32 max_height;
	u32 num_of_hw_cursor;
	u32 hw_cursor_w;
	u32 hw_cursor_h;
	u32 stride_alignment;
	bool have_builtin_i2c;
	bool has_vram;
};

/*
 * struct lsdc_display_pipe - Abstraction of hardware display pipeline.
 * @crtc: CRTC control structure
 * @plane: Plane control structure
 * @encoder: Encoder control structure
 * @pixpll: Pll control structure
 * @connector: point to connector control structure this display pipe bind
 * @index: the index corresponding to the hardware display pipe
 * @available: is this display pipe is available on the motherboard, The
 *  downstream mother board manufacturer may use only one of them.
 *  For example, LEMOTE LX-6901 board just has only one VGA output.
 *
 * Display pipeline with plane, crtc, encoder, PLL collapsed into one entity.
 */
struct lsdc_display_pipe {
	struct drm_crtc crtc;
	struct drm_plane primary;
	struct drm_plane cursor;
	struct drm_encoder encoder;
	struct lsdc_pll pixpll;
	struct lsdc_connector *lconn;

	int index;
	bool available;
};

struct lsdc_crtc_state {
	struct drm_crtc_state base;
	struct lsdc_pll_core_values pparams;
};

struct lsdc_device {
	struct drm_device drm;

	/* LS7A1000 has a dediacted video RAM, typically 64 MB or more */
	void __iomem *reg_base;
	void __iomem *vram;
	resource_size_t vram_base;
	resource_size_t vram_size;

	struct lsdc_display_pipe disp_pipe[LSDC_NUM_CRTC];

	/*
	 * @num_output: count the number of active display pipe.
	 */
	unsigned int num_output;

	/* @desc: device dependent data and feature descriptions */
	const struct lsdc_chip_desc *desc;

	/* @reglock: protects concurrent register access */
	spinlock_t reglock;

	/*
	 * @use_vram_helper: using vram helper base solution instead of
	 * CMA helper based solution. The DC scanout from the VRAM is
	 * proved to be more reliable, but graphic application is may
	 * become slow when using this driver mode.
	 */
	bool use_vram_helper;

	int irq;
	u32 irq_status;
};

#define to_lsdc(x) container_of(x, struct lsdc_device, drm)

static inline struct lsdc_crtc_state *
to_lsdc_crtc_state(struct drm_crtc_state *base)
{
	return container_of(base, struct lsdc_crtc_state, base);
}

static inline u32 lsdc_reg_read32(struct lsdc_device * const ldev, u32 offset)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&ldev->reglock, flags);
	val = readl(ldev->reg_base + offset);
	spin_unlock_irqrestore(&ldev->reglock, flags);

	return val;
}

static inline void
lsdc_reg_write32(struct lsdc_device * const ldev, u32 offset, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&ldev->reglock, flags);
	writel(val, ldev->reg_base + offset);
	spin_unlock_irqrestore(&ldev->reglock, flags);
}

int lsdc_crtc_init(struct drm_device *ddev,
		   struct drm_crtc *crtc,
		   unsigned int index,
		   struct drm_plane *primary,
		   struct drm_plane *cursor);

int lsdc_plane_init(struct lsdc_device *ldev, struct drm_plane *plane,
		    enum drm_plane_type type, unsigned int index);

int lsdc_encoder_init(struct drm_encoder * const encoder,
		      struct drm_connector *connector,
		      struct drm_device *ddev,
		      unsigned int index,
		      unsigned int total);

#endif
