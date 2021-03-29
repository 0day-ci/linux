// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include <drm/drm_blend.h>

#include "dpu.h"
#include "dpu-prv.h"

#define PIXENGCFG_DYNAMIC			0x8
#define  PIXENGCFG_DYNAMIC_PRIM_SEL_MASK	0x3f
#define  PIXENGCFG_DYNAMIC_SEC_SEL_SHIFT	8
#define  PIXENGCFG_DYNAMIC_SEC_SEL_MASK		0x3f00

#define PIXENGCFG_STATUS			0xc

#define STATICCONTROL				0x8
#define  SHDTOKSEL_MASK				0x18
#define  SHDTOKSEL(n)				((n) << 3)
#define  SHDLDSEL_MASK				0x6
#define  SHDLDSEL(n)				((n) << 1)

#define CONTROL					0xc
#define  CTRL_MODE_MASK				BIT(0)

#define BLENDCONTROL				0x10
#define  ALPHA(a)				(((a) & 0xff) << 16)
#define  PRIM_C_BLD_FUNC__ONE_MINUS_CONST_ALPHA	0x7
#define  PRIM_C_BLD_FUNC__ONE_MINUS_SEC_ALPHA	0x5
#define  PRIM_C_BLD_FUNC__ZERO			0x0
#define  SEC_C_BLD_FUNC__CONST_ALPHA		(0x6 << 4)
#define  SEC_C_BLD_FUNC__SEC_ALPHA		(0x4 << 4)
#define  PRIM_A_BLD_FUNC__ZERO			(0x0 << 8)
#define  SEC_A_BLD_FUNC__ZERO			(0x0 << 12)

#define POSITION				0x14
#define  XPOS(x)				((x) & 0x7fff)
#define  YPOS(y)				(((y) & 0x7fff) << 16)

#define PRIMCONTROLWORD				0x18
#define SECCONTROLWORD				0x1c

enum dpu_lb_shadow_sel {
	PRIMARY,	/* background plane */
	SECONDARY,	/* foreground plane */
	BOTH,
};

struct dpu_layerblend {
	void __iomem *pec_base;
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	enum dpu_link_id link_id;
	bool inuse;
	struct dpu_soc *dpu;
};

static const enum dpu_link_id dpu_lb_link_id[] = {
	LINK_ID_LAYERBLEND0, LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND2, LINK_ID_LAYERBLEND3
};

static const enum dpu_link_id prim_sels[] = {
	/* common options */
	LINK_ID_NONE,
	LINK_ID_BLITBLEND9,
	LINK_ID_CONSTFRAME0,
	LINK_ID_CONSTFRAME1,
	LINK_ID_CONSTFRAME4,
	LINK_ID_CONSTFRAME5,
	LINK_ID_MATRIX4,
	LINK_ID_HSCALER4,
	LINK_ID_VSCALER4,
	LINK_ID_MATRIX5,
	LINK_ID_HSCALER5,
	LINK_ID_VSCALER5,
	/*
	 * special options:
	 * layerblend(n) has n special options,
	 * from layerblend0 to layerblend(n - 1), e.g.,
	 * layerblend3 has 3 special options -
	 * layerblend0/1/2.
	 */
	LINK_ID_LAYERBLEND0,
	LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND2,
	LINK_ID_LAYERBLEND3,
};

static const enum dpu_link_id sec_sels[] = {
	LINK_ID_NONE,
	LINK_ID_FETCHWARP2,
	LINK_ID_FETCHDECODE0,
	LINK_ID_FETCHDECODE1,
	LINK_ID_MATRIX4,
	LINK_ID_HSCALER4,
	LINK_ID_VSCALER4,
	LINK_ID_MATRIX5,
	LINK_ID_HSCALER5,
	LINK_ID_VSCALER5,
	LINK_ID_FETCHLAYER0,
};

static inline u32 dpu_pec_lb_read(struct dpu_layerblend *lb,
				  unsigned int offset)
{
	return readl(lb->pec_base + offset);
}

static inline void dpu_pec_lb_write(struct dpu_layerblend *lb,
				    unsigned int offset, u32 value)
{
	writel(value, lb->pec_base + offset);
}

static inline void dpu_pec_lb_write_mask(struct dpu_layerblend *lb,
					 unsigned int offset,
					 u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_pec_lb_read(lb, offset);
	tmp &= ~mask;
	dpu_pec_lb_write(lb, offset, tmp | value);
}

static inline u32 dpu_lb_read(struct dpu_layerblend *lb, unsigned int offset)
{
	return readl(lb->base + offset);
}

static inline void dpu_lb_write(struct dpu_layerblend *lb,
				unsigned int offset, u32 value)
{
	writel(value, lb->base + offset);
}

static inline void dpu_lb_write_mask(struct dpu_layerblend *lb,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_lb_read(lb, offset);
	tmp &= ~mask;
	dpu_lb_write(lb, offset, tmp | value);
}

enum dpu_link_id dpu_lb_get_link_id(struct dpu_layerblend *lb)
{
	return lb->link_id;
}

void dpu_lb_pec_dynamic_prim_sel(struct dpu_layerblend *lb,
				 enum dpu_link_id prim)
{
	struct dpu_soc *dpu = lb->dpu;
	int fixed_sels_num = ARRAY_SIZE(prim_sels) - 4;
	int i;

	for (i = 0; i < fixed_sels_num + lb->id; i++) {
		if (prim_sels[i] == prim) {
			dpu_pec_lb_write_mask(lb, PIXENGCFG_DYNAMIC,
					      PIXENGCFG_DYNAMIC_PRIM_SEL_MASK,
					      prim);
			return;
		}
	}

