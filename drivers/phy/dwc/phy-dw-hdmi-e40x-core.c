// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI PHYs e405 and e406 driver
 *
 * Author: Jose Abreu <jose.abreu@synopsys.com>
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/phy/phy.h>
#include <linux/phy/dwc/dw-hdmi-phy-pdata.h>

#include "phy-dw-hdmi-e40x.h"

void dw_phy_write(struct dw_phy_dev *dw_dev, u16 val, u16 addr)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	funcs->write(arg, val, addr);
}

u16 dw_phy_read(struct dw_phy_dev *dw_dev, u16 addr)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	return funcs->read(arg, addr);
}

static void dw_phy_reset(struct dw_phy_dev *dw_dev, int enable)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	funcs->reset(arg, enable);
}

void dw_phy_pddq(struct dw_phy_dev *dw_dev, int enable)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	funcs->pddq(arg, enable);
}

static void dw_phy_svsmode(struct dw_phy_dev *dw_dev, int enable)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	funcs->svsmode(arg, enable);
}

static void dw_phy_zcal_reset(struct dw_phy_dev *dw_dev)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	funcs->zcal_reset(arg);
}

static bool dw_phy_zcal_done(struct dw_phy_dev *dw_dev)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	return funcs->zcal_done(arg);
}

bool dw_phy_tmds_valid(struct dw_phy_dev *dw_dev)
{
	const struct dw_phy_funcs *funcs = dw_dev->config->funcs;
	void *arg = dw_dev->config->funcs_arg;

	return funcs->tmds_valid(arg);
}

static int dw_phy_color_depth_to_mode(u8 color_depth)
{
	int sc_clrdep = 0;

	switch (color_depth) {
	case 24:
		sc_clrdep = DW_PHY_CLRDEP_8BIT_MODE;
		break;
	case 30:
		sc_clrdep = DW_PHY_CLRDEP_10BIT_MODE;
		break;
	case 36:
		sc_clrdep = DW_PHY_CLRDEP_12BIT_MODE;
		break;
	case 48:
		sc_clrdep = DW_PHY_CLRDEP_16BIT_MODE;
		break;
	default:
		return -EINVAL;
	}

	return sc_clrdep;
}

static int dw_phy_config(struct dw_phy_dev *dw_dev, u8 color_depth,
			 bool hdmi2, bool scrambling)
{
	const struct dw_phy_mpll_config *mpll_cfg = dw_dev->phy_data->mpll_cfg;
	struct dw_phy_pdata *phy = dw_dev->config;
	struct device *dev = dw_dev->dev;
	u16 val, sc_clrdep;
	int timeout = 100;
	bool zcal_done;
	int ret;

	dev_dbg(dev, "%s: color_depth=%d, hdmi2=%d, scrambling=%d, cfg_clk=%d\n",
		__func__, color_depth, hdmi2, scrambling, phy->cfg_clk);

	ret = dw_phy_color_depth_to_mode(color_depth);
	if (ret < 0)
		return ret;

	sc_clrdep = ret;

	dw_phy_reset(dw_dev, 1);
	dw_phy_pddq(dw_dev, 1);
	dw_phy_svsmode(dw_dev, 1);

#if IS_ENABLED(CONFIG_VIDEO_DWC_HDMI_PHY_E40X_SUPPORT_TESTCHIP)
	dw_phy_zcal_reset(dw_dev);
	do {
		usleep_range(1000, 1100);
		zcal_done = dw_phy_zcal_done(dw_dev);
	} while (!zcal_done && timeout--);

	if (!zcal_done) {
		dev_err(dw_dev->dev, "Zcal calibration failed\n");
		return -ETIMEDOUT;
	}
#endif /* CONFIG_VIDEO_DWC_HDMI_PHY_E40X_SUPPORT_TESTCHIP */

	dw_phy_reset(dw_dev, 0);

	/* CMU */
	val = DW_PHY_LOCK_THRES(0x08) & DW_PHY_LOCK_THRES_MASK;
	val |= DW_PHY_TIMEBASE_OVR_EN;
	val |= DW_PHY_TIMEBASE_OVR(phy->cfg_clk * 4) & DW_PHY_TIMEBASE_OVR_MASK;
	dw_phy_write(dw_dev, val, DW_PHY_CMU_CONFIG);

	/* Color Depth and enable fast switching */
	val = dw_phy_read(dw_dev, DW_PHY_SYSTEM_CONFIG);
	val &= ~DW_PHY_CLRDEP_MASK;
	val |= sc_clrdep | DW_PHY_FAST_SWITCHING;
	dw_phy_write(dw_dev, val, DW_PHY_SYSTEM_CONFIG);

	/* MPLL */
	for (; mpll_cfg->addr != 0x0; mpll_cfg++)
		dw_phy_write(dw_dev, mpll_cfg->val, mpll_cfg->addr);

	/* Operation for data rates between 3.4Gbps and 6Gbps */
	val = dw_phy_read(dw_dev, DW_PHY_CDR_CTRL_CNT);
	val &= ~DW_PHY_HDMI_MHL_MODE_MASK;
	if (hdmi2)
		val |= DW_PHY_HDMI_MHL_MODE_ABOVE_3_4G_BITPS;
	else
		val |= DW_PHY_HDMI_MHL_MODE_BELOW_3_4G_BITPS;
	dw_phy_write(dw_dev, val, DW_PHY_CDR_CTRL_CNT);

	/* Scrambling */
	val = dw_phy_read(dw_dev, DW_PHY_OVL_PROT_CTRL);
	if (scrambling)
		val |= DW_PHY_SCRAMBLING_EN_OVR |
			DW_PHY_SCRAMBLING_EN_OVR_EN;
	else
		val &= ~(DW_PHY_SCRAMBLING_EN_OVR |
			DW_PHY_SCRAMBLING_EN_OVR_EN);
	dw_phy_write(dw_dev, val, DW_PHY_OVL_PROT_CTRL);

	/* Enable PHY */
	dw_phy_pddq(dw_dev, 0);

	dw_dev->color_depth = color_depth;
	dw_dev->hdmi2 = hdmi2;
	dw_dev->scrambling = scrambling;
	return 0;
}

