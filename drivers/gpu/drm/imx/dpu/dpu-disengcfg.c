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

#define POLARITYCTRL		0xc
#define  POLHS_HIGH		BIT(0)
#define  POLVS_HIGH		BIT(1)
#define  POLEN_HIGH		BIT(2)
#define  PIXINV_INV		BIT(3)

#define SRCSELECT0		0x10
#define  PATH_SELECT0		BIT(4)
#define  MATRIX_FIRST		BIT(4)
#define  GAMMA_FIRST		0
#define  SIG_SELECT0		0x3
#define  SIG_FRAMEGEN		0x0
#define  SIG_GAMMACOR		0x1
#define  SIG_MATRIX		0x2
#define  SIG_DITHER		0x3

struct dpu_disengcfg {
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	bool inuse;
	struct dpu_soc *dpu;
};

static inline void dpu_dec_write(struct dpu_disengcfg *dec,
				 unsigned int offset, u32 value)
{
	writel(value, dec->base + offset);
}

struct dpu_disengcfg *dpu_dec_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_disengcfg *dec;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->dec_priv); i++) {
		dec = dpu->dec_priv[i];
		if (dec->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->dec_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&dec->mutex);

	if (dec->inuse) {
		mutex_unlock(&dec->mutex);
		return ERR_PTR(-EBUSY);
	}

	dec->inuse = true;

	mutex_unlock(&dec->mutex);

	return dec;
}

void dpu_dec_put(struct dpu_disengcfg *dec)
{
	if (IS_ERR_OR_NULL(dec))
		return;

	mutex_lock(&dec->mutex);

	dec->inuse = false;

	mutex_unlock(&dec->mutex);
}

void dpu_dec_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_disengcfg *dec = dpu->dec_priv[index];

	dpu_dec_write(dec, POLARITYCTRL, POLEN_HIGH);
	dpu_dec_write(dec, SRCSELECT0, GAMMA_FIRST | SIG_FRAMEGEN);
}

int dpu_dec_init(struct dpu_soc *dpu, unsigned int index,
		 unsigned int id, enum dpu_unit_type type,
		 unsigned long unused, unsigned long base)
{
	struct dpu_disengcfg *dec;

	dec = devm_kzalloc(dpu->dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	dpu->dec_priv[index] = dec;

	dec->base = devm_ioremap(dpu->dev, base, SZ_32);
	if (!dec->base)
		return -ENOMEM;

	dec->dpu = dpu;
	dec->id = id;
	dec->index = index;

	mutex_init(&dec->mutex);

	return 0;
}
