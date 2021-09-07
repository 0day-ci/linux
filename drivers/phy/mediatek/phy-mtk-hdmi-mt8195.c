// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/nvmem-consumer.h>

#include "phy-mtk-hdmi-mt8195.h"
#include "phy-mtk-hdmi.h"

static inline bool mtk_hdmi_phy_readbit(struct mtk_hdmi_phy *hdmi_phy,
					unsigned short reg, unsigned int offset)
{
	return (readl(hdmi_phy->regs + reg) & offset);
}

/*********Analog API export to HDMI Digital start*****/
static void mtk_hdmi_ana_fifo_en(struct mtk_hdmi_phy *hdmi_phy)
{
	/*make data fifo writable for hdmi2.0*/
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_ANA_CTL, reg_ana_hdmi20_fifo_en,
			  reg_ana_hdmi20_fifo_en);
}

void mtk_tmds_high_bit_clk_ratio(struct mtk_hdmi_phy *hdmi_phy, bool enable)
{
	mtk_hdmi_ana_fifo_en(hdmi_phy);

	/* HDMI 2.0 specification, 3.4Gbps <= TMDS Bit Rate <= 6G,
	 * clock bit ratio 1:40, under 3.4Gbps, clock bit ratio 1:10
	 */
	if (enable)
		mtk_hdmi_phy_mask(hdmi_phy, HDMI20_CLK_CFG,
				  (0x2 << reg_txc_div_SHIFT), reg_txc_div);
	else
		mtk_hdmi_phy_mask(hdmi_phy, HDMI20_CLK_CFG, 0, reg_txc_div);
}

/*********Analog API export to HDMI Digital end*******/

static int mtk_hdmi_pll_select_source(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_3,
			0x0 << REG_HDMITX_REF_XTAL_SEL_SHIFT,
			REG_HDMITX_REF_XTAL_SEL);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_3,
			0x0 << REG_HDMITX_REF_RESPLL_SEL_SHIFT,
			REG_HDMITX_REF_RESPLL_SEL);

	/*DA_HDMITX21_REF_CK for TXPLL input source*/
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_10,
			  0x0 << RG_HDMITXPLL_REF_CK_SEL_SHIFT,
			  RG_HDMITXPLL_REF_CK_SEL);

	return 0;
}

static int mtk_hdmi_pll_performance_setting(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	/* no matter pll input source is HDMIRX_REF_CK, xTal26M or TVD PLL,
	 * the performance configuration is the same.
	 * RG_HDMITXPLL_BP2 always 1'b1 = 0x1
	 * RG_HDMITXPLL_BC[1:0] always 2'b11 = 0x3
	 * RG_HDMITXPLL_IC[4:0] always 5'b00001 = 0x1
	 * RG_HDMITXPLL_BR[2:0] stage treatment:
	 *      24bit or 48bit->3'b001 = 0x1
	 *      30bit or 36bit->3'b011 = 0x3
	 * RG_HDMITXPLL_IR[4:0] stage treatment:
	 *      24bit,30bit,48bit ->5'b00010 = 0x2
	 *      36bit ->5'b00011 = 0x3
	 * RG_HDMITXPLL_BP[3:0] always 4'b1111 = 0xf
	 * RG_HDMITXPLL_IBAND_FIX_EN, RG_HDMITXPLL_RESERVE[14] always 2'b00 = 0x0
	 * RG_HDMITXPLL_HIKVCO always 1'b1 = 0x1
	 */

	/* BP2 */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_0,
			  0x1 << RG_HDMITXPLL_BP2_SHIFT, RG_HDMITXPLL_BP2);

	/* BC */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0x3 << RG_HDMITXPLL_BC_SHIFT, RG_HDMITXPLL_BC);

	/* IC */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0x1 << RG_HDMITXPLL_IC_SHIFT, RG_HDMITXPLL_IC);

	/* BR */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			0x2 << RG_HDMITXPLL_BR_SHIFT,
			RG_HDMITXPLL_BR);

	/* IR */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			0x2 << RG_HDMITXPLL_IR_SHIFT,
			RG_HDMITXPLL_IR);

	/* BP */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0xf << RG_HDMITXPLL_BP_SHIFT, RG_HDMITXPLL_BP);

	/* IBAND_FIX_EN, RESERVE[14] */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_0,
			  0x0 << RG_HDMITXPLL_IBAND_FIX_EN_SHIFT,
			  RG_HDMITXPLL_IBAND_FIX_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_1,
			  0x0 << RG_HDMITXPLL_RESERVE_BIT14_SHIFT,
			  RG_HDMITXPLL_RESERVE_BIT14);

	/* HIKVCO */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0x0 << RG_HDMITXPLL_HIKVCO_SHIFT,
			  RG_HDMITXPLL_HIKVCO);

	/* HREN */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_0,
			  0x1 << RG_HDMITXPLL_HREN_SHIFT, RG_HDMITXPLL_HREN);

	/* LVR_SEL */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_0,
			  0x1 << RG_HDMITXPLL_LVR_SEL_SHIFT,
			  RG_HDMITXPLL_LVR_SEL);

	/* RG_HDMITXPLL_RESERVE[12:11] */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_1,
			  0x3 << RG_HDMITXPLL_RESERVE_BIT12_11_SHIFT,
			  RG_HDMITXPLL_RESERVE_BIT12_11);

	/* TCL_EN */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_0,
			  0x1 << RG_HDMITXPLL_TCL_EN_SHIFT,
			  RG_HDMITXPLL_TCL_EN);

	/* disable read calibration impedance from efuse */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_1,
			0x1f << RG_INTR_IMP_RG_MODE_SHIFT,
			RG_INTR_IMP_RG_MODE);

	return 0;
}

