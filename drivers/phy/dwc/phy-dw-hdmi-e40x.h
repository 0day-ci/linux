/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI PHYs e405 and e406 driver
 *
 * Author: Jose Abreu <jose.abreu@synopsys.com>
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */

#ifndef __DW_HDMI_PHY_E40X_H__
#define __DW_HDMI_PHY_E40X_H__

#include <linux/phy/phy.h>

/* PHYs e405 and e406 Registers */

/* Clock Measurement Unit Configuration Register */
#define DW_PHY_CMU_CONFIG			0x02
#define DW_PHY_TIMEBASE_OVR(v)			(v)
#define DW_PHY_TIMEBASE_OVR_MASK		GENMASK(8, 0)
#define DW_PHY_TIMEBASE_OVR_EN			BIT(9)
#define DW_PHY_LOCK_THRES(v)			((v) << 10)
#define DW_PHY_LOCK_THRES_MASK			GENMASK(15, 10)

/* System Configuration Register */
#define DW_PHY_SYSTEM_CONFIG			0x03
#define DW_PHY_CLRDEP_8BIT_MODE			(0 << 5)
#define DW_PHY_CLRDEP_10BIT_MODE		BIT(5)
#define DW_PHY_CLRDEP_12BIT_MODE		(2 << 5)
#define DW_PHY_CLRDEP_16BIT_MODE		(3 << 5)
#define DW_PHY_CLRDEP_MASK			GENMASK(6, 5)
#define DW_PHY_FAST_SWITCHING			BIT(11)

/* Main FSM Control Register */
#define DW_PHY_MAINFSM_CTRL			0x05
#define DW_PHY_MAIN_FSM_STATE(v)		(v)
#define DW_PHY_MAIN_FSM_STATE_MASK		GENMASK(3, 0)
#define DW_PHY_FORCE_STATE_EN			BIT(4)
#define DW_PHY_FORCE_STATE_DIS			(0 << 4)
#define DW_PHY_FORCE_STATE_MASK			BIT(4)
#define DW_PHY_EQCAL_DIS_CTRL_QUARTER_RATE	(BIT(2) << 9)
#define DW_PHY_EQCAL_DIS_CTRL_ONE_EIGHT_RATE	(BIT(3) << 9)
#define DW_PHY_EQCAL_DIS_CTRL_MASK		GENMASK(12, 9)

/* Main FSM Override 2 Register */
#define DW_PHY_MAINFSM_OVR2			0x08
#define DW_PHY_EQ_EN_OVR			BIT(5)
#define DW_PHY_EQ_EN_OVR_EN			BIT(6)

/* Main FSM Status 1 Register */
#define DW_PHY_MAINFSM_STATUS1			0x09
#define DW_PHY_CLOCK_STABLE			BIT(8)
#define DW_PHY_PLL_RATE_BIT0			BIT(9)
#define DW_PHY_PLL_RATE_BIT1			(2 << 9)
#define DW_PHY_PLL_RATE_MASK			GENMASK(10, 9)

/* Overload Protection Control Register */
#define DW_PHY_OVL_PROT_CTRL			0x0D
#define DW_PHY_SCRAMBLING_EN_OVR		BIT(6)
#define DW_PHY_SCRAMBLING_EN_OVR_EN		BIT(7)

/* CDR Control Register */
#define DW_PHY_CDR_CTRL_CNT			0x0E
#define DW_PHY_HDMI_MHL_MODE_BELOW_3_4G_BITPS	(0 << 8)
#define DW_PHY_HDMI_MHL_MODE_ABOVE_3_4G_BITPS	BIT(8)
#define DW_PHY_HDMI_MHL_MODE_MASK		GENMASK(9, 8)

#define DW_PHY_CLK_MPLL_STATUS			0x2F
#define DW_PHY_CH0_CDR_CTRL			0x31

