// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "edp_v510.h"
#include "edp_v510_reg.h"

#define MSM_EDP_PLL_OFFSET 0x0000
#define MSM_EDP_TX0_OFFSET 0x0200
#define MSM_EDP_TX1_OFFSET 0x0600
#define MSM_EDP_PHY_OFFSET 0x0a00

struct edp_phy_clks {
	struct edp_phy *edp_phy;
	struct clk_hw edp_link_hw;
	struct clk_hw edp_pixel_hw;
};

struct edp_phy {
	void __iomem *base;
	struct edp_phy_opts *edp_opts;
	struct edp_phy_clks *edp_clks;
};

static inline u32 edp_pll_read(struct edp_phy *phy, u32 offset)
{
	offset += MSM_EDP_PLL_OFFSET;
	return readl_relaxed(phy->base + offset);
}

static inline u32 edp_tx0_read(struct edp_phy *phy, u32 offset)
{
	offset += MSM_EDP_TX0_OFFSET;
	return readl_relaxed(phy->base + offset);
}

static inline u32 edp_tx1_read(struct edp_phy *phy, u32 offset)
{
	offset += MSM_EDP_TX1_OFFSET;
	return readl_relaxed(phy->base + offset);
}

static inline u32 edp_phy_read(struct edp_phy *phy, u32 offset)
{
	offset += MSM_EDP_PHY_OFFSET;
	return readl_relaxed(phy->base + offset);
}

static inline void edp_pll_write(struct edp_phy *phy, u32 offset, u32 data)
{
	offset += MSM_EDP_PLL_OFFSET;
	writel(data, phy->base + offset);
}

static inline void edp_tx0_write(struct edp_phy *phy, u32 offset, u32 data)
{
	offset += MSM_EDP_TX0_OFFSET;
	writel(data, phy->base + offset);
}

static inline void edp_tx1_write(struct edp_phy *phy, u32 offset, u32 data)
{
	offset += MSM_EDP_TX1_OFFSET;
	writel(data, phy->base + offset);
}

static inline void edp_phy_write(struct edp_phy *phy, u32 offset, u32 data)
{
	offset += MSM_EDP_PHY_OFFSET;
	writel(data, phy->base + offset);
}

static int edp_pixel_clk_determine_rate(struct clk_hw *hw,
						struct clk_rate_request *req)
{
	switch (req->rate) {
	case 1620000000UL / 2:
	case 2160000000UL / 2:
	case 2430000000UL / 2:
	case 2700000000UL / 2:
	case 5940000000UL / 6:
		return 0;
	default:
		return -EINVAL;
	}
}

static unsigned long
edp_pixel_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct edp_phy_clks *edp_clks;
	struct edp_phy *edp_phy;
	struct edp_phy_opts *opts;

	edp_clks = container_of(hw, struct edp_phy_clks, edp_pixel_hw);
	edp_phy = edp_clks->edp_phy;
	opts = edp_phy->edp_opts;

	switch (opts->link_rate) {
	case 162000:
		return 1620000000UL / 2;
	break;
	case 216000:
		return 2160000000UL / 2;
	break;
	case 243000:
		return 2430000000UL / 2;
	break;
	case 270000:
		return 2700000000UL / 2;
	break;
	case 324000:
		return 3240000000UL / 4;
	break;
	case 432000:
		return 4320000000UL / 4;
	break;
	case 540000:
		return 5400000000UL / 4;
	break;
	case 594000:
		return 5940000000UL / 6;
	case 810000:
		return 8100000000UL / 6;
	default:
		return 0;
	}
}

static const struct clk_ops edp_pixel_clk_ops = {
	.determine_rate = edp_pixel_clk_determine_rate,
	.recalc_rate = edp_pixel_clk_recalc_rate,
};

static int edp_link_clk_determine_rate(struct clk_hw *hw,
						struct clk_rate_request *req)
{
	switch (req->rate) {
	case 162000000:
	case 216000000:
	case 243000000:
	case 270000000:
	case 324000000:
	case 432000000:
	case 540000000:
	case 594000000:
	case 810000000:
		return 0;
	default:
		return -EINVAL;
	}
}

