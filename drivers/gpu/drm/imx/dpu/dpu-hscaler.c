// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-prv.h"

#define PIXENGCFG_DYNAMIC		0x8
#define  PIXENGCFG_DYNAMIC_SRC_SEL_MASK	0x3f

#define STATICCONTROL			0x8
#define SETUP1				0xc
#define SETUP2				0x10

#define CONTROL				0x14
#define  CTRL_MODE_MASK			BIT(0)

struct dpu_hscaler {
	void __iomem *pec_base;
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	enum dpu_link_id link_id;
	bool inuse;
	struct dpu_soc *dpu;
};

static const enum dpu_link_id dpu_hs_link_id[] = {
	LINK_ID_HSCALER4, LINK_ID_HSCALER5, LINK_ID_HSCALER9
};

static const enum dpu_link_id src_sels[3][4] = {
	{
		LINK_ID_NONE,
		LINK_ID_FETCHDECODE0,
		LINK_ID_MATRIX4,
		LINK_ID_VSCALER4,
	}, {
		LINK_ID_NONE,
		LINK_ID_FETCHDECODE1,
		LINK_ID_MATRIX5,
		LINK_ID_VSCALER5,
	}, {
		LINK_ID_NONE,
		LINK_ID_MATRIX9,
		LINK_ID_VSCALER9,
		LINK_ID_FILTER9,
	},
};

static inline u32 dpu_pec_hs_read(struct dpu_hscaler *hs,
				  unsigned int offset)
{
	return readl(hs->pec_base + offset);
}

static inline void dpu_pec_hs_write(struct dpu_hscaler *hs,
				    unsigned int offset, u32 value)
{
	writel(value, hs->pec_base + offset);
}

static inline void dpu_pec_hs_write_mask(struct dpu_hscaler *hs,
					 unsigned int offset,
					 u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_pec_hs_read(hs, offset);
	tmp &= ~mask;
	dpu_pec_hs_write(hs, offset, tmp | value);
}

static inline u32 dpu_hs_read(struct dpu_hscaler *hs, unsigned int offset)
{
	return readl(hs->base + offset);
}

static inline void dpu_hs_write(struct dpu_hscaler *hs,
				unsigned int offset, u32 value)
{
	writel(value, hs->base + offset);
}

static inline void dpu_hs_write_mask(struct dpu_hscaler *hs,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_hs_read(hs, offset);
	tmp &= ~mask;
	dpu_hs_write(hs, offset, tmp | value);
}

enum dpu_link_id dpu_hs_get_link_id(struct dpu_hscaler *hs)
{
	return hs->link_id;
}

void dpu_hs_pec_dynamic_src_sel(struct dpu_hscaler *hs, enum dpu_link_id src)
{
	struct dpu_soc *dpu = hs->dpu;
	int i;

	for (i = 0; i < ARRAY_SIZE(src_sels[hs->index]); i++) {
		if (src_sels[hs->index][i] == src) {
			dpu_pec_hs_write_mask(hs, PIXENGCFG_DYNAMIC,
					      PIXENGCFG_DYNAMIC_SRC_SEL_MASK,
					      src);
			return;
		}
	}

	dev_err(dpu->dev, "HScaler%u - invalid source 0x%02x\n", hs->id, src);
}

void dpu_hs_pec_clken(struct dpu_hscaler *hs, enum dpu_pec_clken clken)
{
	dpu_pec_hs_write_mask(hs, PIXENGCFG_DYNAMIC, CLKEN_MASK, CLKEN(clken));
}

static void dpu_hs_enable_shden(struct dpu_hscaler *hs)
{
	dpu_hs_write_mask(hs, STATICCONTROL, SHDEN, SHDEN);
}

void dpu_hs_setup1(struct dpu_hscaler *hs,
		   unsigned int src_w, unsigned int dst_w)
{
	struct dpu_soc *dpu = hs->dpu;
	u32 scale_factor;
	u64 tmp64;

	if (src_w == dst_w) {
		scale_factor = 0x80000;
	} else {
		if (src_w > dst_w) {
			tmp64 = (u64)((u64)dst_w * 0x80000);
			do_div(tmp64, src_w);

		} else {
			tmp64 = (u64)((u64)src_w * 0x80000);
			do_div(tmp64, dst_w);
		}
		scale_factor = (u32)tmp64;
	}

	if (scale_factor > 0x80000) {
		dev_err(dpu->dev, "HScaler%u - invalid scale factor 0x%08x\n",
							hs->id, scale_factor);
		return;
	}

	dpu_hs_write(hs, SETUP1, SCALE_FACTOR(scale_factor));

	dev_dbg(dpu->dev, "HScaler%u - scale factor 0x%08x\n",
							hs->id, scale_factor);
}

void dpu_hs_setup2(struct dpu_hscaler *hs, u32 phase_offset)
{
	dpu_hs_write(hs, SETUP2, PHASE_OFFSET(phase_offset));
}

void dpu_hs_output_size(struct dpu_hscaler *hs, u32 line_num)
{
	dpu_hs_write_mask(hs, CONTROL, OUTPUT_SIZE_MASK, OUTPUT_SIZE(line_num));
}

void dpu_hs_filter_mode(struct dpu_hscaler *hs, enum dpu_scaler_filter_mode m)
{
	dpu_hs_write_mask(hs, CONTROL, FILTER_MODE_MASK, FILTER_MODE(m));
}

void dpu_hs_scale_mode(struct dpu_hscaler *hs, enum dpu_scaler_scale_mode m)
{
	dpu_hs_write_mask(hs, CONTROL, SCALE_MODE_MASK, SCALE_MODE(m));
}

void dpu_hs_mode(struct dpu_hscaler *hs, enum dpu_scaler_mode m)
{
	dpu_hs_write_mask(hs, CONTROL, CTRL_MODE_MASK, m);
}

unsigned int dpu_hs_get_id(struct dpu_hscaler *hs)
{
	return hs->id;
}

struct dpu_hscaler *dpu_hs_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_hscaler *hs;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->hs_priv); i++) {
		hs = dpu->hs_priv[i];
		if (hs->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->hs_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&hs->mutex);

	if (hs->inuse) {
		mutex_unlock(&hs->mutex);
		return ERR_PTR(-EBUSY);
	}

	hs->inuse = true;

	mutex_unlock(&hs->mutex);

	return hs;
}

void dpu_hs_put(struct dpu_hscaler *hs)
{
	if (IS_ERR_OR_NULL(hs))
		return;

	mutex_lock(&hs->mutex);

	hs->inuse = false;

	mutex_unlock(&hs->mutex);
}

void dpu_hs_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_hscaler *hs = dpu->hs_priv[index];

	dpu_hs_enable_shden(hs);
	dpu_hs_setup2(hs, 0);
	dpu_hs_pec_dynamic_src_sel(hs, LINK_ID_NONE);
}

int dpu_hs_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_hscaler *hs;

	hs = devm_kzalloc(dpu->dev, sizeof(*hs), GFP_KERNEL);
	if (!hs)
		return -ENOMEM;

	dpu->hs_priv[index] = hs;

	hs->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!hs->pec_base)
		return -ENOMEM;

	hs->base = devm_ioremap(dpu->dev, base, SZ_32);
	if (!hs->base)
		return -ENOMEM;

	hs->dpu = dpu;
	hs->id = id;
	hs->index = index;
	hs->link_id = dpu_hs_link_id[index];

	mutex_init(&hs->mutex);

	return 0;
}
