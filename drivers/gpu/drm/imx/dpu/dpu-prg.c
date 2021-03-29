// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "dpu-prg.h"

#define SET			0x4
#define CLR			0x8
#define TOG			0xc

#define PRG_CTRL		0x00
#define  BYPASS			BIT(0)
#define  SC_DATA_TYPE_8BIT	0
#define  SC_DATA_TYPE_10BIT	BIT(2)
#define  UV_EN			BIT(3)
#define  HANDSHAKE_MODE_4LINES	0
#define  HANDSHAKE_MODE_8LINES	BIT(4)
#define  SHADOW_LOAD_MODE	BIT(5)
#define  DES_DATA_TYPE_32BPP	(0 << 16)
#define  DES_DATA_TYPE_24BPP	(1 << 16)
#define  DES_DATA_TYPE_16BPP	(2 << 16)
#define  DES_DATA_TYPE_8BPP	(3 << 16)
#define  SOFTRST		BIT(30)
#define  SHADOW_EN		BIT(31)

#define PRG_STATUS		0x10
#define  BUFFER_VALID_B		BIT(1)
#define  BUFFER_VALID_A		BIT(0)

#define PRG_REG_UPDATE		0x20
#define  REG_UPDATE		BIT(0)

#define PRG_STRIDE		0x30
#define  STRIDE(n)		(((n) - 1) & 0xffff)

#define PRG_HEIGHT		0x40
#define  HEIGHT(n)		(((n) - 1) & 0xffff)

#define PRG_BADDR		0x50

#define PRG_OFFSET		0x60
#define  Y(n)			(((n) & 0x7) << 16)
#define  X(n)			((n) & 0xffff)

#define PRG_WIDTH		0x70
#define  WIDTH(n)		(((n) - 1) & 0xffff)

#define DPU_PRG_MAX_STRIDE	0x10000

struct dpu_prg {
	struct device *dev;
	void __iomem *base;
	struct list_head list;
	struct clk *clk_apb;
	struct clk *clk_rtram;
	bool is_auxiliary;
};

static DEFINE_MUTEX(dpu_prg_list_mutex);
static LIST_HEAD(dpu_prg_list);

static inline u32 dpu_prg_read(struct dpu_prg *prg, unsigned int offset)
{
	return readl(prg->base + offset);
}

static inline void
dpu_prg_write(struct dpu_prg *prg, unsigned int offset, u32 value)
{
	writel(value, prg->base + offset);
}

static void dpu_prg_reset(struct dpu_prg *prg)
{
	usleep_range(10, 20);
	dpu_prg_write(prg, PRG_CTRL + SET, SOFTRST);
	usleep_range(10, 20);
	dpu_prg_write(prg, PRG_CTRL + CLR, SOFTRST);
}

void dpu_prg_enable(struct dpu_prg *prg)
{
	dpu_prg_write(prg, PRG_CTRL + CLR, BYPASS);
}

void dpu_prg_disable(struct dpu_prg *prg)
{
	dpu_prg_write(prg, PRG_CTRL, BYPASS);
}

static int dpu_prg_mod_to_mt_w(struct dpu_prg *prg, u64 modifier,
			       unsigned int bits_per_pixel, unsigned int *mt_w)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_NONE:
		*mt_w = 0;
		break;
	case DRM_FORMAT_MOD_VIVANTE_TILED:
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		*mt_w = (bits_per_pixel == 16) ? 8 : 4;
		break;
	default:
		dev_err(prg->dev, "unsupported modifier 0x%016llx\n", modifier);
		return -EINVAL;
	}

	return 0;
}

static int dpu_prg_mod_to_mt_h(struct dpu_prg *prg, u64 modifier,
			       unsigned int *mt_h)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_NONE:
		*mt_h = 0;
		break;
	case DRM_FORMAT_MOD_VIVANTE_TILED:
	case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
		*mt_h = 4;
		break;
	default:
		dev_err(prg->dev, "unsupported modifier 0x%016llx\n", modifier);
		return -EINVAL;
	}

	return 0;
}

/* address TKT343664: base address has to align to burst size */
static unsigned int dpu_prg_burst_size_fixup(dma_addr_t baddr)
{
	unsigned int burst_size;

	burst_size = 1 << __ffs(baddr);
	burst_size = round_up(burst_size, 8);
	burst_size = min(burst_size, 128U);

	return burst_size;
}