static int
mtk_hdmi_pll_set_hw(struct clk_hw *hw,
		      unsigned char prediv,
		      unsigned char fbkdiv_high, unsigned long fbkdiv_low,
		      unsigned char fbkdiv_hs3, unsigned char posdiv1,
		      unsigned char posdiv2, unsigned char txprediv,
		      unsigned char txposdiv, unsigned char digital_div)
{
	unsigned char txposdiv_value = 0;
	unsigned char div3_ctrl_value = 0;
	unsigned char posdiv_vallue = 0;
	unsigned char div_ctrl_value = 0;
	unsigned char reserve_3_2_value = 0;
	unsigned char prediv_value = 0;
	unsigned char reserve13_value = 0;

	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	mtk_hdmi_pll_select_source(hw);

	mtk_hdmi_pll_performance_setting(hw);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_10,
			  0x2 << RG_HDMITX21_BIAS_PE_BG_VREF_SEL_SHIFT,
			  RG_HDMITX21_BIAS_PE_BG_VREF_SEL);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_10,
			  0x0 << RG_HDMITX21_VREF_SEL_SHIFT,
			  RG_HDMITX21_VREF_SEL);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_9,
			  0x2 << RG_HDMITX21_SLDO_VREF_SEL_SHIFT,
			  RG_HDMITX21_SLDO_VREF_SEL);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_10,
			  0x0 << RG_HDMITX21_BIAS_PE_VREF_SELB_SHIFT,
			  RG_HDMITX21_BIAS_PE_VREF_SELB);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_3,
			  0x1 << RG_HDMITX21_SLDOLPF_EN_SHIFT,
			  RG_HDMITX21_SLDOLPF_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x11 << RG_HDMITX21_INTR_CAL_SHIFT,
			  RG_HDMITX21_INTR_CAL);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0x1 << RG_HDMITXPLL_PWD_SHIFT, RG_HDMITXPLL_PWD);

	/* TXPOSDIV */
	if (txposdiv == 1)
		txposdiv_value = 0x0;
	else if (txposdiv == 2)
		txposdiv_value = 0x1;
	else if (txposdiv == 4)
		txposdiv_value = 0x2;
	else if (txposdiv == 8)
		txposdiv_value = 0x3;
	else
		return -EINVAL;

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  txposdiv_value << RG_HDMITX21_TX_POSDIV_SHIFT,
			  RG_HDMITX21_TX_POSDIV);

	/* /5, tmds_clk_frequency = tmds_data_frequency / 5 */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_TX_POSDIV_EN_SHIFT,
			  RG_HDMITX21_TX_POSDIV_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x0 << RG_HDMITX21_FRL_EN_SHIFT, RG_HDMITX21_FRL_EN);

	/* TXPREDIV */
	if (txprediv == 2) {
		div3_ctrl_value = 0x0;
		posdiv_vallue = 0x0;
	} else if (txprediv == 4) {
		div3_ctrl_value = 0x0;
		posdiv_vallue = 0x1;
	} else if (txprediv == 6) {
		div3_ctrl_value = 0x1;
		posdiv_vallue = 0x0;
	} else if (txprediv == 12) {
		div3_ctrl_value = 0x1;
		posdiv_vallue = 0x1;
	} else
		return -EINVAL;

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  div3_ctrl_value
				  << RG_HDMITXPLL_POSDIV_DIV3_CTRL_SHIFT,
			  RG_HDMITXPLL_POSDIV_DIV3_CTRL);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  posdiv_vallue << RG_HDMITXPLL_POSDIV_SHIFT,
			  RG_HDMITXPLL_POSDIV);

	/* POSDIV1 */
	if (posdiv1 == 5)
		div_ctrl_value = 0x0;
	else if (posdiv1 == 10)
		div_ctrl_value = 0x1;
	else if (posdiv1 == (125 / 10))
		div_ctrl_value = 0x2;
	else if (posdiv1 == 15)
		div_ctrl_value = 0x3;
	else
		div_ctrl_value = 0x1;

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  div_ctrl_value << RG_HDMITXPLL_DIV_CTRL_SHIFT,
			  RG_HDMITXPLL_DIV_CTRL);

	/* DE add new setting */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_1,
			  0x0 << RG_HDMITXPLL_RESERVE_BIT14_SHIFT,
			  RG_HDMITXPLL_RESERVE_BIT14);

	/* POSDIV2 */
	if (posdiv2 == 1)
		reserve_3_2_value = 0x0;
	else if (posdiv2 == 2)
		reserve_3_2_value = 0x1;
	else if (posdiv2 == 4)
		reserve_3_2_value = 0x2;
	else if (posdiv2 == 6)
		reserve_3_2_value = 0x3;
	else
		reserve_3_2_value = 0x1;

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_1,
			  reserve_3_2_value
				  << RG_HDMITXPLL_RESERVE_BIT3_2_SHIFT,
			  RG_HDMITXPLL_RESERVE_BIT3_2);

	/* DE add new setting */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_1,
			  0x2 << RG_HDMITXPLL_RESERVE_BIT1_0_SHIFT,
			  RG_HDMITXPLL_RESERVE_BIT1_0);

	/* PREDIV */
	if (prediv == 1)
		prediv_value = 0x0;
	else if (prediv == 2)
		prediv_value = 0x1;
	else if (prediv == 4)
		prediv_value = 0x2;

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  prediv_value << RG_HDMITXPLL_PREDIV_SHIFT,
			  RG_HDMITXPLL_PREDIV);

	/* FBKDIV_HS3 */
	if (fbkdiv_hs3 == 1)
		reserve13_value = 0x0;
	else if (fbkdiv_hs3 == 2)
		reserve13_value = 0x1;
	else
		return -EINVAL;

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_1,
			  reserve13_value << RG_HDMITXPLL_RESERVE_BIT13_SHIFT,
			  RG_HDMITXPLL_RESERVE_BIT13);

	/* FBDIV */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  fbkdiv_high << RG_HDMITXPLL_FBKDIV_high_SHIFT,
			  RG_HDMITXPLL_FBKDIV_high);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_3,
			  fbkdiv_low << RG_HDMITXPLL_FBKDIV_low_SHIFT,
			  RG_HDMITXPLL_FBKDIV_low);

	/* Digital DIVIDER */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_3,
			0x0 << REG_PIXEL_CLOCK_SEL_SHIFT,
			REG_PIXEL_CLOCK_SEL);

	if (digital_div == 1)
		mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_3,
				  0x0 << REG_HDMITX_PIXEL_CLOCK_SHIFT,
				  REG_HDMITX_PIXEL_CLOCK);
	else {
		mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_3,
				  0x1 << REG_HDMITX_PIXEL_CLOCK_SHIFT,
				  REG_HDMITX_PIXEL_CLOCK);
		mtk_hdmi_phy_mask(hdmi_phy, HDMI_CTL_3,
				  (digital_div - 1) << REG_HDMITXPLL_DIV_SHIFT,
				  REG_HDMITXPLL_DIV);
	}

	return 0;
}

