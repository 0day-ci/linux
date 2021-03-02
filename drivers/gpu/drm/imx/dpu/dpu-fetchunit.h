/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2019,2020 NXP
 */

#ifndef __DPU_FETCHUNIT_H__
#define __DPU_FETCHUNIT_H__

#include <linux/io.h>
#include <linux/mutex.h>

#include "dpu.h"
#include "dpu-dprc.h"
#include "dpu-prv.h"

#define REG_OFFSET			((fu)->reg_offset)
#define SUBID_OFFSET			(((fu)->sub_id) * 0x28)

#define PIXENGCFG_DYNAMIC		0x8

#define SOURCEBUFFERDIMENSION(fu)	(0x18 + SUBID_OFFSET + REG_OFFSET)
#define  LINEWIDTH(w)			(((w) - 1) & 0x3fff)
#define  LINECOUNT(h)			((((h) - 1) & 0x3fff) << 16)

#define COLORCOMPONENTBITS(fu)		(0x1c + SUBID_OFFSET + REG_OFFSET)
#define  ITUFORMAT			BIT(31)
#define  R_BITS(n)			(((n) & 0xf) << 24)
#define  G_BITS(n)			(((n) & 0xf) << 16)
#define  B_BITS(n)			(((n) & 0xf) << 8)
#define  A_BITS(n)			((n) & 0xf)
#define  Y_BITS(n)			R_BITS(n)
#define  Y_BITS_MASK			0xf000000
#define  U_BITS(n)			G_BITS(n)
#define  U_BITS_MASK			0xf0000
#define  V_BITS(n)			B_BITS(n)
#define  V_BITS_MASK			0xf00

#define COLORCOMPONENTSHIFT(fu)		(0x20 + SUBID_OFFSET + REG_OFFSET)
#define  R_SHIFT(n)			(((n) & 0x1f) << 24)
#define  G_SHIFT(n)			(((n) & 0x1f) << 16)
#define  B_SHIFT(n)			(((n) & 0x1f) << 8)
#define  A_SHIFT(n)			((n) & 0x1f)
#define  Y_SHIFT(n)			R_SHIFT(n)
#define  Y_SHIFT_MASK			0x1f000000
#define  U_SHIFT(n)			G_SHIFT(n)
#define  U_SHIFT_MASK			0x1f0000
#define  V_SHIFT(n)			B_SHIFT(n)
#define  V_SHIFT_MASK			0x1f00

#define LAYERPROPERTY(fu)		(0x34 + SUBID_OFFSET + REG_OFFSET)
#define	 PALETTEENABLE			BIT(0)
enum dpu_tilemode {
	TILE_FILL_ZERO,
	TILE_FILL_CONSTANT,
	TILE_PAD,
	TILE_PAD_ZERO,
};
#define  ALPHASRCENABLE			BIT(8)
#define  ALPHACONSTENABLE		BIT(9)
#define  ALPHAMASKENABLE		BIT(10)
#define  ALPHATRANSENABLE		BIT(11)
#define  ALPHA_ENABLE_MASK		(ALPHASRCENABLE | ALPHACONSTENABLE | \
					 ALPHAMASKENABLE | ALPHATRANSENABLE)
#define  RGBALPHASRCENABLE		BIT(12)
#define  RGBALPHACONSTENABLE		BIT(13)
#define  RGBALPHAMASKENABLE		BIT(14)
#define  RGBALPHATRANSENABLE		BIT(15)
#define  RGB_ENABLE_MASK		(RGBALPHASRCENABLE |	\
					 RGBALPHACONSTENABLE |	\
					 RGBALPHAMASKENABLE |	\
					 RGBALPHATRANSENABLE)
#define  PREMULCONSTRGB			BIT(16)
enum dpu_yuvconversionmode {
	YUVCONVERSIONMODE_OFF,
	YUVCONVERSIONMODE_ITU601,
	YUVCONVERSIONMODE_ITU601_FR,
	YUVCONVERSIONMODE_ITU709,
};
#define  YUVCONVERSIONMODE_MASK		0x60000
#define  YUVCONVERSIONMODE(m)		(((m) & 0x3) << 17)
#define  GAMMAREMOVEENABLE		BIT(20)
#define  CLIPWINDOWENABLE		BIT(30)
#define  SOURCEBUFFERENABLE		BIT(31)

