// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2020 NXP
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-prv.h"

#define STATICCONTROL		0x8
#define  BLUEWRITEENABLE	BIT(1)
#define  GREENWRITEENABLE	BIT(2)
#define  REDWRITEENABLE		BIT(3)
#define  COLORWRITEENABLE	(REDWRITEENABLE |	\
				 GREENWRITEENABLE |	\
				 BLUEWRITEENABLE)

#define LUTSTART		0xc
#define  STARTBLUE(n)		((n) & 0x3ff)
#define  STARTGREEN(n)		(((n) & 0x3ff) << 10)
#define  STARTRED(n)		(((n) & 0x3ff) << 20)

#define LUTDELTAS		0x10
#define  DELTABLUE(n)		((n) & 0x3ff)
#define  DELTAGREEN(n)		(((n) & 0x3ff) << 10)
#define  DELTARED(n)		(((n) & 0x3ff) << 20)

#define CONTROL			0x14
#define  CTRL_MODE_MASK		BIT(0)
#define  ALHPAMASK		BIT(4)
#define  ALHPAINVERT		BIT(5)

/* 16-bit to 10-bit */
#define GAMMACOR_COL_CONVERT(n)	(((n) * 0x3ff) / 0xffff)

struct dpu_gammacor {
	void __iomem *base;
	struct mutex mutex;
	unsigned int id;
	unsigned int index;
	bool inuse;
	struct dpu_soc *dpu;
};

static inline u32 dpu_gc_read(struct dpu_gammacor *gc, unsigned int offset)
{
	return readl(gc->base + offset);
}

static inline void dpu_gc_write(struct dpu_gammacor *gc,
				unsigned int offset, u32 value)
{
	writel(value, gc->base + offset);
}

static inline void dpu_gc_write_mask(struct dpu_gammacor *gc,
				     unsigned int offset, u32 mask, u32 value)
{
	u32 tmp;

	tmp = dpu_gc_read(gc, offset);
	tmp &= ~mask;
	dpu_gc_write(gc, offset, tmp | value);
}

static void dpu_gc_enable_shden(struct dpu_gammacor *gc)
{
	dpu_gc_write_mask(gc, STATICCONTROL, SHDEN, SHDEN);
}

void dpu_gc_enable_rgb_write(struct dpu_gammacor *gc)
{
	dpu_gc_write_mask(gc, STATICCONTROL, COLORWRITEENABLE, COLORWRITEENABLE);
}

void dpu_gc_disable_rgb_write(struct dpu_gammacor *gc)
{
	dpu_gc_write_mask(gc, STATICCONTROL, COLORWRITEENABLE, 0);
}

static inline void
dpu_gc_sample_point_color_convert(u32 *red, u32 *green, u32 *blue)
{
	*red   = GAMMACOR_COL_CONVERT(*red);
	*green = GAMMACOR_COL_CONVERT(*green);
	*blue  = GAMMACOR_COL_CONVERT(*blue);
}

void dpu_gc_start_rgb(struct dpu_gammacor *gc, const struct drm_color_lut *lut)
{
	struct dpu_soc *dpu = gc->dpu;
	u32 r = lut[0].red, g = lut[0].green, b = lut[0].blue;

	dpu_gc_sample_point_color_convert(&r, &g, &b);

	dpu_gc_write(gc, LUTSTART, STARTRED(r) | STARTGREEN(g) | STARTBLUE(b));

	dev_dbg(dpu->dev, "GammaCor%u LUT start:\t r-0x%03x g-0x%03x b-0x%03x\n",
							gc->id, r, g, b);
}

void dpu_gc_delta_rgb(struct dpu_gammacor *gc, const struct drm_color_lut *lut)
{
	struct dpu_soc *dpu = gc->dpu;
	int i, curr, next;
	u32 dr, dg, db;

	/* The first delta value is zero. */
	dpu_gc_write(gc, LUTDELTAS, DELTARED(0) | DELTAGREEN(0) | DELTABLUE(0));

	/*
	 * Assuming gamma_size = 256, we get additional 32 delta
	 * values for every 8 sample points, so 33 delta values for
	 * 33 sample points in total as the GammaCor unit requires.
	 * A curve with 10-bit resolution will be generated in the
	 * GammaCor unit internally by a linear interpolation in-between
	 * the sample points.  Note that the last value in the lookup
	 * table is lut[255].
	 */
	for (i = 0; i < 32; i++) {
		curr = i * 8;
		next = curr + 8;

		if (next == 256)
			next--;

		dr = lut[next].red   - lut[curr].red;
		dg = lut[next].green - lut[curr].green;
		db = lut[next].blue  - lut[curr].blue;

		dpu_gc_sample_point_color_convert(&dr, &dg, &db);

		dpu_gc_write(gc, LUTDELTAS,
			     DELTARED(dr) | DELTAGREEN(dg) | DELTABLUE(db));

		dev_dbg(dpu->dev,
			"GammaCor%u delta[%d]:\t r-0x%03x g-0x%03x b-0x%03x\n",
						gc->id, i + 1, dr, dg, db);
	}
}

void dpu_gc_mode(struct dpu_gammacor *gc, enum dpu_gc_mode mode)
{
	dpu_gc_write_mask(gc, CONTROL, CTRL_MODE_MASK, mode);
}

struct dpu_gammacor *dpu_gc_get(struct dpu_soc *dpu, unsigned int id)
{
	struct dpu_gammacor *gc;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpu->gc_priv); i++) {
		gc = dpu->gc_priv[i];
		if (gc->id == id)
			break;
	}

	if (i == ARRAY_SIZE(dpu->gc_priv))
		return ERR_PTR(-EINVAL);

	mutex_lock(&gc->mutex);

	if (gc->inuse) {
		mutex_unlock(&gc->mutex);
		return ERR_PTR(-EBUSY);
	}

	gc->inuse = true;

	mutex_unlock(&gc->mutex);

	return gc;
}

void dpu_gc_put(struct dpu_gammacor *gc)
{
	if (IS_ERR_OR_NULL(gc))
		return;

	mutex_lock(&gc->mutex);

	gc->inuse = false;

	mutex_unlock(&gc->mutex);
}

void dpu_gc_hw_init(struct dpu_soc *dpu, unsigned int index)
{
	struct dpu_gammacor *gc = dpu->gc_priv[index];

	dpu_gc_write(gc, CONTROL, 0);
	dpu_gc_enable_shden(gc);
}

int dpu_gc_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long unused, unsigned long base)
{
	struct dpu_gammacor *gc;

	gc = devm_kzalloc(dpu->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	dpu->gc_priv[index] = gc;

	gc->base = devm_ioremap(dpu->dev, base, SZ_32);
	if (!gc->base)
		return -ENOMEM;

	gc->dpu = dpu;
	gc->id = id;
	gc->index = index;

	mutex_init(&gc->mutex);

	return 0;
}
