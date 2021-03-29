// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu-prv.h"

#define STATICCONTROL		0x8

#define FRAMEDIMENSIONS		0xc
#define  WIDTH(w)		(((w) - 1) & 0x3fff)
#define  HEIGHT(h)		((((h) - 1) & 0x3fff) << 16)

#define CONSTANTCOLOR		0x10
#define  RED(r)			(((r) & 0xff) << 24)
#define  GREEN(g)		(((g) & 0xff) << 16)
#define  BLUE(b)		(((b) & 0xff) << 8)
#define  ALPHA(a)		((a) & 0xff)

struct dpu_constframe {
	void __iomem *pec_base;
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	enum dpu_link_id link_id;
	bool inuse;
	struct dpu_soc *dpu;
};

static const enum dpu_link_id
dpu_cf_link_id[] = {LINK_ID_CONSTFRAME0, LINK_ID_CONSTFRAME1,
		    LINK_ID_CONSTFRAME4, LINK_ID_CONSTFRAME5};

static inline void dpu_cf_write(struct dpu_constframe *cf,
				unsigned int offset, u32 value)
{
	writel(value, cf->base + offset);
}

static void dpu_cf_enable_shden(struct dpu_constframe *cf)
{
	dpu_cf_write(cf, STATICCONTROL, SHDEN);
}

enum dpu_link_id dpu_cf_get_link_id(struct dpu_constframe *cf)
{
	return cf->link_id;
}

void dpu_cf_framedimensions(struct dpu_constframe *cf, unsigned int w,
			    unsigned int h)
{
	dpu_cf_write(cf, FRAMEDIMENSIONS, WIDTH(w) | HEIGHT(h));
}

void dpu_cf_constantcolor_black(struct dpu_constframe *cf)
{
	dpu_cf_write(cf, CONSTANTCOLOR, 0);
}

void dpu_cf_constantcolor_blue(struct dpu_constframe *cf)
{
	dpu_cf_write(cf, CONSTANTCOLOR, BLUE(0xff));
}

static struct dpu_constframe *dpu_cf_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_constframe *cf;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->cf_priv); i++) {
		cf = dpu->cf_priv[i];
		if (cf->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->cf_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&cf->mutex);

	if (cf->inuse) {
		mutex_unlock(&cf->mutex);
		return ERR_PTR(-EBUSY);
	}

	cf->inuse = true;

	mutex_unlock(&cf->mutex);

	return cf;
}

static void dpu_cf_put(struct dpu_constframe *cf)
{
	if (IS_ERR_OR_NULL(cf))
		return;

	mutex_lock(&cf->mutex);

	cf->inuse = false;

	mutex_unlock(&cf->mutex);
}

/* ConstFrame for safety stream */
struct dpu_constframe *dpu_cf_safe_get(struct dpu_soc *dpu,
				       unsigned int stream_id)
{
	return dpu_cf_get(dpu, stream_id + DPU_SAFETY_STREAM_OFFSET);
}

void dpu_cf_safe_put(struct dpu_constframe *cf)
{
	return dpu_cf_put(cf);
}

/* ConstFrame for content stream */
struct dpu_constframe *dpu_cf_cont_get(struct dpu_soc *dpu,
				       unsigned int stream_id)
{
	return dpu_cf_get(dpu, stream_id);
}

void dpu_cf_cont_put(struct dpu_constframe *cf)
{
	return dpu_cf_put(cf);
}

void dpu_cf_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	dpu_cf_enable_shden(dpu->cf_priv[index]);
}

int dpu_cf_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_constframe *cf;

	cf = devm_kzalloc(dpu->dev, sizeof(*cf), GFP_KERNEL);
	if (!cf)
		return -ENOMEM;

	dpu->cf_priv[index] = cf;

	cf->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!cf->pec_base)
		return -ENOMEM;

	cf->base = devm_ioremap(dpu->dev, base, SZ_32);
	if (!cf->base)
		return -ENOMEM;

	cf->dpu = dpu;
	cf->id = id;
	cf->index = index;
	cf->link_id = dpu_cf_link_id[index];

	mutex_init(&cf->mutex);

	return 0;
}