#define PCW_DECIMAL_WIDTH 24

static int mtk_hdmi_pll_calculate_params(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	int ret;
	unsigned long long tmds_clk = 0;
	unsigned long long pixel_clk = 0;
	//ref clock from hdmi Rx
	unsigned long long hdmirx_ref_ck = 0;
	//ref clock from tvd pll
	unsigned long long ad_respll_ck = 0;
	//pll input source frequency
	unsigned long long da_hdmitx21_ref_ck = 0;
	unsigned long long ns_hdmipll_ck = 0; //ICO output clk
	//source clk for Display digital
	unsigned long long ad_hdmipll_pixel_ck = 0;
	unsigned char digital_div = 0;
	unsigned long long pcw = 0; //FBDIV
	unsigned char txprediv = 0;
	unsigned char txposdiv = 0;
	unsigned char fbkdiv_high = 0;
	unsigned long fbkdiv_low = 0;
	unsigned char posdiv1 = 0;
	unsigned char posdiv2 = 0;
	unsigned char prediv = 1; //prediv is always 1
	unsigned char fbkdiv_hs3 = 1; //fbkdiv_hs3 is always 1
	int i = 0;
	unsigned char TXPREDIV[4] = { 2, 4, 6, 12 };
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	tmds_clk = rate;
	da_hdmitx21_ref_ck = 26000000UL; //in HZ

	/*  TXPOSDIV stage treatment:
	 *	0M  <  TMDS clk  < 54M		  /8
	 *	54M <= TMDS clk  < 148.35M    /4
	 *	148.35M <=TMDS clk < 296.7M   /2
	 *	296.7 <=TMDS clk <= 594M	  /1
	 */
	if (tmds_clk < 54000000UL)
		txposdiv = 8;
	else if ((tmds_clk >= 54000000UL) && (tmds_clk < 148350000UL))
		txposdiv = 4;
	else if ((tmds_clk >= 148350000UL) && (tmds_clk < 296700000UL))
		txposdiv = 2;
	else if ((tmds_clk >= 296700000UL) && (tmds_clk <= 594000000UL))
		txposdiv = 1;
	else
		return -EINVAL;

	/* calculate TXPREDIV: can be 2, 4, 6, 12
	 * ICO clk = 5*TMDS_CLK*TXPOSDIV*TXPREDIV
	 * ICO clk constraint: 5G =< ICO clk <= 12G
	 */
	for (i = 0; i < ARRAY_SIZE(TXPREDIV); i++) {
		ns_hdmipll_ck = 5 * tmds_clk * txposdiv * TXPREDIV[i];
		if ((ns_hdmipll_ck >= 5000000000UL) &&
		    (ns_hdmipll_ck <= 12000000000UL))
			break;
	}
	if ((i == (ARRAY_SIZE(TXPREDIV) - 1)) &&
	    ((ns_hdmipll_ck < 5000000000UL) ||
	     (ns_hdmipll_ck > 12000000000UL))) {
		return -EINVAL;
	}
	txprediv = TXPREDIV[i];

	/* PCW calculation: FBKDIV
	 * formula: pcw=(frequency_out*2^pcw_bit) / frequency_in / FBKDIV_HS3;
	 * RG_HDMITXPLL_FBKDIV[32:0]:
	 * [32,24] 9bit integer, [23,0]:24bit fraction
	 */
	pcw = ns_hdmipll_ck;
	pcw = pcw << PCW_DECIMAL_WIDTH;
	pcw = pcw / da_hdmitx21_ref_ck;
	pcw = pcw / fbkdiv_hs3;

	if ((pcw / (1ULL << 32)) > 1)
		return -EINVAL;
	else if ((pcw / (1ULL << 32)) == 1) {
		fbkdiv_high = 1;
		fbkdiv_low = pcw % (1ULL << 32);
	} else {
		fbkdiv_high = 0;
		fbkdiv_low = pcw;
	}

	posdiv1 = 10;
	posdiv2 = 1;
	ad_hdmipll_pixel_ck = (ns_hdmipll_ck / 10) / 1;

	/* Digital clk divider, max /32 */
	digital_div = ad_hdmipll_pixel_ck / pixel_clk;
	if (!((digital_div <= 32) && (digital_div >= 1)))
		return -EINVAL;

	ret = mtk_hdmi_pll_set_hw(hw,
				    prediv, fbkdiv_high, fbkdiv_low, fbkdiv_hs3,
				    posdiv1, posdiv2, txprediv, txposdiv,
				    digital_div);
	if (ret)
		return -EINVAL;

	pr_debug("[HDMI][TXPLL] Parameter list:\n");
	pr_debug("prediv = %d\n", prediv);
	pr_debug("pcw = %llx\n", pcw);
	pr_debug("fbkdiv_high = %x\n", fbkdiv_high);
	pr_debug("fbkdiv_low = %lx\n", fbkdiv_low);
	pr_debug("fbkdiv_hs3 = %d\n", fbkdiv_hs3);
	pr_debug("posdiv1 = %d\n", posdiv1);
	pr_debug("posdiv2 = %d\n", posdiv2);
	pr_debug("digital_div = %d\n", digital_div);
	pr_debug("txprediv = %d\n", txprediv);
	pr_debug("txposdiv = %d\n", txposdiv);
	return 0;
}