static int dw_phy_enable(struct dw_phy_dev *dw_dev, unsigned char color_depth,
			 bool hdmi2, bool scrambling)
{
	int ret;

	ret = dw_phy_config(dw_dev, color_depth, hdmi2, scrambling);
	if (ret)
		return ret;

	dw_phy_reset(dw_dev, 0);
	dw_phy_pddq(dw_dev, 0);
	dw_dev->phy_enabled = true;
	return 0;
}

static void dw_phy_disable(struct dw_phy_dev *dw_dev)
{
	if (!dw_dev->phy_enabled)
		return;

	dw_phy_reset(dw_dev, 1);
	dw_phy_pddq(dw_dev, 1);
	dw_phy_svsmode(dw_dev, 0);
	dw_dev->mpll_status = 0xFFFF;
	dw_dev->phy_enabled = false;
}

static int dw_phy_set_color_depth(struct dw_phy_dev *dw_dev,
				  u8 color_depth)
{
	u16 val, sc_clrdep;
	int ret;

	if (!dw_dev->phy_enabled)
		return -EINVAL;

	ret = dw_phy_color_depth_to_mode(color_depth);
	if (ret < 0)
		return ret;

	sc_clrdep = ret;

	/* Color Depth */
	val = dw_phy_read(dw_dev, DW_PHY_SYSTEM_CONFIG);
	val &= ~DW_PHY_CLRDEP_MASK;
	val |= sc_clrdep;
	dw_phy_write(dw_dev, val, DW_PHY_SYSTEM_CONFIG);

	dev_dbg(dw_dev->dev, "%s: color_depth=%d\n", __func__, color_depth);

	return 0;
}

static bool dw_phy_has_dt(struct dw_phy_dev *dw_dev)
{
	return (of_device_get_match_data(dw_dev->dev) != NULL);
}

