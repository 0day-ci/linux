// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-fetchunit.h"
#include "dpu-prv.h"

#define FRAMEDIMENSIONS		0x150
#define FRAMERESAMPLING		0x154
#define CONTROL			0x158
#define TRIGGERENABLE		0x15c
#define CONTROLTRIGGER		0x160
#define START			0x164
#define FETCHTYPE		0x168
#define BURSTBUFFERPROPERTIES	0x16c
#define STATUS			0x170
#define HIDDENSTATUS		0x174

static const enum dpu_link_id dpu_fl_link_id[] = {LINK_ID_FETCHLAYER0};

static void dpu_fl_set_fmt(struct dpu_fetchunit *fu,
			   const struct drm_format_info *format,
			   enum drm_color_encoding color_encoding,
			   enum drm_color_range color_range,
			   bool unused)
{
	u32 bits = 0, shifts = 0;

	dpu_fu_set_src_bpp(fu, format->cpp[0] * 8);

	dpu_fu_write_mask(fu, LAYERPROPERTY(fu), YUVCONVERSIONMODE_MASK,
				YUVCONVERSIONMODE(YUVCONVERSIONMODE_OFF));

	dpu_fu_get_pixel_format_bits(fu, format->format, &bits);
	dpu_fu_get_pixel_format_shifts(fu, format->format, &shifts);

	dpu_fu_write(fu, COLORCOMPONENTBITS(fu), bits);
	dpu_fu_write(fu, COLORCOMPONENTSHIFT(fu), shifts);
}

static void
dpu_fl_set_framedimensions(struct dpu_fetchunit *fu, unsigned int w,
			   unsigned int h, bool unused)
{
	dpu_fu_write(fu, FRAMEDIMENSIONS, FRAMEWIDTH(w) | FRAMEHEIGHT(h));
}

static void dpu_fl_set_ops(struct dpu_fetchunit *fu)
{
	memcpy(&fu->ops, &dpu_fu_common_ops, sizeof(dpu_fu_common_ops));
	fu->ops.set_src_buf_dimensions =
				dpu_fu_set_src_buf_dimensions_no_deinterlace;
	fu->ops.set_fmt			= dpu_fl_set_fmt;
	fu->ops.set_framedimensions	= dpu_fl_set_framedimensions;
}

struct dpu_fetchunit *dpu_fl_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_fetchunit *fu;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->fl_priv); i++) {
		fu = dpu->fl_priv[i];
		if (fu->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->fl_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&fu->mutex);

	if (fu->inuse) {
		mutex_unlock(&fu->mutex);
		return ERR_PTR(-EBUSY);
	}

	fu->inuse = true;

	mutex_unlock(&fu->mutex);

	return fu;
}

void dpu_fl_put(struct dpu_fetchunit *fu)
{
	if (IS_ERR_OR_NULL(fu))
		return;

	mutex_lock(&fu->mutex);

	fu->inuse = false;

	mutex_unlock(&fu->mutex);
}

void dpu_fl_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_fetchunit *fu = dpu->fl_priv[index];

	dpu_fu_common_hw_init(fu);
	dpu_fu_shdldreq_sticky(fu, 0xff);
}

int dpu_fl_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_fetchunit *fu;
	int ret;

	fu = devm_kzalloc(dpu->dev, sizeof(*fu), GFP_KERNEL);
	if (!fu)
		return -ENOMEM;

	dpu->fl_priv[index] = fu;

	fu->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!fu->pec_base)
		return -ENOMEM;

	fu->base = devm_ioremap(dpu->dev, base, SZ_2K);
	if (!fu->base)
		return -ENOMEM;

	fu->dpu = dpu;
	fu->id = id;
	fu->index = index;
	fu->type = type;
	fu->sub_id = 0;
	fu->link_id = dpu_fl_link_id[index];
	snprintf(fu->name, sizeof(fu->name), "FetchLayer%u", id);

	ret = dpu_fu_attach_dprc(fu);
	if (ret) {
		dev_err_probe(dpu->dev, ret, "%s - failed to attach DPRC\n",
								fu->name);
		return ret;
	}

	dpu_fl_set_ops(fu);

	mutex_init(&fu->mutex);

	return 0;
}