static unsigned long
edp_link_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct edp_phy_clks *edp_clks;
	struct edp_phy *edp_phy;
	struct edp_phy_opts *opts;

	edp_clks = container_of(hw, struct edp_phy_clks, edp_link_hw);
	edp_phy = edp_clks->edp_phy;
	opts = edp_phy->edp_opts;

	switch (opts->link_rate) {
	case 162000:
	case 216000:
	case 243000:
	case 270000:
	case 324000:
	case 432000:
	case 540000:
	case 594000:
	case 810000:
		return opts->link_rate * 1000;
	default:
		return 0;
	}
}

static const struct clk_ops edp_link_clk_ops = {
	.determine_rate = edp_link_clk_determine_rate,
	.recalc_rate = edp_link_clk_recalc_rate,
};

static struct clk_hw *
edp_clks_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct edp_phy_clks *edp_clks = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= 2) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	if (idx == 0)
		return &edp_clks->edp_link_hw;

	return &edp_clks->edp_pixel_hw;
}

static void edp_clk_release_provider(void *res)
{
	of_clk_del_provider(res);
}

static int edp_phy_clks_register(struct device *dev, struct edp_phy *edp_phy)
{
	struct clk_init_data init = { };
	struct edp_phy_clks *edp_clks;
	int ret;

	edp_clks = devm_kzalloc(dev, sizeof(*edp_clks), GFP_KERNEL);
	if (!edp_clks)
		return -ENOMEM;

	edp_clks->edp_phy = edp_phy;
	edp_phy->edp_clks = edp_clks;

	init.ops = &edp_link_clk_ops;
	init.name = "edp_phy_pll_link_clk";
	edp_clks->edp_link_hw.init = &init;
	ret = devm_clk_hw_register(dev, &edp_clks->edp_link_hw);
	if (ret)
		return ret;

	init.ops = &edp_pixel_clk_ops;
	init.name = "edp_phy_pll_vco_div_clk";
	edp_clks->edp_pixel_hw.init = &init;
	ret = devm_clk_hw_register(dev, &edp_clks->edp_pixel_hw);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(dev->of_node, edp_clks_hw_get, edp_clks);
	if (ret)
		return ret;

	/*
	 * Roll a devm action because the clock provider is the child node, but
	 * the child node is not actually a device.
	 */
	ret = devm_add_action(dev, edp_clk_release_provider, dev->of_node);
	if (ret)
		edp_clk_release_provider(dev->of_node);

	return ret;
}

static void edp_phy_ssc_en(struct edp_phy *edp_phy, bool en)
{
	if (en) {
		edp_pll_write(edp_phy, 0x10, 0x01);
		edp_pll_write(edp_phy, 0x14, 0x00);
		edp_pll_write(edp_phy, 0x1c, 0x36);
		edp_pll_write(edp_phy, 0x20, 0x01);
		edp_pll_write(edp_phy, 0x24, 0x5c);
		edp_pll_write(edp_phy, 0x28, 0x08);
	} else {
		edp_pll_write(edp_phy, 0x10, 0x00);
	}
}

int msm_edp_v510_phy_enable(struct edp_phy *edp_phy)
{
	u32 status;

	edp_phy_write(edp_phy, EDP_PHY_PD_CTL, 0x7D);
	edp_pll_write(edp_phy, QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x17);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG1, 0x13);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG2, 0x24);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG3, 0x00);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG4, 0x0a);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG5, 0x26);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG6, 0x0a);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG7, 0x03);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG8, 0xB7);
	edp_phy_write(edp_phy, EDP_PHY_AUX_CFG9, 0x03);
	edp_phy_write(edp_phy, EDP_PHY_AUX_INTERRUPT_MASK, 0x1f);

	edp_phy_write(edp_phy, EDP_PHY_MODE, 0xFC);

	if (readl_poll_timeout_atomic((edp_phy->base +
				MSM_EDP_PLL_OFFSET + QSERDES_COM_CMN_STATUS),
				status, ((status & BIT(7)) > 0), 5, 100))
		DRM_ERROR("%s: refgen not ready. Status=0x%x\n", __func__, status);

	edp_tx0_write(edp_phy, TXn_LDO_CONFIG, 0x01);
	edp_tx1_write(edp_phy, TXn_LDO_CONFIG, 0x01);
	edp_tx0_write(edp_phy, TXn_LANE_MODE_1, 0x00);
	edp_tx1_write(edp_phy, TXn_LANE_MODE_1, 0x00);

	return 0;
}

