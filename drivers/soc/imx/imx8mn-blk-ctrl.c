// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2021 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 * Copyright 2022 NXP, Abel Vesa <abel.vesa@nxp.com>
 */

#include <dt-bindings/power/imx8mn-power.h>

#include "imx-blk-ctrl.h"

static int imx8mn_disp_power_notifier(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct imx_blk_ctrl *bc = container_of(nb, struct imx_blk_ctrl,
						 power_nb);

	if (action != GENPD_NOTIFY_ON && action != GENPD_NOTIFY_PRE_OFF)
		return NOTIFY_OK;

	/* Enable bus clock and deassert bus reset */
	regmap_set_bits(bc->regmap, BLK_CLK_EN, BIT(8));
	regmap_set_bits(bc->regmap, BLK_SFT_RSTN, BIT(8));

	/*
	 * On power up we have no software backchannel to the GPC to
	 * wait for the ADB handshake to happen, so we just delay for a
	 * bit. On power down the GPC driver waits for the handshake.
	 */
	if (action == GENPD_NOTIFY_ON)
		udelay(5);


	return NOTIFY_OK;
}

static const struct imx_blk_ctrl_domain_data imx8mn_disp_blk_ctl_domain_data[] = {
	[IMX8MN_DISPBLK_PD_MIPI_DSI] = {
		.name = "dispblk-mipi-dsi",
		.clk_names = (const char *[]){ "dsi-pclk", "dsi-ref", },
		.num_clks = 2,
		.gpc_name = "mipi-dsi",
		.rst_mask = BIT(0) | BIT(1),
		.clk_mask = BIT(0) | BIT(1),
		.mipi_phy_rst_mask = BIT(17),
	},
	[IMX8MN_DISPBLK_PD_MIPI_CSI] = {
		.name = "dispblk-mipi-csi",
		.clk_names = (const char *[]){ "csi-aclk", "csi-pclk" },
		.num_clks = 2,
		.gpc_name = "mipi-csi",
		.rst_mask = BIT(2) | BIT(3),
		.clk_mask = BIT(2) | BIT(3),
		.mipi_phy_rst_mask = BIT(16),
	},
	[IMX8MN_DISPBLK_PD_LCDIF] = {
		.name = "dispblk-lcdif",
		.clk_names = (const char *[]){ "lcdif-axi", "lcdif-apb", "lcdif-pix", },
		.num_clks = 3,
		.gpc_name = "lcdif",
		.rst_mask = BIT(4) | BIT(5),
		.clk_mask = BIT(4) | BIT(5),
	},
	[IMX8MN_DISPBLK_PD_ISI] = {
		.name = "dispblk-isi",
		.clk_names = (const char *[]){ "disp_axi", "disp_apb", "disp_axi_root",
						"disp_apb_root"},
		.num_clks = 4,
		.gpc_name = "isi",
		.rst_mask = BIT(6) | BIT(7),
		.clk_mask = BIT(6) | BIT(7),
	},
};

static const struct imx_blk_ctrl_data imx8mn_disp_blk_ctl_dev_data = {
	.max_reg = 0x84,
	.power_notifier_fn = imx8mn_disp_power_notifier,
	.domains = imx8mn_disp_blk_ctl_domain_data,
	.num_domains = ARRAY_SIZE(imx8mn_disp_blk_ctl_domain_data),
};

static const struct of_device_id imx8mn_blk_ctrl_of_match[] = {
	{
		.compatible = "fsl,imx8mn-disp-blk-ctrl",
		.data = &imx8mn_disp_blk_ctl_dev_data
	}, {
		/* Sentinel */
	}
};
MODULE_DEVICE_TABLE(of, imx8mn_blk_ctrl_of_match);

static struct platform_driver imx8mn_blk_ctrl_driver = {
	.probe = imx_blk_ctrl_probe,
	.remove = imx_blk_ctrl_remove,
	.driver = {
		.name = "imx8mn-blk-ctrl",
		.pm = &imx_blk_ctrl_pm_ops,
		.of_match_table = imx8mn_blk_ctrl_of_match,
	},
};
module_platform_driver(imx8mn_blk_ctrl_driver);
