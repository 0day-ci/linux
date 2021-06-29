// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 NXP
 */

#include <dt-bindings/clock/imx8mm-clock.h>
#include <dt-bindings/power/imx8mm-power.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>

#include "blk-ctl.h"

#define MEDIA_BLK_BUS_RSTN_BLK_SYNC_SFT_EN			BIT(6)
#define MEDIA_BLK_MIPI_DSI_I_PRESETN_SFT_EN			BIT(5)
#define MEDIA_BLK_MIPI_CSI_I_PRESETN_SFT_EN			BIT(4)
#define MEDIA_BLK_CAMERA_PIXEL_RESET_N_SFT_EN			BIT(3)
#define MEDIA_BLK_CSI_BRIDGE_SFT_EN				GENMASK(2, 0)

#define MEDIA_BLK_BUS_PD_MASK					BIT(12)
#define MEDIA_BLK_MIPI_CSI_PD_MASK				GENMASK(11, 10)
#define MEDIA_BLK_MIPI_DSI_PD_MASK				GENMASK(9, 8)
#define MEDIA_BLK_LCDIF_PD_MASK					GENMASK(7, 6)
#define MEDIA_BLK_CSI_BRIDGE_PD_MASK				GENMASK(5, 0)

static struct imx_blk_ctl_hw imx8mm_dispmix_blk_ctl_pds[] = {
	IMX_BLK_CTL_PD("CSI_BRIDGE", NULL, IMX8MM_BLK_CTL_PD_DISPMIX_CSI_BRIDGE, 0x4,
		       MEDIA_BLK_CSI_BRIDGE_PD_MASK, 0, MEDIA_BLK_CSI_BRIDGE_SFT_EN,
		       IMX_BLK_CTL_PD_RESET),
	IMX_BLK_CTL_PD("LCDIF", NULL, IMX8MM_BLK_CTL_PD_DISPMIX_LCDIF, 0x4,
		       MEDIA_BLK_LCDIF_PD_MASK, -1, -1, 0),
	IMX_BLK_CTL_PD("MIPI_DSI", "mipi", IMX8MM_BLK_CTL_PD_DISPMIX_MIPI_DSI, 0x4,
		       MEDIA_BLK_MIPI_DSI_PD_MASK, 0, MEDIA_BLK_MIPI_DSI_I_PRESETN_SFT_EN,
		       IMX_BLK_CTL_PD_RESET),
	IMX_BLK_CTL_PD("MIPI_CSI", "mipi", IMX8MM_BLK_CTL_PD_DISPMIX_MIPI_CSI, 0x4,
		       MEDIA_BLK_MIPI_CSI_PD_MASK, 0,
		       MEDIA_BLK_MIPI_CSI_I_PRESETN_SFT_EN | MEDIA_BLK_CAMERA_PIXEL_RESET_N_SFT_EN,
		       IMX_BLK_CTL_PD_RESET),
	IMX_BLK_CTL_PD("DISPMIX_BUS", "dispmix", IMX8MM_BLK_CTL_PD_DISPMIX_BUS, 0x4,
		       MEDIA_BLK_BUS_PD_MASK, 0, MEDIA_BLK_BUS_RSTN_BLK_SYNC_SFT_EN,
		       IMX_BLK_CTL_PD_HANDSHAKE | IMX_BLK_CTL_PD_RESET)
};

static struct imx_blk_ctl_hw imx8mm_vpumix_blk_ctl_pds[] = {
	IMX_BLK_CTL_PD("VPU_BLK_CTL_G2", "vpu-g2", IMX8MM_BLK_CTL_PD_VPU_G2, 0x4,
		       BIT(0), 0, BIT(0), IMX_BLK_CTL_PD_RESET),
	IMX_BLK_CTL_PD("VPU_BLK_CTL_G1", "vpu-g1", IMX8MM_BLK_CTL_PD_VPU_G1, 0x4,
		       BIT(1), 0, BIT(1), IMX_BLK_CTL_PD_RESET),
	IMX_BLK_CTL_PD("VPU_BLK_CTL_H1", "vpu-h1", IMX8MM_BLK_CTL_PD_VPU_H1, 0x4,
		       BIT(2), 0, BIT(2), IMX_BLK_CTL_PD_RESET),
	IMX_BLK_CTL_PD("VPU_BLK_CTL_BUS", "vpumix", IMX8MM_BLK_CTL_PD_VPU_BUS, 0x4,
		       BIT(2), 0, BIT(2), IMX_BLK_CTL_PD_HANDSHAKE | IMX_BLK_CTL_PD_RESET)
};

static const struct regmap_config imx8mm_blk_ctl_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x30,
	.fast_io		= true,
};

static const struct imx_blk_ctl_dev_data imx8mm_vpumix_blk_ctl_dev_data = {
	.pds = imx8mm_vpumix_blk_ctl_pds,
	.pds_num = ARRAY_SIZE(imx8mm_vpumix_blk_ctl_pds),
	.max_num = IMX8MM_BLK_CTL_PD_VPU_MAX,
	.hw_hsk = &imx8mm_vpumix_blk_ctl_pds[3],
	.config = imx8mm_blk_ctl_regmap_config,
	.name = "imx-vpumix-blk-ctl",
};

static const struct imx_blk_ctl_dev_data imx8mm_dispmix_blk_ctl_dev_data = {
	.pds = imx8mm_dispmix_blk_ctl_pds,
	.pds_num = ARRAY_SIZE(imx8mm_dispmix_blk_ctl_pds),
	.max_num = IMX8MM_BLK_CTL_PD_DISPMIX_MAX,
	.hw_hsk = &imx8mm_dispmix_blk_ctl_pds[4],
	.config = imx8mm_blk_ctl_regmap_config,
	.name = "imx-dispmix-blk-ctl",
};

static int imx8mm_blk_ctl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct imx_blk_ctl_dev_data *dev_data = of_device_get_match_data(dev);
	struct regmap *regmap;
	struct imx_blk_ctl *ctl;
	void __iomem *base;

	ctl = devm_kzalloc(dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &dev_data->config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ctl->regmap = regmap;
	ctl->dev = dev;
	mutex_init(&ctl->lock);

	ctl->num_clks = devm_clk_bulk_get_all(dev, &ctl->clks);
	if (ctl->num_clks < 0)
		return ctl->num_clks;

	dev_set_drvdata(dev, ctl);
	ctl->dev_data = dev_data;

	return imx_blk_ctl_register(dev);
}

static const struct of_device_id imx_blk_ctl_of_match[] = {
	{ .compatible = "fsl,imx8mm-vpumix-blk-ctl", .data = &imx8mm_vpumix_blk_ctl_dev_data },
	{ .compatible = "fsl,imx8mm-dispmix-blk-ctl", .data = &imx8mm_dispmix_blk_ctl_dev_data },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_blk_ctl_of_match);

static struct platform_driver imx_blk_ctl_driver = {
	.probe = imx8mm_blk_ctl_probe,
	.driver = {
		.name = "imx8mm-blk-ctl",
		.of_match_table = of_match_ptr(imx_blk_ctl_of_match),
		.pm = &imx_blk_ctl_pm_ops,
	},
};
module_platform_driver(imx_blk_ctl_driver);