/* Channel 0 Equalizer Control 1 Register*/
#define DW_PHY_CH0_EQ_CTRL1			0x32
#define DW_PHY_CH0_LOOP_CTR_LIMIT(v)		(v)
#define DW_PHY_CH0_LOOP_CTR_LIMIT_MASK		GENMASK(3, 0)
#define DW_PHY_CH0_MSTR_CTR_LIMIT(v)		((v) << 4)
#define DW_PHY_CH0_MSTR_CTR_LIMIT_MASK		GENMASK(8, 4)
#define DW_PHY_CH0_ADAP_COMP_LIMIT(v)		((v) << 9)
#define DW_PHY_CH0_ADAP_COMP_LIMIT_MASK		GENMASK(12, 9)

/* Channel 0 Equalizer Control 2 Register */
#define DW_PHY_CH0_EQ_CTRL2			0x33
#define DW_PHY_CH0_OVRD_LOCK			BIT(1)
#define DW_PHY_CH0_OVRD_LOCK_VECTOR_EN		BIT(2)
#define DW_PHY_CH0_LB_ACTIVE_OVR		BIT(5)
#define DW_PHY_CH0_EQUALIZATION_CTR_THR(v)	((v) << 11)
#define DW_PHY_CH0_EQUALIZATION_CTR_THR_MASK	GENMASK(13, 11)
#define DW_PHY_CH0_CH_EQ_SAME_OVRD		BIT(14)

#define DW_PHY_CH0_EQ_STATUS			0x34

/* Channel 0 Equalizer Control 3 Register */
#define DW_PHY_CH0_EQ_CTRL3			0x3E
#define DW_PHY_CH0_EXT_EQ_SET_MASK		GENMASK(3, 0)

#define DW_PHY_CH0_EQ_CTRL4			0x3F
#define DW_PHY_CH0_EQ_STATUS2			0x40
#define DW_PHY_CH0_EQ_STATUS3			0x42
#define DW_PHY_CH0_EQ_CTRL6			0x43
#define DW_PHY_CH1_CDR_CTRL			0x51

/* Channel 1 Equalizer Control 1 Register */
#define DW_PHY_CH1_EQ_CTRL1			0x52
#define DW_PHY_CH1_LOOP_CTR_LIMIT(v)		(v)
#define DW_PHY_CH1_LOOP_CTR_LIMIT_MASK		GENMASK(3, 0)
#define DW_PHY_CH1_MSTR_CTR_LIMIT(v)		((v) << 4)
#define DW_PHY_CH1_MSTR_CTR_LIMIT_MASK		GENMASK(8, 4)
#define DW_PHY_CH1_ADAP_COMP_LIMIT(v)		((v) << 9)
#define DW_PHY_CH1_ADAP_COMP_LIMIT_MASK		GENMASK(12, 9)

/* Channel 1 Equalizer Control 2 Register */
#define DW_PHY_CH1_EQ_CTRL2			0x53
#define DW_PHY_CH1_OVRD_LOCK			BIT(1)
#define DW_PHY_CH1_OVRD_LOCK_VECTOR_EN		BIT(2)
#define DW_PHY_CH1_LB_ACTIVE_OVR		BIT(5)
#define DW_PHY_CH1_EQUALIZATION_CTR_THR(v)	((v) << 11)
#define DW_PHY_CH1_EQUALIZATION_CTR_THR_MASK	GENMASK(13, 11)

#define DW_PHY_CH1_EQ_STATUS			0x54

/* Channel 1 Equalizer Control 3 Register */
#define DW_PHY_CH1_EQ_CTRL3			0x5E
#define DW_PHY_CH1_EXT_EQ_SET_MASK		GENMASK(3, 0)

#define DW_PHY_CH1_EQ_CTRL4			0x5F
#define DW_PHY_CH1_EQ_STATUS2			0x60
#define DW_PHY_CH1_EQ_STATUS3			0x62
#define DW_PHY_CH1_EQ_CTRL6			0x63
#define DW_PHY_CH2_CDR_CTRL			0x71