static const u8 edp_hbr2_pre_emphasis[4][4] = {
	{0x08, 0x11, 0x17, 0x1B},	/* pe0, 0 db */
	{0x00, 0x0C, 0x13, 0xFF},	/* pe1, 3.5 db */
	{0x05, 0x10, 0xFF, 0xFF},	/* pe2, 6.0 db */
	{0x00, 0xFF, 0xFF, 0xFF}	/* pe3, 9.5 db */
};

static const u8 edp_hbr2_voltage_swing[4][4] = {
	{0x0A, 0x11, 0x17, 0x1F}, /* sw0, 0.4v  */
	{0x0C, 0x14, 0x1D, 0xFF}, /* sw1, 0.6 v */
	{0x15, 0x1F, 0xFF, 0xFF}, /* sw1, 0.8 v */
	{0x17, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2 v, optional */
};

void msm_edp_v510_phy_vm_pe_init(struct edp_phy *edp_phy, struct edp_phy_opts *opts)
{

	edp_phy->edp_opts = opts;

	edp_tx0_write(edp_phy, TXn_TX_DRV_LVL, edp_hbr2_voltage_swing[0][0]);
	edp_tx0_write(edp_phy, TXn_TX_EMP_POST1_LVL,
			edp_hbr2_pre_emphasis[0][0]);
	edp_tx1_write(edp_phy, TXn_TX_DRV_LVL, edp_hbr2_voltage_swing[0][0]);
	edp_tx1_write(edp_phy, TXn_TX_EMP_POST1_LVL,
			edp_hbr2_pre_emphasis[0][0]);

	edp_tx0_write(edp_phy, TXn_HIGHZ_DRVR_EN, 4);
	edp_tx0_write(edp_phy, TXn_TRANSCEIVER_BIAS_EN, 3);
	edp_tx1_write(edp_phy, TXn_HIGHZ_DRVR_EN, 7);
	edp_tx1_write(edp_phy, TXn_TRANSCEIVER_BIAS_EN, 0);
	edp_phy_write(edp_phy, EDP_PHY_CFG_1, 3);

}

void msm_edp_v510_phy_config(struct edp_phy *edp_phy, u8 v_level, u8 p_level)
{
	edp_tx0_write(edp_phy, TXn_TX_DRV_LVL,
			edp_hbr2_voltage_swing[v_level][p_level]);
	edp_tx0_write(edp_phy, TXn_TX_EMP_POST1_LVL,
			edp_hbr2_pre_emphasis[v_level][p_level]);

	edp_tx1_write(edp_phy, TXn_TX_DRV_LVL,
			edp_hbr2_voltage_swing[v_level][p_level]);
	edp_tx1_write(edp_phy, TXn_TX_EMP_POST1_LVL,
			edp_hbr2_pre_emphasis[v_level][p_level]);
}

static void edp_pll_vco_init(struct edp_phy *edp_phy)
{
	edp_phy_ssc_en(edp_phy, true);
	edp_pll_write(edp_phy, QSERDES_COM_SVS_MODE_CLK_SEL, 0x01);
	edp_pll_write(edp_phy, QSERDES_COM_SYSCLK_EN_SEL, 0x0b);
	edp_pll_write(edp_phy, QSERDES_COM_SYS_CLK_CTRL, 0x02);
	edp_pll_write(edp_phy, QSERDES_COM_CLK_ENABLE1, 0x0c);
	edp_pll_write(edp_phy, QSERDES_COM_SYSCLK_BUF_ENABLE, 0x06);
	edp_pll_write(edp_phy, QSERDES_COM_CLK_SEL, 0x30);
	edp_pll_write(edp_phy, QSERDES_COM_PLL_IVCO, 0x07);
	edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP_EN, 0x04);
	edp_pll_write(edp_phy, QSERDES_COM_PLL_CCTRL_MODE0, 0x36);
	edp_pll_write(edp_phy, QSERDES_COM_PLL_RCTRL_MODE0, 0x16);
	edp_pll_write(edp_phy, QSERDES_COM_CP_CTRL_MODE0, 0x06);
	edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00);
	edp_pll_write(edp_phy, QSERDES_COM_CMN_CONFIG, 0x02);
	edp_pll_write(edp_phy, QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x3f);
	edp_pll_write(edp_phy, QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00);
	edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE_MAP, 0x00);
	edp_pll_write(edp_phy, QSERDES_COM_BG_TIMER, 0x0a);
	edp_pll_write(edp_phy, QSERDES_COM_CORECLK_DIV_MODE0, 0x14);
	edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE_CTRL, 0x00);
	edp_pll_write(edp_phy, QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x17);
	edp_pll_write(edp_phy, QSERDES_COM_CORE_CLK_EN, 0x0f);

	switch (edp_phy->edp_opts->link_rate) {
	case 162000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x05);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x69);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x80);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x07);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x6f);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x08);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0xa0);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x03);
		break;
	case 216000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x04);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x70);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x08);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x3f);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x0b);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0x34);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x03);
		break;
	case 243000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x04);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x7e);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x09);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0xa7);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x0c);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0x5c);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x02);
		break;
	case 270000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x03);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x69);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x80);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x07);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x0f);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x0e);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0xa0);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x03);
		break;
	case 324000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x03);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x7e);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x09);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0xdf);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x10);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0x5c);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x02);
		break;
	case 432000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x01);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x70);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x08);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x7f);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x16);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0x34);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x03);
		break;
	case 540000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x01);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x8c);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x0a);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x1f);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x1c);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0x84);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x01);
		break;
	case 594000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x01);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x9a);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x0b);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0xef);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x1e);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0xac);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x00);
		break;
	case 810000:
		edp_pll_write(edp_phy, QSERDES_COM_HSCLK_SEL, 0x00);
		edp_pll_write(edp_phy, QSERDES_COM_DEC_START_MODE0, 0x69);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x80);
		edp_pll_write(edp_phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x07);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x2f);
		edp_pll_write(edp_phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x2a);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE1_MODE0, 0xa0);
		edp_pll_write(edp_phy, QSERDES_COM_VCO_TUNE2_MODE0, 0x03);
		break;
	default:
		DRM_ERROR("%s: Invalid link rate. rate=%lu\n", __func__,
					edp_phy->edp_opts->link_rate);
		break;
	}
}

