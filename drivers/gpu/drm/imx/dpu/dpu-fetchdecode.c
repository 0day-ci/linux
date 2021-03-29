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

#define RINGBUFSTARTADDR0	0x10
#define RINGBUFWRAPADDR0	0x14
#define FRAMEPROPERTIES0	0x18
#define FRAMEDIMENSIONS		0x44
#define FRAMERESAMPLING		0x48
#define DECODECONTROL		0x4c
#define SOURCEBUFFERLENGTH	0x50
#define CONTROL			0x54
#define CONTROLTRIGGER		0x58
#define START			0x5c
#define FETCHTYPE		0x60
#define DECODERSTATUS		0x64
#define READADDRESS0		0x68
#define BURSTBUFFERPROPERTIES	0x6c
#define STATUS			0x70
#define HIDDENSTATUS		0x74

#define DPU_FETCHDECODE_DISP_SCALER_OFFSET	4
#define DPU_FETCHDECODE_REG_OFFSET		0xc

#define DPU_FETCHDECODE_CAP_MASK	(DPU_FETCHUNIT_CAP_USE_FETCHECO | \
					 DPU_FETCHUNIT_CAP_USE_SCALER |   \
					 DPU_FETCHUNIT_CAP_PACKED_YUV422)

static const enum dpu_link_id dpu_fd_link_id[] = {
	LINK_ID_FETCHDECODE0, LINK_ID_FETCHDECODE1, LINK_ID_FETCHDECODE9
};

static const enum dpu_link_id fd_srcs[3][4] = {
	{
		LINK_ID_NONE,
		LINK_ID_FETCHECO0,
		LINK_ID_FETCHDECODE1,
		LINK_ID_FETCHWARP2,
	}, {
		LINK_ID_NONE,
		LINK_ID_FETCHECO1,
		LINK_ID_FETCHDECODE0,
		LINK_ID_FETCHWARP2,
	}, {
		LINK_ID_NONE,
		LINK_ID_FETCHECO9,
		LINK_ID_FETCHWARP9,
	},
};

static void dpu_fd_pec_dynamic_src_sel(struct dpu_fetchunit *fu,
				       enum dpu_link_id src)
{
	struct dpu_soc *dpu = fu->dpu;
	int i;

	for (i = 0; i < ARRAY_SIZE(fd_srcs[fu->index]); i++) {
		if (fd_srcs[fu->index][i] == src) {
			dpu_pec_fu_write(fu, PIXENGCFG_DYNAMIC, src);
			return;
		}
	}

	dev_err(dpu->dev, "%s - invalid source 0x%02x\n", fu->name, src);
}

static void
dpu_fd_set_src_buf_dimensions(struct dpu_fetchunit *fu,
			      unsigned int w, unsigned int h,
			      const struct drm_format_info *unused,
			      bool deinterlace)
{
	if (deinterlace)
		h /= 2;

	dpu_fu_write(fu, SOURCEBUFFERDIMENSION(fu),
						LINEWIDTH(w) | LINECOUNT(h));
}