/* address TKT339017: mismatch between burst size and stride */
static unsigned int dpu_prg_stride_fixup(unsigned int stride,
					 unsigned int burst_size,
					 dma_addr_t baddr, u64 modifier)
{
	if (modifier)
		stride = round_up(stride + round_up(baddr % 8, 8), burst_size);
	else
		stride = round_up(stride, burst_size);

	return stride;
}

void dpu_prg_configure(struct dpu_prg *prg,
		       unsigned int width, unsigned int height,
		       unsigned int x_offset, unsigned int y_offset,
		       unsigned int stride, unsigned int bits_per_pixel,
		       dma_addr_t baddr,
		       const struct drm_format_info *format, u64 modifier,
		       bool start)
{
	unsigned int mt_w, mt_h;	/* micro-tile width/height */
	unsigned int burst_size;
	dma_addr_t _baddr;
	u32 val;
	int ret;

	ret = dpu_prg_mod_to_mt_w(prg, modifier, bits_per_pixel, &mt_w);
	ret |= dpu_prg_mod_to_mt_h(prg, modifier, &mt_h);
	if (ret)
		return;

	if (modifier) {
		x_offset %= mt_w;
		y_offset %= mt_h;

		/* consider x offset to calculate stride */
		_baddr = baddr + (x_offset * (bits_per_pixel / 8));
	} else {
		x_offset = 0;
		y_offset = 0;
		_baddr = baddr;
	}

	burst_size = dpu_prg_burst_size_fixup(_baddr);

	stride = dpu_prg_stride_fixup(stride, burst_size, _baddr, modifier);

	/*
	 * address TKT342628(part 1):
	 * when prg stride is less or equals to burst size,
	 * the auxiliary prg height needs to be a half
	 */
	if (prg->is_auxiliary && stride <= burst_size) {
		height /= 2;
		if (modifier)
			y_offset /= 2;
	}

	dpu_prg_write(prg, PRG_STRIDE, STRIDE(stride));
	dpu_prg_write(prg, PRG_WIDTH, WIDTH(width));
	dpu_prg_write(prg, PRG_HEIGHT, HEIGHT(height));
	dpu_prg_write(prg, PRG_OFFSET, X(x_offset) | Y(y_offset));
	dpu_prg_write(prg, PRG_BADDR, baddr);

	val = SHADOW_LOAD_MODE | SC_DATA_TYPE_8BIT | BYPASS;
	if (format->format == DRM_FORMAT_NV21 ||
	    format->format == DRM_FORMAT_NV12) {
		val |= HANDSHAKE_MODE_8LINES;
		/*
		 * address TKT342628(part 2):
		 * when prg stride is less or equals to burst size,
		 * we disable UV_EN bit for the auxiliary prg
		 */
		if (prg->is_auxiliary && stride > burst_size)
			val |= UV_EN;
	} else {
		val |= HANDSHAKE_MODE_4LINES;
	}
	switch (bits_per_pixel) {
	case 32:
		val |= DES_DATA_TYPE_32BPP;
		break;
	case 24:
		val |= DES_DATA_TYPE_24BPP;
		break;
	case 16:
		val |= DES_DATA_TYPE_16BPP;
		break;
	case 8:
		val |= DES_DATA_TYPE_8BPP;
		break;
	}
	/* no shadow for the first frame */
	if (!start)
		val |= SHADOW_EN;
	dpu_prg_write(prg, PRG_CTRL, val);
}

void dpu_prg_reg_update(struct dpu_prg *prg)
{
	dpu_prg_write(prg, PRG_REG_UPDATE, REG_UPDATE);
}

void dpu_prg_shadow_enable(struct dpu_prg *prg)
{
	dpu_prg_write(prg, PRG_CTRL + SET, SHADOW_EN);
}