static int mtk_hdmi_pll_drv_setting(struct clk_hw *hw)
{
	unsigned char data_channel_bias, clk_channel_bias;
	unsigned char impedance, impedance_en;
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	/* bias & impedance setting */
	data_channel_bias = 0x3c; //24mA
	clk_channel_bias = 0x34; //20mA
	impedance_en = 0xf;
	impedance = 0x36; //100ohm

	/* bias */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_1,
			  data_channel_bias << RG_HDMITX21_DRV_IBIAS_D0_SHIFT,
			  RG_HDMITX21_DRV_IBIAS_D0);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_1,
			  data_channel_bias << RG_HDMITX21_DRV_IBIAS_D1_SHIFT,
			  RG_HDMITX21_DRV_IBIAS_D1);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_1,
			  data_channel_bias << RG_HDMITX21_DRV_IBIAS_D2_SHIFT,
			  RG_HDMITX21_DRV_IBIAS_D2);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_0,
			  clk_channel_bias << RG_HDMITX21_DRV_IBIAS_CLK_SHIFT,
			  RG_HDMITX21_DRV_IBIAS_CLK);

	/* impedance */
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_0,
			  impedance_en << RG_HDMITX21_DRV_IMP_EN_SHIFT,
			  RG_HDMITX21_DRV_IMP_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_2,
			  impedance << RG_HDMITX21_DRV_IMP_D0_EN1_SHIFT,
			  RG_HDMITX21_DRV_IMP_D0_EN1);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_2,
			  impedance << RG_HDMITX21_DRV_IMP_D1_EN1_SHIFT,
			  RG_HDMITX21_DRV_IMP_D1_EN1);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_2,
			  impedance << RG_HDMITX21_DRV_IMP_D2_EN1_SHIFT,
			  RG_HDMITX21_DRV_IMP_D2_EN1);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_2,
			  impedance << RG_HDMITX21_DRV_IMP_CLK_EN1_SHIFT,
			  RG_HDMITX21_DRV_IMP_CLK_EN1);
	return 0;
}