static void edp_lanes_init(struct edp_phy *edp_phy)
{
	edp_tx0_write(edp_phy, TXn_TRANSCEIVER_BIAS_EN, 0x03);
	edp_tx0_write(edp_phy, TXn_CLKBUF_ENABLE, 0x0f);
	edp_tx0_write(edp_phy, TXn_RESET_TSYNC_EN, 0x03);
	edp_tx0_write(edp_phy, TXn_TRAN_DRVR_EMP_EN, 0x01);
	edp_tx0_write(edp_phy, TXn_TX_BAND, 0x4);

	edp_tx1_write(edp_phy, TXn_TRANSCEIVER_BIAS_EN, 0x03);
	edp_tx1_write(edp_phy, TXn_CLKBUF_ENABLE, 0x0f);
	edp_tx1_write(edp_phy, TXn_RESET_TSYNC_EN, 0x03);
	edp_tx1_write(edp_phy, TXn_TRAN_DRVR_EMP_EN, 0x01);
	edp_tx1_write(edp_phy, TXn_TX_BAND, 0x4);
}

static void edp_lanes_configure(struct edp_phy *edp_phy)
{
	edp_tx0_write(edp_phy, TXn_HIGHZ_DRVR_EN, 0x1f);
	edp_tx0_write(edp_phy, TXn_HIGHZ_DRVR_EN, 0x04);
	edp_tx0_write(edp_phy, TXn_TX_POL_INV, 0x00);

	edp_tx1_write(edp_phy, TXn_HIGHZ_DRVR_EN, 0x1f);
	edp_tx1_write(edp_phy, TXn_HIGHZ_DRVR_EN, 0x04);
	edp_tx1_write(edp_phy, TXn_TX_POL_INV, 0x00);

	edp_tx1_write(edp_phy, TXn_HIGHZ_DRVR_EN, 0x04);
	edp_tx1_write(edp_phy, TXn_TX_POL_INV, 0x00);

	edp_tx0_write(edp_phy, TXn_TX_DRV_LVL_OFFSET, 0x10);
	edp_tx1_write(edp_phy, TXn_TX_DRV_LVL_OFFSET, 0x10);

	edp_tx0_write(edp_phy, TXn_RES_CODE_LANE_OFFSET_TX0, 0x11);
	edp_tx0_write(edp_phy, TXn_RES_CODE_LANE_OFFSET_TX1, 0x11);

	edp_tx1_write(edp_phy, TXn_RES_CODE_LANE_OFFSET_TX0, 0x11);
	edp_tx1_write(edp_phy, TXn_RES_CODE_LANE_OFFSET_TX1, 0x11);

	edp_tx0_write(edp_phy, TXn_TX_EMP_POST1_LVL, 0x00);
	edp_tx0_write(edp_phy, TXn_TX_DRV_LVL, 0x18);
	edp_tx1_write(edp_phy, TXn_TX_EMP_POST1_LVL, 0x00);
	edp_tx1_write(edp_phy, TXn_TX_DRV_LVL, 0x18);
}