bool dpu_prg_stride_supported(struct dpu_prg *prg,
			      unsigned int x_offset,
			      unsigned int bits_per_pixel, u64 modifier,
			      unsigned int stride, dma_addr_t baddr)
{
	unsigned int mt_w;	/* micro-tile width */
	unsigned int burst_size;
	int ret;

	ret = dpu_prg_mod_to_mt_w(prg, modifier, bits_per_pixel, &mt_w);
	if (ret)
		return false;

	if (modifier) {
		x_offset %= mt_w;

		/* consider x offset to calculate stride */
		baddr += (x_offset * (bits_per_pixel / 8));
	}

	burst_size = dpu_prg_burst_size_fixup(baddr);

	stride = dpu_prg_stride_fixup(stride, burst_size, baddr, modifier);

	if (stride > DPU_PRG_MAX_STRIDE)
		return false;

	return true;
}

void dpu_prg_set_auxiliary(struct dpu_prg *prg)
{
	prg->is_auxiliary = true;
}

void dpu_prg_set_primary(struct dpu_prg *prg)
{
	prg->is_auxiliary = false;
}

struct dpu_prg *
dpu_prg_lookup_by_phandle(struct device *dev, const char *name, int index)
{
	struct device_node *prg_node = of_parse_phandle(dev->of_node,
							name, index);
	struct dpu_prg *prg;

	mutex_lock(&dpu_prg_list_mutex);
	list_for_each_entry(prg, &dpu_prg_list, list) {
		if (prg_node == prg->dev->of_node) {
			mutex_unlock(&dpu_prg_list_mutex);
			device_link_add(dev, prg->dev,
					DL_FLAG_PM_RUNTIME |
					DL_FLAG_AUTOREMOVE_CONSUMER);
			return prg;
		}
	}
	mutex_unlock(&dpu_prg_list_mutex);

	return NULL;
}

static const struct of_device_id dpu_prg_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-prg", },
	{ .compatible = "fsl,imx8qxp-prg", },
	{ /* sentinel */ },
};

static int dpu_prg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct dpu_prg *prg;
	int ret;

	prg = devm_kzalloc(dev, sizeof(*prg), GFP_KERNEL);
	if (!prg)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	prg->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(prg->base))
		return PTR_ERR(prg->base);

	prg->clk_apb = devm_clk_get(dev, "apb");
	if (IS_ERR(prg->clk_apb)) {
		ret = PTR_ERR(prg->clk_apb);
		dev_err_probe(dev, ret, "failed to get apb clock\n");
		return ret;
	}

	prg->clk_rtram = devm_clk_get(dev, "rtram");
	if (IS_ERR(prg->clk_rtram)) {
		ret = PTR_ERR(prg->clk_rtram);
		dev_err_probe(dev, ret, "failed to get rtram clock\n");
		return ret;
	}

	prg->dev = dev;
	platform_set_drvdata(pdev, prg);

	pm_runtime_enable(dev);

	mutex_lock(&dpu_prg_list_mutex);
	list_add(&prg->list, &dpu_prg_list);
	mutex_unlock(&dpu_prg_list_mutex);

	return 0;
}

static int dpu_prg_remove(struct platform_device *pdev)
{
	struct dpu_prg *prg = platform_get_drvdata(pdev);

	mutex_lock(&dpu_prg_list_mutex);
	list_del(&prg->list);
	mutex_unlock(&dpu_prg_list_mutex);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused dpu_prg_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_prg *prg = platform_get_drvdata(pdev);

	clk_disable_unprepare(prg->clk_rtram);
	clk_disable_unprepare(prg->clk_apb);

	return 0;
}

static int __maybe_unused dpu_prg_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_prg *prg = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(prg->clk_apb);
	if (ret) {
		dev_err(dev, "failed to enable apb clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(prg->clk_rtram);
	if (ret) {
		dev_err(dev, "failed to enable rtramclock: %d\n", ret);
		return ret;
	}

	dpu_prg_reset(prg);

	return ret;
}

static const struct dev_pm_ops dpu_prg_pm_ops = {
	SET_RUNTIME_PM_OPS(dpu_prg_runtime_suspend,
			   dpu_prg_runtime_resume, NULL)
};

struct platform_driver dpu_prg_driver = {
	.probe = dpu_prg_probe,
	.remove = dpu_prg_remove,
	.driver = {
		.pm = &dpu_prg_pm_ops,
		.name = "dpu-prg",
		.of_match_table = dpu_prg_dt_ids,
	},
};