static int mtk_hdmi_pll_prepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_TX_POSDIV_EN_SHIFT,
			  RG_HDMITX21_TX_POSDIV_EN);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_0,
			  0xf << RG_HDMITX21_SER_EN_SHIFT, RG_HDMITX21_SER_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_D0_DRV_OP_EN_SHIFT,
			  RG_HDMITX21_D0_DRV_OP_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_D1_DRV_OP_EN_SHIFT,
			  RG_HDMITX21_D1_DRV_OP_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_D2_DRV_OP_EN_SHIFT,
			  RG_HDMITX21_D2_DRV_OP_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_CK_DRV_OP_EN_SHIFT,
			  RG_HDMITX21_CK_DRV_OP_EN);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x0 << RG_HDMITX21_FRL_D0_EN_SHIFT,
			  RG_HDMITX21_FRL_D0_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x0 << RG_HDMITX21_FRL_D1_EN_SHIFT,
			  RG_HDMITX21_FRL_D1_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x0 << RG_HDMITX21_FRL_D2_EN_SHIFT,
			  RG_HDMITX21_FRL_D2_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x0 << RG_HDMITX21_FRL_CK_EN_SHIFT,
			  RG_HDMITX21_FRL_CK_EN);

	mtk_hdmi_pll_drv_setting(hw);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_10,
			  0x0 << RG_HDMITX21_BG_PWD_SHIFT, RG_HDMITX21_BG_PWD);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x1 << RG_HDMITX21_BIAS_EN_SHIFT,
			  RG_HDMITX21_BIAS_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_3,
			  0x1 << RG_HDMITX21_CKLDO_EN_SHIFT,
			  RG_HDMITX21_CKLDO_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_3,
			  0xf << RG_HDMITX21_SLDO_EN_SHIFT,
			  RG_HDMITX21_SLDO_EN);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  0x1 << DA_HDMITXPLL_PWR_ON_SHIFT,
			  DA_HDMITXPLL_PWR_ON);
	udelay(5);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  0x0 << DA_HDMITXPLL_ISO_EN_SHIFT,
			  DA_HDMITXPLL_ISO_EN);
	udelay(5);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0x0 << RG_HDMITXPLL_PWD_SHIFT, RG_HDMITXPLL_PWD);
	udelay(30);

	return 0;
}