static int dw_phy_parse_dt(struct dw_phy_dev *dw_dev)
{
	const struct dw_hdmi_phy_data *of_data;
	int ret;

	of_data = of_device_get_match_data(dw_dev->dev);
	if (!of_data) {
		dev_err(dw_dev->dev, "no valid PHY configuration available\n");
		return -EINVAL;
	}

	/* load PHY version */
	dw_dev->config->version = of_data->version;

	/* load PHY clock */
	dw_dev->clk = devm_clk_get(dw_dev->dev, "cfg");
	if (IS_ERR(dw_dev->clk)) {
		dev_err(dw_dev->dev, "failed to get cfg clock\n");
		return PTR_ERR(dw_dev->clk);
	}

	ret = clk_prepare_enable(dw_dev->clk);
	if (ret) {
		dev_err(dw_dev->dev, "failed to enable cfg clock\n");
		return ret;
	}

	dw_dev->config->cfg_clk = clk_get_rate(dw_dev->clk) / 1000000U;
	if (!dw_dev->config->cfg_clk) {
		dev_err(dw_dev->dev, "invalid cfg clock frequency\n");
		ret = -EINVAL;
		goto err_clk;
	}

	return 0;

err_clk:
	clk_disable_unprepare(dw_dev->clk);
	return ret;
}

static int dw_phy_parse_pd(struct dw_phy_dev *dw_dev)
{
	/* validate if the platform data was properly supplied */

	if (!dw_dev->config->version) {
		dev_err(dw_dev->dev,
			"invalid version platform data supplied\n");
		return -EINVAL;
	}

	if (!dw_dev->config->cfg_clk) {
		dev_err(dw_dev->dev, "invalid clock platform data supplied\n");
		return -EINVAL;
	}

	return 0;
}

static int dw_phy_set_data(struct dw_phy_dev *dw_dev)
{
	const struct dw_hdmi_phy_data *of_data;

	of_data = of_device_get_match_data(dw_dev->dev);

	if (of_data) {
		dw_dev->phy_data = (struct dw_hdmi_phy_data *)of_data;
	} else if (dw_dev->config->version == dw_phy_e405_data.version) {
		dw_dev->phy_data = &dw_phy_e405_data;
	} else if (dw_dev->config->version == dw_phy_e406_data.version) {
		dw_dev->phy_data = &dw_phy_e406_data;
	} else {
		dev_err(dw_dev->dev, "failed setting PHY data\n");
		return -EINVAL;
	}

	return 0;
}

static const char *dw_phy_lookup_dev_id(struct dw_phy_dev *dw_dev)
{
	const char *dev_id = "dw-hdmi-rx";

	/* The lookup dev_id name by default is "dw-hdmi-rx",
	 * however if there is a parent device associated then
	 * the dev_id will be overridden by that dev_name of parent.
	 * This allows other drivers to re-use the same API PHY.
	 */
	if (dw_dev->dev->parent)
		dev_id = dev_name(dw_dev->dev->parent);

	return dev_id;
}

static int dw_hdmi_phy_calibrate(struct phy *phy)
{
	struct dw_phy_dev *dw_dev = phy_get_drvdata(phy);
	const struct phy_configure_opts_hdmi *hdmi_opts = &dw_dev->hdmi_opts;

	/* call the equalization function for calibration */
	return dw_dev->phy_data->dw_phy_eq_init(dw_dev,
						hdmi_opts->calibration_acq,
						hdmi_opts->calibration_force);
}

static int dw_hdmi_phy_power_on(struct phy *phy)
{
	struct dw_phy_dev *dw_dev = phy_get_drvdata(phy);

	return dw_phy_enable(dw_dev, dw_dev->hdmi_opts.color_depth,
			     dw_dev->hdmi_opts.tmds_bit_clock_ratio,
			     dw_dev->hdmi_opts.scrambling);
}

static int dw_hdmi_phy_power_off(struct phy *phy)
{
	struct dw_phy_dev *dw_dev = phy_get_drvdata(phy);

	dw_phy_disable(dw_dev);

	return 0;
}

static int dw_hdmi_phy_configure(struct phy *phy,
				 union phy_configure_opts *opts)
{
	const struct phy_configure_opts_hdmi *hdmi_opts = &opts->hdmi;
	struct dw_phy_dev *dw_dev = phy_get_drvdata(phy);
	int ret = 0;

	/* save the configuration options */
	memcpy(&dw_dev->hdmi_opts, hdmi_opts, sizeof(*hdmi_opts));

	/* check if it is needed to reconfigure deep_color */
	if (dw_dev->hdmi_opts.set_color_depth) {
		if (dw_dev->phy_enabled) {
			ret = dw_phy_set_color_depth(dw_dev,
						     hdmi_opts->color_depth);
			if (!ret)
				dw_dev->hdmi_opts.set_color_depth = 0;
		}
	}