static void dpu_fd_set_fmt(struct dpu_fetchunit *fu,
			   const struct drm_format_info *format,
			   enum drm_color_encoding color_encoding,
			   enum drm_color_range color_range,
			   bool deinterlace)
{
	u32 val, bits = 0, shifts = 0;
	bool is_planar_yuv = false, is_rastermode_yuv422 = false;
	bool is_yuv422upsamplingmode_interpolate = false;
	bool is_inputselect_compact = false;
	unsigned int bpp;

	switch (format->format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		is_rastermode_yuv422 = true;
		is_yuv422upsamplingmode_interpolate = true;
		bpp = 16;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		if (deinterlace)
			is_yuv422upsamplingmode_interpolate = true;
		is_planar_yuv = true;
		is_rastermode_yuv422 = true;
		is_inputselect_compact = true;
		bpp = format->cpp[0] * 8;
		break;
	default:
		bpp = format->cpp[0] * 8;
		break;
	}

	dpu_fu_set_src_bpp(fu, bpp);

	val = dpu_fu_read(fu, CONTROL);
	val &= ~YUV422UPSAMPLINGMODE_MASK;
	val &= ~INPUTSELECT_MASK;
	val &= ~RASTERMODE_MASK;
	if (is_yuv422upsamplingmode_interpolate)
		val |= YUV422UPSAMPLINGMODE(YUV422UPSAMPLINGMODE_INTERPOLATE);
	else
		val |= YUV422UPSAMPLINGMODE(YUV422UPSAMPLINGMODE_REPLICATE);
	if (is_inputselect_compact)
		val |= INPUTSELECT(INPUTSELECT_COMPPACK);
	else
		val |= INPUTSELECT(INPUTSELECT_INACTIVE);
	if (is_rastermode_yuv422)
		val |= RASTERMODE(RASTERMODE_YUV422);
	else
		val |= RASTERMODE(RASTERMODE_NORMAL);
	dpu_fu_write(fu, CONTROL, val);

	val = dpu_fu_read(fu, LAYERPROPERTY(fu));
	val &= ~YUVCONVERSIONMODE_MASK;
	if (format->is_yuv) {
		if (color_encoding == DRM_COLOR_YCBCR_BT709)
			val |= YUVCONVERSIONMODE(YUVCONVERSIONMODE_ITU709);
		else if (color_encoding == DRM_COLOR_YCBCR_BT601 &&
			 color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			val |= YUVCONVERSIONMODE(YUVCONVERSIONMODE_ITU601_FR);
		else
			val |= YUVCONVERSIONMODE(YUVCONVERSIONMODE_ITU601);
	} else {
		val |= YUVCONVERSIONMODE(YUVCONVERSIONMODE_OFF);
	}
	dpu_fu_write(fu, LAYERPROPERTY(fu), val);

	dpu_fu_get_pixel_format_bits(fu, format->format, &bits);
	dpu_fu_get_pixel_format_shifts(fu, format->format, &shifts);

	if (is_planar_yuv) {
		bits &= ~(U_BITS_MASK | V_BITS_MASK);
		shifts &= ~(U_SHIFT_MASK | V_SHIFT_MASK);
	}

	dpu_fu_write(fu, COLORCOMPONENTBITS(fu), bits);
	dpu_fu_write(fu, COLORCOMPONENTSHIFT(fu), shifts);
}

static void dpu_fd_set_framedimensions(struct dpu_fetchunit *fu,
				       unsigned int w, unsigned int h,
				       bool deinterlace)
{
	if (deinterlace)
		h /= 2;

	dpu_fu_write(fu, FRAMEDIMENSIONS, FRAMEWIDTH(w) | FRAMEHEIGHT(h));
}

static void dpu_fd_set_ops(struct dpu_fetchunit *fu)
{
	memcpy(&fu->ops, &dpu_fu_common_ops, sizeof(dpu_fu_common_ops));
	fu->ops.set_pec_dynamic_src_sel = dpu_fd_pec_dynamic_src_sel;
	fu->ops.set_src_buf_dimensions	= dpu_fd_set_src_buf_dimensions;
	fu->ops.set_fmt			= dpu_fd_set_fmt;
	fu->ops.set_framedimensions	= dpu_fd_set_framedimensions;
}

struct dpu_fetchunit *dpu_fd_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_fetchunit *fu;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->fd_priv); i++) {
		fu = dpu->fd_priv[i];
		if (fu->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->fd_priv))
		return ERR_PTR(-EINVAL);

	fu->fe = dpu_fe_get(dpu, id);
	if (IS_ERR(fu->fe))
		return ERR_CAST(fu->fe);

	fu->hs = dpu_hs_get(dpu, fu->type == DPU_DISP ?
				id + DPU_FETCHDECODE_DISP_SCALER_OFFSET : id);
	if (IS_ERR(fu->hs))
		return ERR_CAST(fu->hs);

	fu->vs = dpu_vs_get(dpu, fu->type == DPU_DISP ?
				id + DPU_FETCHDECODE_DISP_SCALER_OFFSET : id);
	if (IS_ERR(fu->vs))
		return ERR_CAST(fu->vs);

	mutex_lock(&fu->mutex);

	if (fu->inuse) {
		mutex_unlock(&fu->mutex);
		return ERR_PTR(-EBUSY);
	}

	fu->inuse = true;

	mutex_unlock(&fu->mutex);

	return fu;
}

void dpu_fd_put(struct dpu_fetchunit *fu)
{
	if (IS_ERR_OR_NULL(fu))
		return;

	mutex_lock(&fu->mutex);

	fu->inuse = false;

	mutex_unlock(&fu->mutex);
}

void dpu_fd_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_fetchunit *fu = dpu->fd_priv[index];

	fu->ops.set_pec_dynamic_src_sel(fu, LINK_ID_NONE);
	dpu_fu_common_hw_init(fu);
}

int dpu_fd_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_fetchunit *fu;
	int ret;

	fu = devm_kzalloc(dpu->dev, sizeof(*fu), GFP_KERNEL);
	if (!fu)
		return -ENOMEM;

	dpu->fd_priv[index] = fu;

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
	fu->link_id = dpu_fd_link_id[index];
	fu->cap_mask = DPU_FETCHDECODE_CAP_MASK;
	fu->reg_offset = DPU_FETCHDECODE_REG_OFFSET;
	snprintf(fu->name, sizeof(fu->name), "FetchDecode%u", id);

	ret = dpu_fu_attach_dprc(fu);
	if (ret) {
		dev_err_probe(dpu->dev, ret, "%s - failed to attach DPRC\n",
								fu->name);
		return ret;
	}

	dpu_fd_set_ops(fu);

	mutex_init(&fu->mutex);

	return 0;
}