	dev_err(dpu->dev, "LayerBlend%u - invalid primary source 0x%02x\n",
								lb->id, prim);
}

void dpu_lb_pec_dynamic_sec_sel(struct dpu_layerblend *lb, enum dpu_link_id sec)
{
	struct dpu_soc *dpu = lb->dpu;
	int i;

	for (i = 0; i < ARRAY_SIZE(sec_sels); i++) {
		if (sec_sels[i] == sec) {
			dpu_pec_lb_write_mask(lb, PIXENGCFG_DYNAMIC,
					PIXENGCFG_DYNAMIC_SEC_SEL_MASK,
					sec << PIXENGCFG_DYNAMIC_SEC_SEL_SHIFT);
			return;
		}
	}

	dev_err(dpu->dev, "LayerBlend%u - invalid secondary source 0x%02x\n",
								lb->id, sec);
}

void dpu_lb_pec_clken(struct dpu_layerblend *lb, enum dpu_pec_clken clken)
{
	dpu_pec_lb_write_mask(lb, PIXENGCFG_DYNAMIC, CLKEN_MASK, CLKEN(clken));
}

static void dpu_lb_enable_shden(struct dpu_layerblend *lb)
{
	dpu_lb_write_mask(lb, STATICCONTROL, SHDEN, SHDEN);
}

static void dpu_lb_shdtoksel(struct dpu_layerblend *lb,
			     enum dpu_lb_shadow_sel sel)
{
	dpu_lb_write_mask(lb, STATICCONTROL, SHDTOKSEL_MASK, SHDTOKSEL(sel));
}

static void dpu_lb_shdldsel(struct dpu_layerblend *lb,
			    enum dpu_lb_shadow_sel sel)
{
	dpu_lb_write_mask(lb, STATICCONTROL, SHDLDSEL_MASK, SHDLDSEL(sel));
}

void dpu_lb_mode(struct dpu_layerblend *lb, enum dpu_lb_mode mode)
{
	dpu_lb_write_mask(lb, CONTROL, CTRL_MODE_MASK, mode);
}

void dpu_lb_blendcontrol(struct dpu_layerblend *lb, unsigned int zpos,
			 unsigned int pixel_blend_mode, u16 alpha)
{
	u32 val = PRIM_A_BLD_FUNC__ZERO | SEC_A_BLD_FUNC__ZERO;

	if (zpos == 0) {
		val |= PRIM_C_BLD_FUNC__ZERO | SEC_C_BLD_FUNC__CONST_ALPHA;
		alpha = DRM_BLEND_ALPHA_OPAQUE;
	} else {
		switch (pixel_blend_mode) {
		case DRM_MODE_BLEND_PIXEL_NONE:
			val |= PRIM_C_BLD_FUNC__ONE_MINUS_CONST_ALPHA |
			       SEC_C_BLD_FUNC__CONST_ALPHA;
			break;
		case DRM_MODE_BLEND_PREMULTI:
			val |= PRIM_C_BLD_FUNC__ONE_MINUS_SEC_ALPHA |
			       SEC_C_BLD_FUNC__CONST_ALPHA;
			break;
		case DRM_MODE_BLEND_COVERAGE:
			val |= PRIM_C_BLD_FUNC__ONE_MINUS_SEC_ALPHA |
			       SEC_C_BLD_FUNC__SEC_ALPHA;
			break;
		}
	}

	val |= ALPHA(alpha >> 8);

	dpu_lb_write(lb, BLENDCONTROL, val);
}

void dpu_lb_position(struct dpu_layerblend *lb, int x, int y)
{
	dpu_lb_write(lb, POSITION, XPOS(x) | YPOS(y));
}

unsigned int dpu_lb_get_id(struct dpu_layerblend *lb)
{
	return lb->id;
}

struct dpu_layerblend *dpu_lb_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_layerblend *lb;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->lb_priv); i++) {
		lb = dpu->lb_priv[i];
		if (lb->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->lb_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&lb->mutex);

	if (lb->inuse) {
		mutex_unlock(&lb->mutex);
		return ERR_PTR(-EBUSY);
	}

	lb->inuse = true;

	mutex_unlock(&lb->mutex);

	return lb;
}

void dpu_lb_put(struct dpu_layerblend *lb)
{
	if (IS_ERR_OR_NULL(lb))
		return;

	mutex_lock(&lb->mutex);

	lb->inuse = false;

	mutex_unlock(&lb->mutex);
}

void dpu_lb_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_layerblend *lb = dpu->lb_priv[index];

	dpu_lb_pec_dynamic_prim_sel(lb, LINK_ID_NONE);
	dpu_lb_pec_dynamic_sec_sel(lb, LINK_ID_NONE);
	dpu_lb_pec_clken(lb, CLKEN_DISABLE);
	dpu_lb_shdldsel(lb, BOTH);
	dpu_lb_shdtoksel(lb, BOTH);
	dpu_lb_enable_shden(lb);
}

int dpu_lb_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_layerblend *lb;

	lb = devm_kzalloc(dpu->dev, sizeof(*lb), GFP_KERNEL);
	if (!lb)
		return -ENOMEM;

	dpu->lb_priv[index] = lb;

	lb->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!lb->pec_base)
		return -ENOMEM;

	lb->base = devm_ioremap(dpu->dev, base, SZ_32);
	if (!lb->base)
		return -ENOMEM;

	lb->dpu = dpu;
	lb->id = id;
	lb->index = index;
	lb->link_id = dpu_lb_link_id[index];

	mutex_init(&lb->mutex);

	return 0;
}
