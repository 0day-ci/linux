// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-prv.h"

#define PIXENGCFG_STATIC	0x8
#define  POWERDOWN		BIT(4)
#define  SYNC_MODE		BIT(8)
#define  AUTO			BIT(8)
#define  SINGLE			0
#define  DIV_MASK		0xff0000
#define  DIV(n)			(((n) & 0xff) << 16)
#define  DIV_RESET		0x80

#define PIXENGCFG_DYNAMIC	0xc

#define PIXENGCFG_REQUEST	0x10

#define PIXENGCFG_TRIGGER	0x14
#define  SYNC_TRIGGER		BIT(0)

#define STATICCONTROL		0x8
#define  KICK_MODE		BIT(8)
#define  EXTERNAL		BIT(8)
#define  SOFTWARE		0
#define  PERFCOUNTMODE		BIT(12)

#define CONTROL			0xc
#define  GAMMAAPPLYENABLE	BIT(0)

#define SOFTWAREKICK		0x10
#define  KICK			BIT(0)

#define STATUS			0x14
#define  CNT_ERR_STS		BIT(0)

#define CONTROLWORD		0x18
#define CURPIXELCNT		0x1c
#define LASTPIXELCNT		0x20
#define PERFCOUNTER		0x24

struct dpu_extdst {
	void __iomem *pec_base;
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	bool inuse;
	struct dpu_soc *dpu;
};

static const enum dpu_link_id src_sels[] = {
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
	LINK_ID_LAYERBLEND3,
	LINK_ID_LAYERBLEND2,
	LINK_ID_LAYERBLEND1,
	LINK_ID_LAYERBLEND0,
};

static inline u32 dpu_pec_ed_read(struct dpu_extdst *ed, unsigned int offset)
{
	return readl(ed->pec_base + offset);
}

static inline void dpu_pec_ed_write(struct dpu_extdst *ed,
				    unsigned int offset, u32 value)
{
	writel(value, ed->pec_base + offset);
}

static inline void dpu_pec_ed_write_mask(struct dpu_extdst *ed,
					 unsigned int offset,
					 u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_pec_ed_read(ed, offset);
	tmp &= ~mask;
	dpu_pec_ed_write(ed, offset, tmp | value);
}

static inline u32 dpu_ed_read(struct dpu_extdst *ed, unsigned int offset)
{
	return readl(ed->base + offset);
}

static inline void dpu_ed_write(struct dpu_extdst *ed,
				unsigned int offset, u32 value)
{
	writel(value, ed->base + offset);
}

static inline void dpu_ed_write_mask(struct dpu_extdst *ed, unsigned int offset,
				     u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_ed_read(ed, offset);
	tmp &= ~mask;
	dpu_ed_write(ed, offset, tmp | value);
}

static inline bool dpu_ed_is_safety_stream(struct dpu_extdst *ed)
{
	if ((ed->id == DPU_SAFETY_STREAM_OFFSET) ||
	    (ed->id == DPU_SAFETY_STREAM_OFFSET + 1))
		return true;

	return false;
}

static void dpu_ed_pec_enable_shden(struct dpu_extdst *ed)
{
	dpu_pec_ed_write_mask(ed, PIXENGCFG_STATIC, SHDEN, SHDEN);
}

void dpu_ed_pec_poweron(struct dpu_extdst *ed)
{
	dpu_pec_ed_write_mask(ed, PIXENGCFG_STATIC, POWERDOWN, 0);
}

static void dpu_ed_pec_sync_mode_single(struct dpu_extdst *ed)
{
	dpu_pec_ed_write_mask(ed, PIXENGCFG_STATIC, SYNC_MODE, SINGLE);
}

static void dpu_ed_pec_div_reset(struct dpu_extdst *ed)
{
	dpu_pec_ed_write_mask(ed, PIXENGCFG_STATIC, DIV_MASK, DIV(DIV_RESET));
}

