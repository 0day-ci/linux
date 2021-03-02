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

#define SETUP(n)			(0xc + ((n) - 1) * 0x4)

#define CONTROL				0x20
#define  FIELD_MODE_MASK		0x3000
#define  FIELD_MODE(n)			((n) << 12)
#define  CTRL_MODE_MASK			BIT(0)

struct dpu_vscaler {
	void __iomem *pec_base;
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	enum dpu_link_id link_id;
	bool inuse;
	struct dpu_soc *dpu;
};

static const enum dpu_link_id dpu_vs_link_id[] = {
	LINK_ID_VSCALER4, LINK_ID_VSCALER5, LINK_ID_VSCALER9
};

static const enum dpu_link_id src_sels[3][4] = {
	{
		LINK_ID_NONE,
		LINK_ID_FETCHDECODE0,
		LINK_ID_MATRIX4,
		LINK_ID_HSCALER4,
	}, {
		LINK_ID_NONE,
		LINK_ID_FETCHDECODE1,
		LINK_ID_MATRIX5,
		LINK_ID_HSCALER5,
	}, {
		LINK_ID_NONE,
		LINK_ID_MATRIX9,
		LINK_ID_HSCALER9,
	},
};

static inline u32 dpu_pec_vs_read(struct dpu_vscaler *vs,
				  unsigned int offset)
{
	return readl(vs->pec_base + offset);
}

static inline void dpu_pec_vs_write(struct dpu_vscaler *vs,
				    unsigned int offset, u32 value)
{
	writel(value, vs->pec_base + offset);
}

static inline void dpu_pec_vs_write_mask(struct dpu_vscaler *vs,
					 unsigned int offset,
					 u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_pec_vs_read(vs, offset);
	tmp &= ~mask;
	dpu_pec_vs_write(vs, offset, tmp | value);
}

static inline u32 dpu_vs_read(struct dpu_vscaler *vs, unsigned int offset)
{
	return readl(vs->base + offset);
}

static inline void dpu_vs_write(struct dpu_vscaler *vs,
				unsigned int offset, u32 value)
{
	writel(value, vs->base + offset);
}

static inline void dpu_vs_write_mask(struct dpu_vscaler *vs,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_vs_read(vs, offset);
	tmp &= ~mask;
	dpu_vs_write(vs, offset, tmp | value);
}

enum dpu_link_id dpu_vs_get_link_id(struct dpu_vscaler *vs)
{
	return vs->link_id;
}

void dpu_vs_pec_dynamic_src_sel(struct dpu_vscaler *vs, enum dpu_link_id src)
{
	struct dpu_soc *dpu = vs->dpu;
	int i;

	for (i = 0; i < ARRAY_SIZE(src_sels[vs->index]); i++) {
		if (src_sels[vs->index][i] == src) {
			dpu_pec_vs_write_mask(vs, PIXENGCFG_DYNAMIC,
					      PIXENGCFG_DYNAMIC_SRC_SEL_MASK,
					      src);
			return;
		}
	}

	dev_err(dpu->dev, "VScaler%u - invalid source 0x%02x\n", vs->id, src);
}

void dpu_vs_pec_clken(struct dpu_vscaler *vs, enum dpu_pec_clken clken)
{
	dpu_pec_vs_write_mask(vs, PIXENGCFG_DYNAMIC, CLKEN_MASK, CLKEN(clken));
}

static void dpu_vs_enable_shden(struct dpu_vscaler *vs)
{
	dpu_vs_write_mask(vs, STATICCONTROL, SHDEN, SHDEN);
}

void dpu_vs_setup1(struct dpu_vscaler *vs,
		   unsigned int src_w, unsigned int dst_w, bool deinterlace)
{
	struct dpu_soc *dpu = vs->dpu;
	u32 scale_factor;
	u64 tmp64;

	if (deinterlace)
		dst_w *= 2;

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
		dev_err(dpu->dev, "VScaler%u - invalid scale factor 0x%08x\n",
							vs->id, scale_factor);
		return;
	}

	dpu_vs_write(vs, SETUP(1), SCALE_FACTOR(scale_factor));

	dev_dbg(dpu->dev, "VScaler%u - scale factor 0x%08x\n",
							vs->id, scale_factor);
}

