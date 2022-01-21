// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2021 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 * Copyright 2022 NXP, Abel Vesa <abel.vesa@nxp.com>
 */

#include <dt-bindings/power/imx8mm-power.h>

#include "imx-blk-ctrl.h"

static int imx8mm_vpu_power_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct imx_blk_ctrl *bc = container_of(nb, struct imx_blk_ctrl,
						 power_nb);

	if (action != GENPD_NOTIFY_ON && action != GENPD_NOTIFY_PRE_OFF)
		return NOTIFY_OK;

	/*
	 * The ADB in the VPUMIX domain has no separate reset and clock
	 * enable bits, but is ungated together with the VPU clocks. To
	 * allow the handshake with the GPC to progress we put the VPUs
	 * in reset and ungate the clocks.
	 */
	regmap_clear_bits(bc->regmap, BLK_SFT_RSTN, BIT(0) | BIT(1) | BIT(2));
	regmap_set_bits(bc->regmap, BLK_CLK_EN, BIT(0) | BIT(1) | BIT(2));

	if (action == GENPD_NOTIFY_ON) {
		/*
		 * On power up we have no software backchannel to the GPC to
		 * wait for the ADB handshake to happen, so we just delay for a
		 * bit. On power down the GPC driver waits for the handshake.
		 */
		udelay(5);

		/* set "fuse" bits to enable the VPUs */
		regmap_set_bits(bc->regmap, 0x8, 0xffffffff);
		regmap_set_bits(bc->regmap, 0xc, 0xffffffff);
		regmap_set_bits(bc->regmap, 0x10, 0xffffffff);
		regmap_set_bits(bc->regmap, 0x14, 0xffffffff);
	}

	return NOTIFY_OK;
}

static const struct imx_blk_ctrl_domain_data imx8mm_vpu_blk_ctl_domain_data[] = {
	[IMX8MM_VPUBLK_PD_G1] = {
		.name = "vpublk-g1",
		.clk_names = (const char *[]){ "g1", },
		.num_clks = 1,
		.gpc_name = "g1",
		.rst_mask = BIT(1),
		.clk_mask = BIT(1),
	},
	[IMX8MM_VPUBLK_PD_G2] = {
		.name = "vpublk-g2",
		.clk_names = (const char *[]){ "g2", },
		.num_clks = 1,
		.gpc_name = "g2",
		.rst_mask = BIT(0),
		.clk_mask = BIT(0),
	},
	[IMX8MM_VPUBLK_PD_H1] = {
		.name = "vpublk-h1",
		.clk_names = (const char *[]){ "h1", },
		.num_clks = 1,
		.gpc_name = "h1",
		.rst_mask = BIT(2),
		.clk_mask = BIT(2),
	},
};

static const struct imx_blk_ctrl_data imx8mm_vpu_blk_ctl_dev_data = {
	.max_reg = 0x18,
	.power_notifier_fn = imx8mm_vpu_power_notifier,
	.domains = imx8mm_vpu_blk_ctl_domain_data,
	.num_domains = ARRAY_SIZE(imx8mm_vpu_blk_ctl_domain_data),
};

static int imx8mm_disp_power_notifier(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct imx_blk_ctrl *bc = container_of(nb, struct imx_blk_ctrl,
						 power_nb);

	if (action != GENPD_NOTIFY_ON && action != GENPD_NOTIFY_PRE_OFF)
		return NOTIFY_OK;

	/* Enable bus clock and deassert bus reset */
	regmap_set_bits(bc->regmap, BLK_CLK_EN, BIT(12));
	regmap_set_bits(bc->regmap, BLK_SFT_RSTN, BIT(6));

	/*
	 * On power up we have no software backchannel to the GPC to
	 * wait for the ADB handshake to happen, so we just delay for a
	 * bit. On power down the GPC driver waits for the handshake.
	 */
	if (action == GENPD_NOTIFY_ON)
		udelay(5);


	return NOTIFY_OK;
}

static const struct imx_blk_ctrl_domain_data imx8mm_disp_blk_ctl_domain_data[] = {
	[IMX8MM_DISPBLK_PD_CSI_BRIDGE] = {
		.name = "dispblk-csi-bridge",
		.clk_names = (const char *[]){ "csi-bridge-axi", "csi-bridge-apb",
					       "csi-bridge-core", },
		.num_clks = 3,
		.gpc_name = "csi-bridge",
		.rst_mask = BIT(0) | BIT(1) | BIT(2),
		.clk_mask = BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5),
	},
	[IMX8MM_DISPBLK_PD_LCDIF] = {
		.name = "dispblk-lcdif",
		.clk_names = (const char *[]){ "lcdif-axi", "lcdif-apb", "lcdif-pix", },
		.num_clks = 3,
		.gpc_name = "lcdif",
		.clk_mask = BIT(6) | BIT(7),
	},
	[IMX8MM_DISPBLK_PD_MIPI_DSI] = {
		.name = "dispblk-mipi-dsi",
		.clk_names = (const char *[]){ "dsi-pclk", "dsi-ref", },
		.num_clks = 2,
		.gpc_name = "mipi-dsi",
		.rst_mask = BIT(5),
		.clk_mask = BIT(8) | BIT(9),
		.mipi_phy_rst_mask = BIT(17),
	},
	[IMX8MM_DISPBLK_PD_MIPI_CSI] = {
		.name = "dispblk-mipi-csi",
		.clk_names = (const char *[]){ "csi-aclk", "csi-pclk" },
		.num_clks = 2,
		.gpc_name = "mipi-csi",
		.rst_mask = BIT(3) | BIT(4),
		.clk_mask = BIT(10) | BIT(11),
		.mipi_phy_rst_mask = BIT(16),
	},
};

static const struct imx_blk_ctrl_data imx8mm_disp_blk_ctl_dev_data = {
	.max_reg = 0x2c,
	.power_notifier_fn = imx8mm_disp_power_notifier,
	.domains = imx8mm_disp_blk_ctl_domain_data,
	.num_domains = ARRAY_SIZE(imx8mm_disp_blk_ctl_domain_data),
};

static const struct of_device_id imx8mm_blk_ctrl_of_match[] = {
	{
		.compatible = "fsl,imx8mm-vpu-blk-ctrl",
		.data = &imx8mm_vpu_blk_ctl_dev_data
	}, {
		.compatible = "fsl,imx8mm-disp-blk-ctrl",
		.data = &imx8mm_disp_blk_ctl_dev_data
	}, {
		/* Sentinel */
	}
};
MODULE_DEVICE_TABLE(of, imx8mm_blk_ctrl_of_match);

static struct platform_driver imx8mm_blk_ctrl_driver = {
	.probe = imx_blk_ctrl_probe,
	.remove = imx_blk_ctrl_remove,
	.driver = {
		.name = "imx8mm-blk-ctrl",
		.pm = &imx_blk_ctrl_pm_ops,
		.of_match_table = imx8mm_blk_ctrl_of_match,
	},
};
module_platform_driver(imx8mm_blk_ctrl_driver);