#define  EMPTYFRAME			BIT(31)
#define  FRAMEWIDTH(w)			(((w) - 1) & 0x3fff)
#define  FRAMEHEIGHT(h)			((((h) - 1) & 0x3fff) << 16)
#define  DELTAX_MASK			0x3f000
#define  DELTAY_MASK			0xfc0000
#define  DELTAX(x)			(((x) & 0x3f) << 12)
#define  DELTAY(y)			(((y) & 0x3f) << 18)
#define  YUV422UPSAMPLINGMODE_MASK	BIT(5)
#define  YUV422UPSAMPLINGMODE(m)	(((m) & 0x1) << 5)
enum dpu_yuv422upsamplingmode {
	YUV422UPSAMPLINGMODE_REPLICATE,
	YUV422UPSAMPLINGMODE_INTERPOLATE,
};
#define  INPUTSELECT_MASK		0x18
#define  INPUTSELECT(s)			(((s) & 0x3) << 3)
enum dpu_inputselect {
	INPUTSELECT_INACTIVE,
	INPUTSELECT_COMPPACK,
	INPUTSELECT_ALPHAMASK,
	INPUTSELECT_COORDINATE,
};
#define  RASTERMODE_MASK		0x7
#define  RASTERMODE(m)			((m) & 0x7)
enum dpu_rastermode {
	RASTERMODE_NORMAL,
	RASTERMODE_DECODE,
	RASTERMODE_ARBITRARY,
	RASTERMODE_PERSPECTIVE,
	RASTERMODE_YUV422,
	RASTERMODE_AFFINE,
};

struct dpu_fetchunit {
	void __iomem *pec_base;
	void __iomem *base;
	char name[13];
	struct mutex mutex;
	struct list_head node;
	unsigned int reg_offset;
	unsigned int id;
	unsigned int index;
	unsigned int sub_id;	/* for fractional fetch units */
	unsigned int stream_id;
	enum dpu_unit_type type;
	enum dpu_link_id link_id;
	u32 cap_mask;
	bool inuse;
	bool is_available;
	struct dpu_soc *dpu;
	struct dpu_fetchunit_ops ops;
	struct dpu_dprc *dprc;
	struct dpu_fetchunit *fe;
	struct dpu_hscaler *hs;
	struct dpu_vscaler *vs;
	struct dpu_layerblend *lb;
};

extern const struct dpu_fetchunit_ops dpu_fu_common_ops;

static inline void
dpu_pec_fu_write(struct dpu_fetchunit *fu, unsigned int offset, u32 value)
{
	writel(value, fu->pec_base + offset);
}

static inline u32 dpu_pec_fu_read(struct dpu_fetchunit *fu, unsigned int offset)
{
	return readl(fu->pec_base + offset);
}

static inline u32 dpu_fu_read(struct dpu_fetchunit *fu, unsigned int offset)
{
	return readl(fu->base + offset);
}

static inline void
dpu_fu_write(struct dpu_fetchunit *fu, unsigned int offset, u32 value)
{
	writel(value, fu->base + offset);
}

static inline void dpu_fu_write_mask(struct dpu_fetchunit *fu,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_fu_read(fu, offset);
	tmp &= ~mask;
	dpu_fu_write(fu, offset, tmp | value);
}

void dpu_fu_get_pixel_format_bits(struct dpu_fetchunit *fu,
				  u32 format, u32 *bits);
void dpu_fu_get_pixel_format_shifts(struct dpu_fetchunit *fu,
				    u32 format, u32 *shifts);
void dpu_fu_shdldreq_sticky(struct dpu_fetchunit *fu, u8 layer_mask);
void dpu_fu_set_src_bpp(struct dpu_fetchunit *fu, unsigned int bpp);
void
dpu_fu_set_src_buf_dimensions_no_deinterlace(struct dpu_fetchunit *fu,
					unsigned int w, unsigned int h,
					const struct drm_format_info *unused1,
					bool unused2);
void dpu_fu_common_hw_init(struct dpu_fetchunit *fu);
int dpu_fu_attach_dprc(struct dpu_fetchunit *fu);

#endif /* __DPU_FETCHUNIT_H__ */