void dpu_ed_pec_src_sel(struct dpu_extdst *ed, enum dpu_link_id src)
{
	struct dpu_soc *dpu = ed->dpu;
	int i;

	for (i = 0; i < ARRAY_SIZE(src_sels); i++) {
		if (src_sels[i] == src) {
			dpu_pec_ed_write(ed, PIXENGCFG_DYNAMIC, src);
			return;
		}
	}

	dev_err(dpu->dev, "invalid source(0x%02x) for ExtDst%u\n", src, ed->id);
}

void dpu_ed_pec_sync_trigger(struct dpu_extdst *ed)
{
	dpu_pec_ed_write(ed, PIXENGCFG_TRIGGER, SYNC_TRIGGER);
}

static void dpu_ed_enable_shden(struct dpu_extdst *ed)
{
	dpu_ed_write_mask(ed, STATICCONTROL, SHDEN, SHDEN);
}

static void dpu_ed_kick_mode_external(struct dpu_extdst *ed)
{
	dpu_ed_write_mask(ed, STATICCONTROL, KICK_MODE, EXTERNAL);
}

static void dpu_ed_disable_perfcountmode(struct dpu_extdst *ed)
{
	dpu_ed_write_mask(ed, STATICCONTROL, PERFCOUNTMODE, 0);
}

static void dpu_ed_disable_gamma_apply(struct dpu_extdst *ed)
{
	dpu_ed_write_mask(ed, CONTROL, GAMMAAPPLYENABLE, 0);
}

static struct dpu_extdst *dpu_ed_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_extdst *ed;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->ed_priv); i++) {
		ed = dpu->ed_priv[i];
		if (ed->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->ed_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&ed->mutex);

	if (ed->inuse) {
		mutex_unlock(&ed->mutex);
		return ERR_PTR(-EBUSY);
	}

	ed->inuse = true;

	mutex_unlock(&ed->mutex);

	return ed;
}

static void dpu_ed_put(struct dpu_extdst *ed)
{
	if (IS_ERR_OR_NULL(ed))
		return;

	mutex_lock(&ed->mutex);

	ed->inuse = false;

	mutex_unlock(&ed->mutex);
}

/* ExtDst for safety stream */
struct dpu_extdst *dpu_ed_safe_get(struct dpu_soc *dpu,
				   unsigned int stream_id)
{
	return dpu_ed_get(dpu, stream_id + DPU_SAFETY_STREAM_OFFSET);
}

void dpu_ed_safe_put(struct dpu_extdst *ed)
{
	return dpu_ed_put(ed);
}

/* ExtDst for content stream */
struct dpu_extdst *dpu_ed_cont_get(struct dpu_soc *dpu,
				   unsigned int stream_id)
{
	return dpu_ed_get(dpu, stream_id);
}

void dpu_ed_cont_put(struct dpu_extdst *ed)
{
	return dpu_ed_put(ed);
}

void dpu_ed_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_extdst *ed = dpu->ed_priv[index];

	dpu_ed_pec_src_sel(ed, LINK_ID_NONE);
	dpu_ed_pec_enable_shden(ed);
	dpu_ed_pec_poweron(ed);
	dpu_ed_pec_sync_mode_single(ed);
	dpu_ed_pec_div_reset(ed);
	dpu_ed_enable_shden(ed);
	dpu_ed_disable_perfcountmode(ed);
	dpu_ed_kick_mode_external(ed);
	dpu_ed_disable_gamma_apply(ed);
}

int dpu_ed_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_extdst *ed;

	ed = devm_kzalloc(dpu->dev, sizeof(*ed), GFP_KERNEL);
	if (!ed)
		return -ENOMEM;

	dpu->ed_priv[index] = ed;

	ed->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_32);
	if (!ed->pec_base)
		return -ENOMEM;

	ed->base = devm_ioremap(dpu->dev, base, SZ_128);
	if (!ed->base)
		return -ENOMEM;

	ed->dpu = dpu;
	ed->id = id;
	ed->index = index;

	mutex_init(&ed->mutex);

	return 0;
}