static void mtk_hdmi_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_10,
			  0x1 << RG_HDMITX21_BG_PWD_SHIFT, RG_HDMITX21_BG_PWD);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_6,
			  0x0 << RG_HDMITX21_BIAS_EN_SHIFT,
			  RG_HDMITX21_BIAS_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_3,
			  0x0 << RG_HDMITX21_CKLDO_EN_SHIFT,
			  RG_HDMITX21_CKLDO_EN);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_3,
			  0x0 << RG_HDMITX21_SLDO_EN_SHIFT,
			  RG_HDMITX21_SLDO_EN);

	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_2,
			  0x1 << RG_HDMITXPLL_PWD_SHIFT, RG_HDMITXPLL_PWD);
	udelay(10);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  0x1 << DA_HDMITXPLL_ISO_EN_SHIFT,
			  DA_HDMITXPLL_ISO_EN);
	udelay(10);
	mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_PLL_CFG_4,
			  0x0 << DA_HDMITXPLL_PWR_ON_SHIFT,
			  DA_HDMITXPLL_PWR_ON);
}

static int mtk_hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct mtk_hdmi_phy *hdmi_phy = to_mtk_hdmi_phy(hw);
	int ret;

	dev_dbg(hdmi_phy->dev, "%s: %lu Hz, parent: %lu Hz\n", __func__, rate,
		parent_rate);

	ret = mtk_hdmi_pll_calculate_params(hw, rate, parent_rate);
	if (ret)
		return -EINVAL;

	return 0;
}

static long mtk_hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	return rate;
}

static unsigned long mtk_hdmi_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	return parent_rate;
}

static const struct clk_ops mtk_hdmi_pll_ops = {
	.prepare = mtk_hdmi_pll_prepare,
	.unprepare = mtk_hdmi_pll_unprepare,
	.set_rate = mtk_hdmi_pll_set_rate,
	.round_rate = mtk_hdmi_pll_round_rate,
	.recalc_rate = mtk_hdmi_pll_recalc_rate,
};

static void vTxSignalOnOff(struct mtk_hdmi_phy *hdmi_phy, bool OnOff)
{
	if (OnOff)
		mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_0, RG_HDMITX21_DRV_EN,
				  RG_HDMITX21_DRV_EN);
	else
		mtk_hdmi_phy_mask(hdmi_phy, HDMI_1_CFG_0,
				  0x0 << RG_HDMITX21_DRV_EN_SHIFT,
				  RG_HDMITX21_DRV_EN);
}