static int edp_pll_vco_configure(struct edp_phy *edp_phy)
{
	struct edp_phy_clks *edp_clks = edp_phy->edp_clks;
	u32 phy_vco_div = 0, status;
	unsigned long pixel_freq = 0;

	switch (edp_phy->edp_opts->link_rate) {
	case 162000:
		phy_vco_div = 2;
		pixel_freq = 1620000000UL / 2;
	break;
	case 216000:
		phy_vco_div = 1;
		pixel_freq = 2160000000UL / 2;
	break;
	case 243000:
		phy_vco_div = 1;
		pixel_freq = 2430000000UL / 2;
	break;
	case 270000:
		phy_vco_div = 1;
		pixel_freq = 2700000000UL / 2;
	break;
	case 324000:
		phy_vco_div = 2;
		pixel_freq = 3240000000UL / 4;
	break;
	case 432000:
		phy_vco_div = 2;
		pixel_freq = 4320000000UL / 4;
	break;
	case 540000:
		phy_vco_div = 2;
		pixel_freq = 5400000000UL / 4;
	break;
	case 594000:
		phy_vco_div = 0;
		pixel_freq = 5940000000UL / 6;
	break;
	case 810000:
		phy_vco_div = 0;
		pixel_freq = 8100000000UL / 6;
	break;
	default:
		DRM_ERROR("%s: Invalid link rate. rate=%lu\n", __func__,
					edp_phy->edp_opts->link_rate);
	break;
	}

	edp_phy_write(edp_phy, EDP_PHY_VCO_DIV, phy_vco_div);

	clk_set_rate(edp_clks->edp_link_hw.clk,
			edp_phy->edp_opts->link_rate * 1000);
	clk_set_rate(edp_clks->edp_pixel_hw.clk, pixel_freq);

	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x01);
	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x05);
	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x01);
	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x09);

	edp_pll_write(edp_phy, QSERDES_COM_RESETSM_CNTRL, 0x20);

	if (readl_poll_timeout_atomic((edp_phy->base +
			MSM_EDP_PLL_OFFSET + QSERDES_COM_C_READY_STATUS),
			status, ((status & BIT(0)) > 0), 500, 10000)) {
		DRM_ERROR("%s: PLL not locked. Status=0x%x\n", __func__, status);
		return -ETIMEDOUT;
	}

	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x19);
	edp_lanes_configure(edp_phy);
	edp_phy_write(edp_phy, EDP_PHY_CFG_1, 0x03);

	if (readl_poll_timeout_atomic((edp_phy->base +
				MSM_EDP_PHY_OFFSET + EDP_PHY_STATUS),
				status, ((status & BIT(1)) > 0), 500, 10000)) {
		DRM_ERROR("%s: PHY not ready. Status=0x%x\n", __func__, status);
		return -ETIMEDOUT;
	}

	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x18);
	udelay(2000);
	edp_phy_write(edp_phy, EDP_PHY_CFG, 0x19);

	return readl_poll_timeout_atomic((edp_phy->base +
				MSM_EDP_PLL_OFFSET + QSERDES_COM_C_READY_STATUS),
				status, ((status & BIT(0)) > 0), 500, 10000);

}

int msm_edp_v510_phy_power_on(struct edp_phy *edp_phy)
{
	int ret = 0;

	edp_pll_vco_init(edp_phy);

	edp_phy_write(edp_phy, EDP_PHY_TX0_TX1_LANE_CTL, 0x05);
	edp_phy_write(edp_phy, EDP_PHY_TX2_TX3_LANE_CTL, 0x05);

	edp_lanes_init(edp_phy);

	ret = edp_pll_vco_configure(edp_phy);

	return ret;
}

void *msm_edp_v510_phy_init(struct device *dev, void __iomem *regbase,
		struct edp_phy_opts *opts)
{
	struct edp_phy *phy = NULL;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return NULL;

	phy->base = regbase;
	phy->edp_opts = opts;
	edp_phy_clks_register(dev, phy);

	return phy;
}