void dpu_vs_setup2(struct dpu_vscaler *vs, bool deinterlace)
{
	/* 0x20000: +0.25 phase offset for deinterlace */
	u32 phase_offset = deinterlace ? 0x20000 : 0;

	dpu_vs_write(vs, SETUP(2), PHASE_OFFSET(phase_offset));
}

void dpu_vs_setup3(struct dpu_vscaler *vs, bool deinterlace)
{
	/* 0x1e0000: -0.25 phase offset for deinterlace */
	u32 phase_offset = deinterlace ? 0x1e0000 : 0;

	dpu_vs_write(vs, SETUP(3), PHASE_OFFSET(phase_offset));
}

void dpu_vs_setup4(struct dpu_vscaler *vs, u32 phase_offset)
{
	dpu_vs_write(vs, SETUP(4), PHASE_OFFSET(phase_offset));
}

void dpu_vs_setup5(struct dpu_vscaler *vs, u32 phase_offset)
{
	dpu_vs_write(vs, SETUP(5), PHASE_OFFSET(phase_offset));
}

void dpu_vs_output_size(struct dpu_vscaler *vs, u32 line_num)
{
	dpu_vs_write_mask(vs, CONTROL, OUTPUT_SIZE_MASK, OUTPUT_SIZE(line_num));
}

void dpu_vs_field_mode(struct dpu_vscaler *vs, enum dpu_scaler_field_mode m)
{
	dpu_vs_write_mask(vs, CONTROL, FIELD_MODE_MASK, FIELD_MODE(m));
}

void dpu_vs_filter_mode(struct dpu_vscaler *vs, enum dpu_scaler_filter_mode m)
{
	dpu_vs_write_mask(vs, CONTROL, FILTER_MODE_MASK, FILTER_MODE(m));
}

void dpu_vs_scale_mode(struct dpu_vscaler *vs, enum dpu_scaler_scale_mode m)
{
	dpu_vs_write_mask(vs, CONTROL, SCALE_MODE_MASK, SCALE_MODE(m));
}

void dpu_vs_mode(struct dpu_vscaler *vs, enum dpu_scaler_mode m)
{
	dpu_vs_write_mask(vs, CONTROL, CTRL_MODE_MASK, m);
}

unsigned int dpu_vs_get_id(struct dpu_vscaler *vs)
{
	return vs->id;
}

struct dpu_vscaler *dpu_vs_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_vscaler *vs;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->vs_priv); i++) {
		vs = dpu->vs_priv[i];
		if (vs->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->vs_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&vs->mutex);

	if (vs->inuse) {
		mutex_unlock(&vs->mutex);
		return ERR_PTR(-EBUSY);
	}

	vs->inuse = true;

	mutex_unlock(&vs->mutex);

	return vs;
}

void dpu_vs_put(struct dpu_vscaler *vs)
{
	if (IS_ERR_OR_NULL(vs))
		return;

	mutex_lock(&vs->mutex);

	vs->inuse = false;

	mutex_unlock(&vs->mutex);
}

void dpu_vs_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_vscaler *vs = dpu->vs_priv[index];

	dpu_vs_enable_shden(vs);
	dpu_vs_setup2(vs, false);
	dpu_vs_setup3(vs, false);
	dpu_vs_setup4(vs, 0);
	dpu_vs_setup5(vs, 0);
	dpu_vs_pec_dynamic_src_sel(vs, LINK_ID_NONE);
}

int dpu_vs_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_vscaler *vs;

	vs = devm_kzalloc(dpu->dev, sizeof(*vs), GFP_KERNEL);
	if (!vs)
		return -ENOMEM;

	dpu->vs_priv[index] = vs;

	vs->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!vs->pec_base)
		return -ENOMEM;

	vs->base = devm_ioremap(dpu->dev, base, SZ_32);
	if (!vs->base)
		return -ENOMEM;

	vs->dpu = dpu;
	vs->id = id;
	vs->index = index;
	vs->link_id = dpu_vs_link_id[index];

	mutex_init(&vs->mutex);

	return 0;
}
