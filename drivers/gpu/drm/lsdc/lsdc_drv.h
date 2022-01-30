/* SPDX-License-Identifier: GPL-2.0+ */
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

#ifndef __LSDC_DRV_H__
#define __LSDC_DRV_H__

#include <drm/drm_device.h>
#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_encoder.h>

#ifdef CONFIG_DRM_LSDC_VRAM_DRIVER
#include <drm/drm_gem_vram_helper.h>
#endif

#include "lsdc_regs.h"
#include "lsdc_pll.h"

#ifndef PCI_DEVICE_ID_GPU
#define PCI_DEVICE_ID_GPU		0x7a15
#endif


#define LSDC_MAX_CRTC           2

/* There is only a 1:1 mapping of encoders and connectors for lsdc */
/*
 *      +-------------------+                                      _________
 *      |                   |                                     |         |
 *      |  CRTC0 --> DVO0 ---------> Encoder0 --> Connector0 ---> | Monotor |
 *      |                   |           ^            ^            |_________|
 *      |                   |           |            |
 *      |                <----- i2c0 ----------------+
 *      |   LSDC IP CORE    |
 *      |                <----- i2c1 ----------------+
 *      |                   |           |            |             _________
 *      |                   |           |            |            |         |
 *      |  CRTC1 --> DVO1 ---------> Encoder1 --> Connector1 ---> |  Panel  |
 *      |                   |                                     |_________|
 *      +-------------------+
 */

enum loongson_dc_family {
	LSDC_CHIP_UNKNOWN = 0,
	LSDC_CHIP_2K1000 = 1, /* 2-Core SoC, 64-bit */
	LSDC_CHIP_7A1000 = 2, /* North bridge of LS3A3000/LS3A4000/LS3A5000 */
	LSDC_CHIP_2K0500 = 3, /* Reduced version of LS2K1000, single core   */
	LSDC_CHIP_7A2000 = 4, /* Newer version of 7A1000 */
	LSDC_CHIP_LAST,
};

enum lsdc_pixel_format {
	LSDC_PF_NONE = 0,
	LSDC_PF_ARGB4444 = 1,    /* ARGB A:4 bits R/G/B: 4 bits each [16 bits] */
	LSDC_PF_ARGB1555 = 2,    /* ARGB A:1 bit RGB:15 bits [16 bits] */
	LSDC_PF_RGB565 = 3,      /* RGB [16 bits] */
	LSDC_PF_XRGB8888 = 4,    /* XRGB [32 bits] */
};

struct lsdc_chip_desc {
	enum loongson_dc_family chip;
	uint32_t num_of_crtc;

	uint32_t max_pixel_clk;

	uint32_t max_width;
	uint32_t max_height;

	uint32_t num_of_hw_cursor;
	uint32_t hw_cursor_w;
	uint32_t hw_cursor_h;
	bool have_builtin_i2c;
};


/**
 * struct lsdc_display_pipe - simple display pipeline
 * @crtc: CRTC control structure
 * @plane: Plane control structure
 * @encoder: Encoder control structure
 * @pixpll: Pll control structure
 * @connector: point to connector control structure
 *
 * display pipeline with plane, crtc, encoder, pll collapsed into one
 * entity. It should be initialized by calling drm_simple_display_pipe_init().
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
	unsigned int pix_fmt;
};


struct lsdc_device {
	struct drm_device drm;

	void __iomem *reg_base;
	void __iomem *vram;
	resource_size_t vram_base;
	resource_size_t vram_size;

	struct lsdc_display_pipe disp_pipe[LSDC_MAX_CRTC];

	unsigned int num_output;

	/* platform specific data */
	const struct lsdc_chip_desc *desc;

	/* @reglock: protects concurrent register access */
	spinlock_t reglock;

	/*
	 * @dirty_update: true if manual dirty update is wantted
	 */
	bool dirty_update;
	/*
	 * @cached_coherent: true if the host platform is hardware maintained
	 * cached coherent.
	 */
	bool cached_coherent;
	/*
	 * @use_vram_helper: using vram helper instead of cma helper base
	 * solution. As ls7a1000 has a dediacted video ram, the dc scanout
	 * from the vram is more reliable.
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

static inline void lsdc_reg_write32(struct lsdc_device * const ldev, u32 offset, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&ldev->reglock, flags);
	writel(val, ldev->reg_base + offset);
	spin_unlock_irqrestore(&ldev->reglock, flags);
}

int lsdc_crtc_init(struct drm_device *ddev, struct drm_crtc *crtc,
		   unsigned int index, struct drm_plane *primary,
		   struct drm_plane *cursor);

int lsdc_plane_init(struct lsdc_device *ldev, struct drm_plane *plane,
		    enum drm_plane_type type, unsigned int index);

int lsdc_encoder_init(struct drm_encoder * const encoder,
		      struct drm_connector *connector,
		      struct drm_device *ddev,
		      unsigned int index,
		      unsigned int total);

#endif