/* Channel 2 Equalizer Control 1 Register */
#define DW_PHY_CH2_EQ_CTRL1			0x72
#define DW_PHY_CH2_LOOP_CTR_LIMIT(v)		(v)
#define DW_PHY_CH2_LOOP_CTR_LIMIT_MASK		GENMASK(3, 0)
#define DW_PHY_CH2_MSTR_CTR_LIMIT(v)		((v) << 4)
#define DW_PHY_CH2_MSTR_CTR_LIMIT_MASK		GENMASK(8, 4)
#define DW_PHY_CH2_ADAP_COMP_LIMIT(v)		((v) << 9)
#define DW_PHY_CH2_ADAP_COMP_LIMIT_MASK		GENMASK(12, 9)

/* Channel 2 Equalizer Control 2 Register */
#define DW_PHY_CH2_EQ_CTRL2			0x73
#define DW_PHY_CH2_OVRD_LOCK			BIT(1)
#define DW_PHY_CH2_OVRD_LOCK_VECTOR_EN		BIT(2)
#define DW_PHY_CH2_LB_ACTIVE_OVR		BIT(5)
#define DW_PHY_CH2_EQUALIZATION_CTR_THR(v)	((v) << 11)
#define DW_PHY_CH2_EQUALIZATION_CTR_THR_MASK	GENMASK(13, 11)

#define DW_PHY_CH2_EQ_STATUS			0x74

/* Channel 2 Equalizer Control 3 Register */
#define DW_PHY_CH2_EQ_CTRL3			0x7E
#define DW_PHY_CH2_EXT_EQ_SET_MASK		GENMASK(3, 0)

#define DW_PHY_CH2_EQ_CTRL4			0x7F
#define DW_PHY_CH2_EQ_STATUS2			0x80
#define DW_PHY_CH2_EQ_STATUS3			0x82
#define DW_PHY_CH2_EQ_CTRL6			0x83

/* PHY equalization test type return codes */
#define DW_PHY_EQ_TEST_TYPE_BEST_SET_IS_LONG	1
#define DW_PHY_EQ_TEST_TYPE_BEST_SET_IS_SHORT	2
#define DW_PHY_EQ_TEST_TYPE_BEST_SET_IS_MAX	3
#define DW_PHY_EQ_TEST_TYPE_BEST_SET_ERROR	255

/* PHY equalization channel struct */
struct dw_phy_eq_ch {
	u16 best_long_setting;
	u8 valid_long_setting;
	u16 best_short_setting;
	u8 valid_short_setting;
	u16 best_setting;
	u16 acc;
	u16 acq;
	u16 last_acq;
	u16 upper_bound_acq;
	u16 lower_bound_acq;
	u16 out_bound_acq;
	u16 read_acq;
};

/* PHY mpll configuration struct */
struct dw_phy_mpll_config {
	u16 addr;
	u16 val;
};

struct dw_phy_dev;

/* PHY data struct */
struct dw_hdmi_phy_data {
	const char *name;
	unsigned int version;
	const struct dw_phy_mpll_config *mpll_cfg;
	int (*dw_phy_eq_init)(struct dw_phy_dev *dw_dev, u16 acq, bool force);
};

/* PHY device struct */
struct dw_phy_dev {
	struct device *dev;
	struct dw_phy_pdata *config;
	const struct dw_hdmi_phy_data *phy_data;
	struct phy *phy;
	struct phy_configure_opts_hdmi hdmi_opts;
	struct clk *clk;
	u8 phy_enabled;
	u16 mpll_status;
	u8 color_depth;
	u8 hdmi2;
	u8 scrambling;
};

void dw_phy_write(struct dw_phy_dev *dw_dev, u16 val, u16 addr);
u16 dw_phy_read(struct dw_phy_dev *dw_dev, u16 addr);
void dw_phy_pddq(struct dw_phy_dev *dw_dev, int enable);
bool dw_phy_tmds_valid(struct dw_phy_dev *dw_dev);

extern const struct dw_hdmi_phy_data dw_phy_e405_data;
extern const struct dw_hdmi_phy_data dw_phy_e406_data;

#endif /* __DW_HDMI_PHY_E40X_H__ */
