// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2021 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 * Copyright 2022 NXP, Abel Vesa <abel.vesa@nxp.com>
 */

#ifndef __IMX_BLK_CTRL_H
#define __IMX_BLK_CTRL_H

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#define BLK_SFT_RSTN	0x0
#define BLK_CLK_EN	0x4
#define BLK_MIPI_RESET_DIV	0x8 /* Mini/Nano DISPLAY_BLK_CTRL only */

struct imx_blk_ctrl_domain;

struct imx_blk_ctrl {
	struct device *dev;
	struct notifier_block power_nb;
	struct device *bus_power_dev;
	struct regmap *regmap;
	struct imx_blk_ctrl_domain *domains;
	struct genpd_onecell_data onecell_data;
};

struct imx_blk_ctrl_domain_data {
	const char *name;
	const char * const *clk_names;
	int num_clks;
	const char *gpc_name;
	u32 rst_mask;
	u32 clk_mask;

	/*
	 * i.MX8M Mini and Nano have a third DISPLAY_BLK_CTRL register
	 * which is used to control the reset for the MIPI Phy.
	 * Since it's only present in certain circumstances,
	 * an if-statement should be used before setting and clearing this
	 * register.
	 */
	u32 mipi_phy_rst_mask;
};

#define DOMAIN_MAX_CLKS 3

struct imx_blk_ctrl_domain {
	struct generic_pm_domain genpd;
	const struct imx_blk_ctrl_domain_data *data;
	struct clk_bulk_data clks[DOMAIN_MAX_CLKS];
	struct device *power_dev;
	struct imx_blk_ctrl *bc;
};

struct imx_blk_ctrl_data {
	int max_reg;
	notifier_fn_t power_notifier_fn;
	const struct imx_blk_ctrl_domain_data *domains;
	int num_domains;
};

extern const struct dev_pm_ops imx_blk_ctrl_pm_ops;

int imx_blk_ctrl_remove(struct platform_device *pdev);
int imx_blk_ctrl_probe(struct platform_device *pdev);

#endif