static void mtk_hdmi_phy_enable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	vTxSignalOnOff(hdmi_phy, true);
	usleep_range(100, 150);
}

static void mtk_hdmi_phy_disable_tmds(struct mtk_hdmi_phy *hdmi_phy)
{
	vTxSignalOnOff(hdmi_phy, false);
}

static int mtk_hdmi_phy_power_on(struct phy *phy)
{
	struct mtk_hdmi_phy *hdmi_phy = phy_get_drvdata(phy);
	int ret;

	if (!__clk_is_enabled(hdmi_phy->pll)) {
		ret = clk_prepare_enable(hdmi_phy->pll);
		if (ret < 0)
			return ret;
	}
	udelay(10);
	mtk_hdmi_phy_enable_tmds(hdmi_phy);

	return 0;
}

static int mtk_hdmi_phy_power_off(struct phy *phy)
{
	struct mtk_hdmi_phy *hdmi_phy = phy_get_drvdata(phy);

	mtk_hdmi_phy_disable_tmds(hdmi_phy);
	udelay(10);
	if (__clk_is_enabled(hdmi_phy->pll))
		clk_disable_unprepare(hdmi_phy->pll);

	return 0;
}

static const struct phy_ops mtk_hdmi_phy_ops = {
	.power_on = mtk_hdmi_phy_power_on,
	.power_off = mtk_hdmi_phy_power_off,
	.owner = THIS_MODULE,
};

static int mtk_hdmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_hdmi_phy *hdmi_phy;
	struct resource *mem;
	struct phy *phy;
	struct phy_provider *phy_provider;
	struct clk *ref_clk;
	const char *ref_clk_name;
	struct clk_init_data clk_init = {
		.ops = &mtk_hdmi_pll_ops,
		.num_parents = 1,
		.parent_names = (const char *const *)&ref_clk_name,
		.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE,
	};
	int ret;

	hdmi_phy = devm_kzalloc(dev, sizeof(*hdmi_phy), GFP_KERNEL);
	if (!hdmi_phy)
		return -ENOMEM;

	ref_clk = devm_clk_get(dev, "hdmi_xtal_sel");
	if (IS_ERR(ref_clk)) {
		ret = PTR_ERR(ref_clk);
		dev_err(&pdev->dev, "Failed to get PLL reference clock: %d\n",
			ret);
		return ret;
	}
	ref_clk_name = __clk_get_name(ref_clk);

	ret = of_property_read_string(dev->of_node, "clock-output-names",
				      &clk_init.name);
	if (ret < 0)
		return ret;

	hdmi_phy->pll_hw.init = &clk_init;
	hdmi_phy->pll = devm_clk_register(dev, &hdmi_phy->pll_hw);
	if (IS_ERR(hdmi_phy->pll)) {
		ret = PTR_ERR(hdmi_phy->pll);
		return ret;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi_phy->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(hdmi_phy->regs)) {
		ret = PTR_ERR(hdmi_phy->regs);
		return ret;
	}

	phy = devm_phy_create(dev, NULL, &mtk_hdmi_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, hdmi_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	hdmi_phy->dev = dev;

	return of_clk_add_provider(dev->of_node, of_clk_src_simple_get,
				   hdmi_phy->pll);
}

static int mtk_hdmi_phy_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mtk_hdmi_phy_match[] = {
	{
		.compatible = "mediatek,mt8195-hdmi-phy",
	},
	{},
};

struct platform_driver mtk_hdmi_phy_mt8195_driver = {
	.probe = mtk_hdmi_phy_probe,
	.remove = mtk_hdmi_phy_remove,
	.driver = {
		.name = "mediatek-hdmi-mt8195-phy",
		.of_match_table = mtk_hdmi_phy_match,
	},
};
module_platform_driver(mtk_hdmi_phy_mt8195_driver);

MODULE_AUTHOR("Can Zeng <can.zeng@mediatek.com>");
MODULE_DESCRIPTION("MediaTek MT8195 HDMI PHY Driver");
MODULE_LICENSE("GPL v2");