	return ret;
}

static const struct phy_ops dw_hdmi_phy_ops = {
	.configure	= dw_hdmi_phy_configure,
	.power_on	= dw_hdmi_phy_power_on,
	.calibrate	= dw_hdmi_phy_calibrate,
	.power_off	= dw_hdmi_phy_power_off,
	.owner		= THIS_MODULE,
};

static int dw_phy_probe(struct platform_device *pdev)
{
	struct dw_phy_pdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct dw_phy_dev *dw_dev;
	int ret;

	dev_dbg(dev, "probe start\n");

	dw_dev = devm_kzalloc(dev, sizeof(*dw_dev), GFP_KERNEL);
	if (!dw_dev)
		return -ENOMEM;

	if (!pdata) {
		dev_err(dev, "no platform data supplied\n");
		return -EINVAL;
	}

	dw_dev->dev = dev;
	dw_dev->config = pdata;

	/* parse configuration */
	if (dw_phy_has_dt(dw_dev)) {
		/* configuration based on device tree */
		ret = dw_phy_parse_dt(dw_dev);
	} else {
		/* configuration based on platform device */
		ret = dw_phy_parse_pd(dw_dev);
	}
	if (ret)
		goto err;

	/* set phy_data depending on the PHY type */
	ret = dw_phy_set_data(dw_dev);
	if (ret)
		goto err;

	/* Force PHY disabling */
	dw_dev->phy_enabled = true;
	dw_phy_disable(dw_dev);

	/* creates the PHY reference */
	dw_dev->phy = devm_phy_create(dw_dev->dev, node, &dw_hdmi_phy_ops);
	if (IS_ERR(dw_dev->phy)) {
		dev_err(dw_dev->dev, "Failed to create HDMI PHY reference\n");
		return PTR_ERR(dw_dev->phy);
	}

	platform_set_drvdata(pdev, dw_dev);
	phy_set_drvdata(dw_dev->phy, dw_dev);

	/* create the lookup association for non-dt systems */
	if (!node) {
		ret = phy_create_lookup(dw_dev->phy, "hdmi-phy",
					dw_phy_lookup_dev_id(dw_dev));
		if (ret) {
			dev_err(dev, "Failed to create HDMI PHY lookup\n");
			goto err;
		}
		dev_dbg(dev,
			"phy_create_lookup: con_id='%s' <-> dev_id='%s')\n",
			"hdmi-phy", dw_phy_lookup_dev_id(dw_dev));
	}

	dev_dbg(dev, "driver probed (name=e%d, cfg clock=%d, dev_name=%s)\n",
		dw_dev->config->version, dw_dev->config->cfg_clk,
		dev_name(dw_dev->dev));
	return 0;

err:
	if (dw_dev->clk)
		clk_disable_unprepare(dw_dev->clk);

	return ret;
}

static int dw_phy_remove(struct platform_device *pdev)
{
	struct dw_phy_dev *dw_dev = platform_get_drvdata(pdev);

	phy_remove_lookup(dw_dev->phy, "hdmi-phy",
			  dw_phy_lookup_dev_id(dw_dev));

	if (dw_dev->clk)
		clk_disable_unprepare(dw_dev->clk);

	return 0;
}

static const struct of_device_id dw_hdmi_phy_e40x_id[] = {
	{ .compatible = "snps,dw-hdmi-phy-e405", .data = &dw_phy_e405_data, },
	{ .compatible = "snps,dw-hdmi-phy-e406", .data = &dw_phy_e406_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, dw_hdmi_phy_e40x_id);

static struct platform_driver dw_phy_e40x_driver = {
	.probe = dw_phy_probe,
	.remove = dw_phy_remove,
	.driver = {
		.name = DW_PHY_E40X_DRVNAME,
		.of_match_table = dw_hdmi_phy_e40x_id,
	}
};
module_platform_driver(dw_phy_e40x_driver);

MODULE_AUTHOR("Jose Abreu <jose.abreu@synopsys.com>");
MODULE_AUTHOR("Nelson Costa <nelson.costa@synopsys.com>");
MODULE_DESCRIPTION("DesignWare HDMI PHYs e405 and e406 driver");
MODULE_LICENSE("GPL");
