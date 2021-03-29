// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2018-2020 NXP
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-fetchunit.h"
#include "dpu-prv.h"

#define FRAMEDIMENSIONS		0x150
#define FRAMERESAMPLING		0x154
#define WARPCONTROL		0x158
#define ARBSTARTX		0x15c
#define ARBSTARTY		0x160
#define ARBDELTA		0x164
#define FIRPOSITIONS		0x168
#define FIRCOEFFICIENTS		0x16c
#define CONTROL			0x170
#define TRIGGERENABLE		0x174
#define CONTROLTRIGGER		0x178
#define START			0x17c
#define FETCHTYPE		0x180
#define BURSTBUFFERPROPERTIES	0x184
#define STATUS			0x188
#define HIDDENSTATUS		0x18c

static const enum dpu_link_id dpu_fw_link_id[] = {
	LINK_ID_FETCHWARP2, LINK_ID_FETCHWARP9
};

static const enum dpu_link_id fw_srcs[2][2] = {
	{
		LINK_ID_NONE,
		LINK_ID_FETCHECO2,
	}, {
		LINK_ID_NONE,
		LINK_ID_FETCHECO9,
	},
};

static void dpu_fw_pec_dynamic_src_sel(struct dpu_fetchunit *fu,
				       enum dpu_link_id src)
{
	struct dpu_soc *dpu = fu->dpu;
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_srcs[fu->index]); i++) {
		if (fw_srcs[fu->index][i] == src) {
			dpu_pec_fu_write(fu, PIXENGCFG_DYNAMIC, src);
			return;
		}
	}

	dev_err(dpu->dev, "%s - invalid source 0x%02x\n", fu->name, src);
}

static void dpu_fw_set_fmt(struct dpu_fetchunit *fu,
			   const struct drm_format_info *format,
			   enum drm_color_encoding color_encoding,
			   enum drm_color_range color_range,
			   bool unused)
{
	u32 val, bits = 0, shifts = 0;
	bool is_planar_yuv = false, is_rastermode_yuv422 = false;
	bool is_inputselect_compact = false;
	unsigned int bpp;

	switch (format->format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
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
	val &= ~INPUTSELECT_MASK;
	val &= ~RASTERMODE_MASK;
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

static void
dpu_fw_set_framedimensions(struct dpu_fetchunit *fu,
			   unsigned int w, unsigned int h, bool unused)
{
	dpu_fu_write(fu, FRAMEDIMENSIONS, FRAMEWIDTH(w) | FRAMEHEIGHT(h));
}

static void dpu_fw_set_ops(struct dpu_fetchunit *fu)
{
	memcpy(&fu->ops, &dpu_fu_common_ops, sizeof(dpu_fu_common_ops));
	fu->ops.set_pec_dynamic_src_sel = dpu_fw_pec_dynamic_src_sel;
	fu->ops.set_src_buf_dimensions =
				dpu_fu_set_src_buf_dimensions_no_deinterlace;
	fu->ops.set_fmt			= dpu_fw_set_fmt;
	fu->ops.set_framedimensions	= dpu_fw_set_framedimensions;
}

struct dpu_fetchunit *dpu_fw_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_fetchunit *fu;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->fw_priv); i++) {
		fu = dpu->fw_priv[i];
		if (fu->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->fw_priv))
		return ERR_PTR(-EINVAL);

	fu->fe = dpu_fe_get(dpu, id);
	if (IS_ERR(fu->fe))
		return ERR_CAST(fu->fe);

	if (fu->type == DPU_BLIT) {
		fu->hs = dpu_hs_get(dpu, id);
		if (IS_ERR(fu->hs))
			return ERR_CAST(fu->hs);

		fu->vs = dpu_vs_get(dpu, id);
		if (IS_ERR(fu->vs))
			return ERR_CAST(fu->vs);
	}

	mutex_lock(&fu->mutex);

	if (fu->inuse) {
		mutex_unlock(&fu->mutex);
		return ERR_PTR(-EBUSY);
	}

	fu->inuse = true;

	mutex_unlock(&fu->mutex);

	return fu;
}

void dpu_fw_put(struct dpu_fetchunit *fu)
{
	if (IS_ERR_OR_NULL(fu))
		return;

	mutex_lock(&fu->mutex);

	fu->inuse = false;

	mutex_unlock(&fu->mutex);
}

void dpu_fw_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_fetchunit *fu = dpu->fw_priv[index];

	fu->ops.set_pec_dynamic_src_sel(fu, LINK_ID_NONE);
	dpu_fu_common_hw_init(fu);
	dpu_fu_shdldreq_sticky(fu, 0xff);
}

int dpu_fw_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base)
{
	struct dpu_fetchunit *fu;
	int ret;

	fu = devm_kzalloc(dpu->dev, sizeof(*fu), GFP_KERNEL);
	if (!fu)
		return -ENOMEM;

	dpu->fw_priv[index] = fu;

	fu->pec_base = devm_ioremap(dpu->dev, pec_base, SZ_16);
	if (!fu->pec_base)
		return -ENOMEM;

	fu->base = devm_ioremap(dpu->dev, base, SZ_512);
	if (!fu->base)
		return -ENOMEM;

	fu->dpu = dpu;
	fu->id = id;
	fu->index = index;
	fu->type = type;
	fu->sub_id = 0;
	fu->link_id = dpu_fw_link_id[index];
	fu->cap_mask = DPU_FETCHUNIT_CAP_USE_FETCHECO;
	snprintf(fu->name, sizeof(fu->name), "FetchWarp%u", id);

	ret = dpu_fu_attach_dprc(fu);
	if (ret) {
		dev_err_probe(dpu->dev, ret, "%s - failed to attach DPRC\n",
								fu->name);
		return ret;
	}

	dpu_fw_set_ops(fu);

	mutex_init(&fu->mutex);

	return 0;
}
