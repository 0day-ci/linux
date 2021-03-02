// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-fetchunit.h"
#include "dpu-prv.h"

#define FRAMEDIMENSIONS		0x38
#define FRAMERESAMPLING		0x3c
#define CONTROL			0x40
#define CONTROLTRIGGER		0x44
#define START			0x48
#define FETCHTYPE		0x4c
#define BURSTBUFFERPROPERTIES	0x50
#define HIDDENSTATUS		0x54

static const enum dpu_link_id dpu_fe_link_id[] = {
	LINK_ID_FETCHECO0, LINK_ID_FETCHECO1,
	LINK_ID_FETCHECO2, LINK_ID_FETCHECO9
};

static void
dpu_fe_set_src_buf_dimensions(struct dpu_fetchunit *fu,
			      unsigned int w, unsigned int h,
			      const struct drm_format_info *format,
			      bool deinterlace)
{
	struct dpu_soc *dpu = fu->dpu;
	unsigned int width, height;

	if (deinterlace) {
		width = w;
		height = h / 2;
	} else {
		width = w / format->hsub;
		height = h / format->vsub;
	}

	switch (format->format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		break;
	default:
		dev_warn(dpu->dev,
			 "%s - unsupported pixel format 0x%08x\n",
						fu->name, format->format);
		return;
	}

	dpu_fu_write(fu, SOURCEBUFFERDIMENSION(fu),
					LINEWIDTH(width) | LINECOUNT(height));
}

static void dpu_fe_set_fmt(struct dpu_fetchunit *fu,
			   const struct drm_format_info *format,
			   enum drm_color_encoding unused1,
			   enum drm_color_range unused2,
			   bool deinterlace)
{
	struct dpu_soc *dpu = fu->dpu;
	u32 bits = 0, shifts = 0;
	unsigned int x, y;

	switch (format->format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		break;
	default:
		dev_warn(dpu->dev,
			 "%s - unsupported pixel format 0x%08x\n",
						fu->name, format->format);
		return;
	}

	switch (format->hsub) {
	case 1:
		x = 0x4;
		break;
	case 2:
		x = 0x2;
		break;
	default:
		dev_warn(dpu->dev,
			 "%s - unsupported horizontal subsampling %u\n",
							fu->name, format->hsub);
		return;
	}

	switch (format->vsub) {
	case 1:
		y = 0x4;
		break;
	case 2:
		y = 0x2;
		break;
	default:
		dev_warn(dpu->dev,
			 "%s - unsupported vertical subsampling %u\n",
							fu->name, format->vsub);
		return;
	}

	dpu_fu_set_src_bpp(fu, 16);

	dpu_fu_write_mask(fu, FRAMERESAMPLING, DELTAX_MASK | DELTAY_MASK,
					DELTAX(x) | DELTAY(y));

	dpu_fu_write_mask(fu, CONTROL, RASTERMODE_MASK,
					RASTERMODE(RASTERMODE_NORMAL));

	dpu_fu_get_pixel_format_bits(fu, format->format, &bits);
	dpu_fu_get_pixel_format_shifts(fu, format->format, &shifts);

	dpu_fu_write(fu, COLORCOMPONENTBITS(fu), bits & ~Y_BITS_MASK);
	dpu_fu_write(fu, COLORCOMPONENTSHIFT(fu), shifts & ~Y_SHIFT_MASK);
}

static void dpu_fe_set_framedimensions(struct dpu_fetchunit *fu,
				       unsigned int w, unsigned int h,
				       bool deinterlace)
{
	if (deinterlace)
		h /= 2;

	dpu_fu_write(fu, FRAMEDIMENSIONS, FRAMEWIDTH(w) | FRAMEHEIGHT(h));
}

static void dpu_fe_set_ops(struct dpu_fetchunit *fu)
{
	memcpy(&fu->ops, &dpu_fu_common_ops, sizeof(dpu_fu_common_ops));
	fu->ops.set_src_buf_dimensions	= dpu_fe_set_src_buf_dimensions;
	fu->ops.set_fmt			= dpu_fe_set_fmt;
	fu->ops.set_framedimensions	= dpu_fe_set_framedimensions;
}

struct dpu_fetchunit *dpu_fe_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_fetchunit *fu;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->fe_priv); i++) {
		fu = dpu->fe_priv[i];
		if (fu->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->fe_priv))
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

void dpu_fe_put(struct dpu_fetchunit *fu)
{
	if (IS_ERR_OR_NULL(fu))
		return;

	mutex_lock(&fu->mutex);

	fu->inuse = false;

	mutex_unlock(&fu->mutex);
}

void dpu_fe_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	dpu_fu_common_hw_init(dpu->fe_priv[index]);
}

int dpu_fe_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_fetchunit *fu;

	fu = devm_kzalloc(dpu->dev, sizeof(*fu), GFP_KERNEL);
	if (!fu)
		return -ENOMEM;

	dpu->fe_priv[index] = fu;

	fu->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!fu->pec_base)
		return -ENOMEM;

	fu->base = devm_ioremap(dpu->dev, base, SZ_128);
	if (!fu->base)
		return -ENOMEM;

	fu->dpu = dpu;
	fu->id = id;
	fu->index = index;
	fu->type = type;
	fu->link_id = dpu_fe_link_id[index];
	snprintf(fu->name, sizeof(fu->name), "FetchECO%u", id);

	dpu_fe_set_ops(fu);

	mutex_init(&fu->mutex);

	return 0;
}
