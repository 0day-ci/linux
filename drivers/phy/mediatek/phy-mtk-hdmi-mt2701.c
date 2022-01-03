// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Chunhui Dai <chunhui.dai@mediatek.com>
 */

#include "phy-mtk-hdmi.h"

#define HDMI_CON0	0x00
#define RG_HDMITX_DRV_IBIAS		0
#define RG_HDMITX_DRV_IBIAS_MASK	(0x3f << 0)
#define RG_HDMITX_EN_SER		12
#define RG_HDMITX_EN_SER_MASK		(0x0f << 12)
#define RG_HDMITX_EN_SLDO		16
#define RG_HDMITX_EN_SLDO_MASK		(0x0f << 16)
#define RG_HDMITX_EN_PRED		20
#define RG_HDMITX_EN_PRED_MASK		(0x0f << 20)
#define RG_HDMITX_EN_IMP		24
#define RG_HDMITX_EN_IMP_MASK		(0x0f << 24)
#define RG_HDMITX_EN_DRV		28
#define RG_HDMITX_EN_DRV_MASK		(0x0f << 28)

#define HDMI_CON1	0x04
#define RG_HDMITX_PRED_IBIAS		18
#define RG_HDMITX_PRED_IBIAS_MASK	(0x0f << 18)
#define RG_HDMITX_PRED_IMP		(0x01 << 22)
#define RG_HDMITX_DRV_IMP		26
#define RG_HDMITX_DRV_IMP_MASK		(0x3f << 26)

#define HDMI_CON2	0x08
#define RG_HDMITX_EN_TX_CKLDO		(0x01 << 0)
#define RG_HDMITX_EN_TX_POSDIV		(0x01 << 1)
#define RG_HDMITX_TX_POSDIV		3
#define RG_HDMITX_TX_POSDIV_MASK	(0x03 << 3)
#define RG_HDMITX_EN_MBIAS		(0x01 << 6)
#define RG_HDMITX_MBIAS_LPF_EN		(0x01 << 7)

#define HDMI_CON4	0x10
#define RG_HDMITX_RESERVE_MASK		(0xffffffff << 0)

#define HDMI_CON6	0x18
#define RG_HTPLL_BR			0
#define RG_HTPLL_BR_MASK		(0x03 << 0)
#define RG_HTPLL_BC			2
#define RG_HTPLL_BC_MASK		(0x03 << 2)
#define RG_HTPLL_BP			4
#define RG_HTPLL_BP_MASK		(0x0f << 4)
#define RG_HTPLL_IR			8
#define RG_HTPLL_IR_MASK		(0x0f << 8)
#define RG_HTPLL_IC			12
#define RG_HTPLL_IC_MASK		(0x0f << 12)
#define RG_HTPLL_POSDIV			16
#define RG_HTPLL_POSDIV_MASK		(0x03 << 16)
#define RG_HTPLL_PREDIV			18
#define RG_HTPLL_PREDIV_MASK		(0x03 << 18)
#define RG_HTPLL_FBKSEL			20
#define RG_HTPLL_FBKSEL_MASK		(0x03 << 20)
#define RG_HTPLL_RLH_EN			(0x01 << 22)
#define RG_HTPLL_FBKDIV			24
#define RG_HTPLL_FBKDIV_MASK		(0x7f << 24)
#define RG_HTPLL_EN			(0x01 << 31)

#define HDMI_CON7	0x1c
#define RG_HTPLL_AUTOK_EN		(0x01 << 23)
#define RG_HTPLL_DIVEN			28
#define RG_HTPLL_DIVEN_MASK		(0x07 << 28)

static int mtk_hdmi_pll_prepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	regmap_set_bits(hdmi_phy->regmap, HDMI_CON7, RG_HTPLL_AUTOK_EN);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_RLH_EN);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_MBIAS);
	usleep_range(80, 100);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_EN);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	usleep_range(80, 100);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	usleep_range(80, 100);
	return 0;
}

static void mtk_hdmi_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	usleep_range(80, 100);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_EN);
	usleep_range(80, 100);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_MBIAS);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_RLH_EN);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON7, RG_HTPLL_AUTOK_EN);
	usleep_range(80, 100);
}

static long mtk_hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	return rate;
}

static int mtk_hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	u32 pos_div;

	if (rate <= 64000000)
		pos_div = 3;
	else if (rate <= 128000000)
		pos_div = 2;
	else
		pos_div = 1;

	regmap_set_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_PREDIV_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_TX_POSDIV);
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_IC_MASK, BIT(RG_HTPLL_IC));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_IR_MASK, BIT(RG_HTPLL_IR));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_TX_POSDIV_MASK,
			   (pos_div << RG_HDMITX_TX_POSDIV));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_FBKSEL_MASK,
			   (1 << RG_HTPLL_FBKSEL));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_FBKDIV_MASK,
			   (19 << RG_HTPLL_FBKDIV));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON7, RG_HTPLL_DIVEN_MASK,
			   (0x2 << RG_HTPLL_DIVEN));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_BP_MASK,
			   (0xc << RG_HTPLL_BP));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_BC_MASK,
			   (0x2 << RG_HTPLL_BC));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_BR_MASK, BIT(RG_HTPLL_BR));

	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON1, RG_HDMITX_PRED_IMP);
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON1, RG_HDMITX_PRED_IBIAS_MASK,
			   (0x3 << RG_HDMITX_PRED_IBIAS));
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_IMP_MASK);
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON1, RG_HDMITX_DRV_IMP_MASK,
			   (0x28 << RG_HDMITX_DRV_IMP));
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON4, RG_HDMITX_RESERVE_MASK, 0x28);
	regmap_update_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_DRV_IBIAS_MASK,
			   (0xa << RG_HDMITX_DRV_IBIAS));
	return 0;
}

static unsigned long mtk_hdmi_pll_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	unsigned long out_rate;
	u32 val;

	regmap_read(hdmi_phy->regmap, HDMI_CON6, &val);
	val = (val & RG_HTPLL_PREDIV_MASK) >> RG_HTPLL_PREDIV;
	switch (val) {
	case 0x00:
		out_rate = parent_rate;
		break;
	case 0x01:
		out_rate = parent_rate / 2;
		break;
	default:
		out_rate = parent_rate / 4;
		break;
	}

	regmap_read(hdmi_phy->regmap, HDMI_CON6, &val);
	val = (val & RG_HTPLL_FBKDIV_MASK) >> RG_HTPLL_FBKDIV;
	out_rate *= (val + 1) * 2;
	regmap_read(hdmi_phy->regmap, HDMI_CON2, &val);
	val &= RG_HDMITX_TX_POSDIV_MASK;
	out_rate >>= (val >> RG_HDMITX_TX_POSDIV);

	regmap_read(hdmi_phy->regmap, HDMI_CON2, &val);
	if (val & RG_HDMITX_EN_TX_POSDIV)
		out_rate /= 5;

	return out_rate;
}

static const struct clk_ops mtk_hdmi_phy_pll_ops = {
	.prepare = mtk_hdmi_pll_prepare,
	.unprepare = mtk_hdmi_pll_unprepare,
	.set_rate = mtk_hdmi_pll_set_rate,
	.round_rate = mtk_hdmi_pll_round_rate,
	.recalc_rate = mtk_hdmi_pll_recalc_rate,
};

static void mtk_hdmi_phy_enable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON7, RG_HTPLL_AUTOK_EN);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_RLH_EN);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_MBIAS);
	usleep_range(80, 100);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_EN);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	usleep_range(80, 100);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	regmap_set_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	usleep_range(80, 100);
}

static void mtk_hdmi_phy_disable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_DRV_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_PRED_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SER_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_MBIAS_LPF_EN);
	usleep_range(80, 100);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON0, RG_HDMITX_EN_SLDO_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_TX_CKLDO);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_EN);
	usleep_range(80, 100);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON2, RG_HDMITX_EN_MBIAS);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_POSDIV_MASK);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON6, RG_HTPLL_RLH_EN);
	regmap_clear_bits(hdmi_phy->regmap, HDMI_CON7, RG_HTPLL_AUTOK_EN);
	usleep_range(80, 100);
}

struct mtk_hdmi_phy_conf mtk_hdmi_phy_2701_conf = {
	.flags = CLK_SET_RATE_GATE,
	.pll_default_off = true,
	.hdmi_phy_clk_ops = &mtk_hdmi_phy_pll_ops,
	.hdmi_phy_enable_tmds = mtk_hdmi_phy_enable_tmds,
	.hdmi_phy_disable_tmds = mtk_hdmi_phy_disable_tmds,
};

MODULE_AUTHOR("Chunhui Dai <chunhui.dai@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI PHY Driver");
MODULE_LICENSE("GPL v2");
