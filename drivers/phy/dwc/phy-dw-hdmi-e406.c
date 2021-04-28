// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI PHY e406
 *
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/phy/dwc/dw-hdmi-phy-pdata.h>

#include "phy-dw-hdmi-e40x.h"

/*
 * Equalization algorithm based on version 01-08-2019
 * but with the following adjustments related with 1080p and 4k:
 * - DW_PHY_EQ_MAX_SETTING 14 -> 10
 * - DW_PHY_EQ_COUNTER_VAL_4K 512 -> 712
 *
 */
#define DW_PHY_EQ_WAIT_TIME_START		3
#define DW_PHY_EQ_SLEEP_TIME_CDR		17
#define DW_PHY_EQ_SLEEP_TIME_ACQ		1
#define DW_PHY_EQ_BOUNDSPREAD			20
#define DW_PHY_EQ_MIN_ACQ_STABLE		3
#define DW_PHY_EQ_ACC_LIMIT			360
#define DW_PHY_EQ_ACC_MIN_LIMIT			0
#define DW_PHY_EQ_MAX_SETTING			10
#define DW_PHY_EQ_SHORT_CABLE_SETTING		4
#define DW_PHY_EQ_ERROR_CABLE_SETTING		4
#define DW_PHY_EQ_MIN_SLOPE			50
#define DW_PHY_EQ_AVG_ACQ			3
#define DW_PHY_EQ_MINMAX_NTRIES			5
#define DW_PHY_EQ_COUNTER_VAL			712
#define DW_PHY_EQ_COUNTER_VAL_4K		712
#define DW_PHY_EQ_COUNTER_VAL_HDMI20		450
#define DW_PHY_EQ_MINMAX_MAXDIFF		4
#define DW_PHY_EQ_MINMAX_MAXDIFF_4K		4
#define DW_PHY_EQ_MINMAX_MAXDIFF_HDMI20		4
#define DW_PHY_EQ_FATBIT_MASK			0x0c03
#define DW_PHY_EQ_FATBIT_MASK_4K		0x0c03
#define DW_PHY_EQ_FATBIT_MASK_HDMI20		0x0e03
#define DW_PHY_EQ_CDR_PHUG_FRUG			0x251f
#define DW_PHY_EQ_CDR_PHUG_FRUG_4k		0x001f
#define DW_PHY_EQ_CDR_PHUG_FRUG_HDMI20		0x001f
#define DW_PHY_EQ_CDR_PHUG_FRUG_DEF		0x001f
#define DW_CHX_EQ_CTRL3_MASK			0x0000

/* PHY e406 mpll configuration */
static const struct dw_phy_mpll_config dw_phy_e406_mpll_cfg[] = {
	{ 0x27, 0x1C94 },
	{ 0x28, 0x3713 },
	{ 0x29, 0x24DA },
	{ 0x2A, 0x5492 },
	{ 0x2B, 0x4B0D },
	{ 0x2C, 0x4760 },
	{ 0x2D, 0x008C },
	{ 0x2E, 0x0010 },
	{ 0x00, 0x0000 },
};

/* PHY e406 equalization functions */
static int dw_phy_eq_test(struct dw_phy_dev *dw_dev,
			  u16 *fat_bit_mask, int *min_max_length,
			  u16 *eq_cnt_threshold, u16 *cdr_phug_frug)
{
	u16 main_fsm_status, val;
	unsigned int i;

	for (i = 0; i < DW_PHY_EQ_WAIT_TIME_START; i++) {
		main_fsm_status = dw_phy_read(dw_dev, DW_PHY_MAINFSM_STATUS1);
		if (main_fsm_status & DW_PHY_CLOCK_STABLE)
			break;
		mdelay(DW_PHY_EQ_SLEEP_TIME_CDR);
	}

	if (i == DW_PHY_EQ_WAIT_TIME_START) {
		dev_dbg(dw_dev->dev, "PHY start conditions not achieved\n");
		return -ETIMEDOUT;
	}

	if (main_fsm_status & DW_PHY_PLL_RATE_BIT1) {
		dev_dbg(dw_dev->dev, "invalid pll rate\n");
		return -EINVAL;
	}

	val = dw_phy_read(dw_dev, DW_PHY_CDR_CTRL_CNT) &
		DW_PHY_HDMI_MHL_MODE_MASK;
	if (val == DW_PHY_HDMI_MHL_MODE_ABOVE_3_4G_BITPS) {
		/* HDMI 2.0 */
		*fat_bit_mask = DW_PHY_EQ_FATBIT_MASK_HDMI20;
		*min_max_length = DW_PHY_EQ_MINMAX_MAXDIFF_HDMI20;
		*eq_cnt_threshold = DW_PHY_EQ_COUNTER_VAL_HDMI20;
		*cdr_phug_frug = DW_PHY_EQ_CDR_PHUG_FRUG_HDMI20;
		dev_dbg(dw_dev->dev, "[EQUALIZER] using HDMI 2.0 values\n");
	} else if (!(main_fsm_status & DW_PHY_PLL_RATE_MASK)) {
		/* HDMI 1.4 (pll rate = 0) */
		*fat_bit_mask = DW_PHY_EQ_FATBIT_MASK_4K;
		*min_max_length = DW_PHY_EQ_MINMAX_MAXDIFF_4K;
		*eq_cnt_threshold = DW_PHY_EQ_COUNTER_VAL_4K;
		*cdr_phug_frug = DW_PHY_EQ_CDR_PHUG_FRUG_4k;
		dev_dbg(dw_dev->dev, "[EQUALIZER] using HDMI 1.4@4k values\n");
	} else {
		/* HDMI 1.4 */
		*fat_bit_mask = DW_PHY_EQ_FATBIT_MASK;
		*min_max_length = DW_PHY_EQ_MINMAX_MAXDIFF;
		*eq_cnt_threshold = DW_PHY_EQ_COUNTER_VAL;
		*cdr_phug_frug = DW_PHY_EQ_CDR_PHUG_FRUG;
		dev_dbg(dw_dev->dev, "[EQUALIZER] using HDMI 1.4 values\n");
	}

	return 0;
}

static void dw_phy_eq_auto_calib(struct dw_phy_dev *dw_dev)
{
	dw_phy_write(dw_dev,
		     (DW_PHY_EQCAL_DIS_CTRL_ONE_EIGHT_RATE |
		      DW_PHY_EQCAL_DIS_CTRL_QUARTER_RATE |
		      DW_PHY_FORCE_STATE_DIS |
		      DW_PHY_MAIN_FSM_STATE(9)), DW_PHY_MAINFSM_CTRL);
	dw_phy_write(dw_dev,
		     (DW_PHY_EQCAL_DIS_CTRL_ONE_EIGHT_RATE |
		      DW_PHY_EQCAL_DIS_CTRL_QUARTER_RATE |
		      DW_PHY_FORCE_STATE_EN |
		      DW_PHY_MAIN_FSM_STATE(9)), DW_PHY_MAINFSM_CTRL);
	dw_phy_write(dw_dev,
		     (DW_PHY_EQCAL_DIS_CTRL_ONE_EIGHT_RATE |
		      DW_PHY_EQCAL_DIS_CTRL_QUARTER_RATE |
		      DW_PHY_FORCE_STATE_DIS |
		      DW_PHY_MAIN_FSM_STATE(9)), DW_PHY_MAINFSM_CTRL);
}

void dw_phy_eq_settings(struct dw_phy_dev *dw_dev, u16 ch0_setting,
			u16 ch1_setting, u16 ch2_setting)
{
	dw_phy_write(dw_dev,
		     (DW_CHX_EQ_CTRL3_MASK |
		      (ch0_setting & DW_PHY_CH0_EXT_EQ_SET_MASK)),
		     DW_PHY_CH0_EQ_CTRL3);
	dw_phy_write(dw_dev,
		     (DW_CHX_EQ_CTRL3_MASK |
		      (ch1_setting & DW_PHY_CH1_EXT_EQ_SET_MASK)),
		     DW_PHY_CH1_EQ_CTRL3);
	dw_phy_write(dw_dev,
		     (DW_CHX_EQ_CTRL3_MASK |
		      (ch2_setting & DW_PHY_CH2_EXT_EQ_SET_MASK)),
		     DW_PHY_CH2_EQ_CTRL3);
	dw_phy_write(dw_dev, DW_PHY_EQ_EN_OVR_EN, DW_PHY_MAINFSM_OVR2);

	dw_phy_eq_auto_calib(dw_dev);
}

static void dw_phy_eq_default(struct dw_phy_dev *dw_dev)
{
	dw_phy_eq_settings(dw_dev, 0, 0, 0);
}

static void dw_phy_eq_single(struct dw_phy_dev *dw_dev)
{
	u16 val;

	dw_phy_write(dw_dev,
		     (DW_PHY_CH0_LOOP_CTR_LIMIT(1) |
		      DW_PHY_CH0_MSTR_CTR_LIMIT(1) |
		      DW_PHY_CH0_ADAP_COMP_LIMIT(1)), DW_PHY_CH0_EQ_CTRL1);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH1_LOOP_CTR_LIMIT(1) |
		      DW_PHY_CH1_MSTR_CTR_LIMIT(1) |
		      DW_PHY_CH1_ADAP_COMP_LIMIT(1)), DW_PHY_CH1_EQ_CTRL1);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH2_LOOP_CTR_LIMIT(1) |
		      DW_PHY_CH2_MSTR_CTR_LIMIT(1) |
		      DW_PHY_CH2_ADAP_COMP_LIMIT(1)), DW_PHY_CH2_EQ_CTRL1);

	dw_phy_write(dw_dev,
		     (DW_PHY_CH1_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH1_LB_ACTIVE_OVR |
		      (DW_PHY_CH1_EQUALIZATION_CTR_THR(DW_PHY_EQ_AVG_ACQ) &
		       DW_PHY_CH1_EQUALIZATION_CTR_THR_MASK)),
		     DW_PHY_CH1_EQ_CTRL2);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH2_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH2_LB_ACTIVE_OVR |
		      (DW_PHY_CH2_EQUALIZATION_CTR_THR(DW_PHY_EQ_AVG_ACQ) &
		       DW_PHY_CH2_EQUALIZATION_CTR_THR_MASK)),
		     DW_PHY_CH2_EQ_CTRL2);

	val = dw_phy_read(dw_dev, DW_PHY_MAINFSM_OVR2);
	val &= ~(DW_PHY_EQ_EN_OVR | DW_PHY_EQ_EN_OVR_EN);
	dw_phy_write(dw_dev, val, DW_PHY_MAINFSM_OVR2);
}

static void dw_phy_eq_equal_setting(struct dw_phy_dev *dw_dev, u16 lock_vector)
{
	dw_phy_write(dw_dev, lock_vector, DW_PHY_CH0_EQ_CTRL4);

	dw_phy_write(dw_dev,
		     (DW_PHY_CH0_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH0_LB_ACTIVE_OVR |
		      (DW_PHY_CH0_EQUALIZATION_CTR_THR(DW_PHY_EQ_AVG_ACQ) &
		       DW_PHY_CH0_EQUALIZATION_CTR_THR_MASK) |
		      DW_PHY_CH0_CH_EQ_SAME_OVRD), DW_PHY_CH0_EQ_CTRL2);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH0_OVRD_LOCK |
		      DW_PHY_CH0_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH0_LB_ACTIVE_OVR |
		      (DW_PHY_CH0_EQUALIZATION_CTR_THR(DW_PHY_EQ_AVG_ACQ) &
		       DW_PHY_CH0_EQUALIZATION_CTR_THR_MASK) |
		      DW_PHY_CH0_CH_EQ_SAME_OVRD), DW_PHY_CH0_EQ_CTRL2);
}

static void dw_phy_eq_init_vars(struct dw_phy_eq_ch *ch)
{
	ch->acc = 0;
	ch->acq = 0;
	ch->last_acq = 0;
	ch->valid_long_setting = 0;
	ch->valid_short_setting = 0;
	ch->best_setting = DW_PHY_EQ_SHORT_CABLE_SETTING;
}

static bool dw_phy_eq_acquire_early_cnt(struct dw_phy_dev *dw_dev,
					u16 setting, u16 acq,
					struct dw_phy_eq_ch *ch0,
					struct dw_phy_eq_ch *ch1,
					struct dw_phy_eq_ch *ch2)
{
	u16 lock_vector = 0x1 << setting;

	ch0->acq = 0;
	ch1->acq = 0;
	ch2->acq = 0;

	dw_phy_eq_equal_setting(dw_dev, lock_vector);
	dw_phy_eq_auto_calib(dw_dev);

	mdelay(DW_PHY_EQ_SLEEP_TIME_CDR);
	if (!dw_phy_tmds_valid(dw_dev))
		dev_dbg(dw_dev->dev, "TMDS is NOT valid\n");

	ch0->acq = dw_phy_read(dw_dev, DW_PHY_CH0_EQ_STATUS3) >>
		DW_PHY_EQ_AVG_ACQ;
	ch1->acq = dw_phy_read(dw_dev, DW_PHY_CH1_EQ_STATUS3) >>
		DW_PHY_EQ_AVG_ACQ;
	ch2->acq = dw_phy_read(dw_dev, DW_PHY_CH2_EQ_STATUS3) >>
		DW_PHY_EQ_AVG_ACQ;

	dev_dbg(dw_dev->dev,
		"%s -> phy_read(dw_dev, DW_PHY_CH0_EQ_STATUS3) [%d] [%u]\n",
		__func__, setting, ch0->acq);
	dev_dbg(dw_dev->dev,
		"%s -> phy_read(dw_dev, DW_PHY_CH1_EQ_STATUS3) [%d] [%u]\n",
		__func__, setting, ch1->acq);
	dev_dbg(dw_dev->dev,
		"%s -> phy_read(dw_dev, DW_PHY_CH2_EQ_STATUS3) [%d] [%u]\n",
		__func__, setting, ch2->acq);

	return true;
}

static int dw_phy_eq_test_type(u16 setting, bool tmds_valid,
			       u16 eq_cnt_threshold, struct dw_phy_eq_ch *ch)
{
	u16 step_slope = 0;

	if (ch->acq < ch->last_acq && tmds_valid) {
		/* Long cable equalization */
		ch->acc += ch->last_acq - ch->acq;
		if (!ch->valid_long_setting &&
		    ch->acq < eq_cnt_threshold &&
		    ch->acc > DW_PHY_EQ_ACC_MIN_LIMIT) {
			ch->best_long_setting = setting;
			ch->valid_long_setting = 1;
		}
		step_slope = ch->last_acq - ch->acq;
	}

	if (tmds_valid && !ch->valid_short_setting) {
		/* Short cable equalization */
		if (setting < DW_PHY_EQ_SHORT_CABLE_SETTING &&
		    ch->acq < eq_cnt_threshold) {
			ch->best_short_setting = setting;
			ch->valid_short_setting = 1;
		}

		if (setting == DW_PHY_EQ_SHORT_CABLE_SETTING) {
			ch->best_short_setting = DW_PHY_EQ_SHORT_CABLE_SETTING;
			ch->valid_short_setting = 1;
		}
	}

	if (ch->valid_long_setting && ch->acc > DW_PHY_EQ_ACC_LIMIT) {
		ch->best_setting = ch->best_long_setting;
		return DW_PHY_EQ_TEST_TYPE_BEST_SET_IS_LONG;
	}

	if (setting == DW_PHY_EQ_MAX_SETTING && ch->acc < DW_PHY_EQ_ACC_LIMIT &&
	    ch->valid_short_setting) {
		ch->best_setting = ch->best_short_setting;
		return DW_PHY_EQ_TEST_TYPE_BEST_SET_IS_SHORT;
	}

	if (setting == DW_PHY_EQ_MAX_SETTING && tmds_valid &&
	    ch->acc > DW_PHY_EQ_ACC_LIMIT &&
	    step_slope > DW_PHY_EQ_MIN_SLOPE) {
		ch->best_setting = DW_PHY_EQ_MAX_SETTING;
		return DW_PHY_EQ_TEST_TYPE_BEST_SET_IS_MAX;
	}

	if (setting == DW_PHY_EQ_MAX_SETTING) {
		ch->best_setting = DW_PHY_EQ_ERROR_CABLE_SETTING;
		return DW_PHY_EQ_TEST_TYPE_BEST_SET_ERROR;
	}

	return 0;
}

static bool dw_phy_eq_setting_finder(struct dw_phy_dev *dw_dev, u16 acq,
				     u16 eq_cnt_threshold,
				     struct dw_phy_eq_ch *ch0,
				     struct dw_phy_eq_ch *ch1,
				     struct dw_phy_eq_ch *ch2)
{
	int ret_ch0 = 0, ret_ch1 = 0, ret_ch2 = 0;
	bool tmds_valid = false;
	u16 act = 0;

	dw_phy_eq_init_vars(ch0);
	dw_phy_eq_init_vars(ch1);
	dw_phy_eq_init_vars(ch2);

	tmds_valid = dw_phy_eq_acquire_early_cnt(dw_dev, act, acq,
						 ch0, ch1, ch2);

	while (!ret_ch0 || !ret_ch1 || !ret_ch2) {
		act++;

		ch0->last_acq = ch0->acq;
		ch1->last_acq = ch1->acq;
		ch2->last_acq = ch2->acq;

		tmds_valid = dw_phy_eq_acquire_early_cnt(dw_dev, act, acq,
							 ch0, ch1, ch2);

		if (!ret_ch0)
			ret_ch0 = dw_phy_eq_test_type(act, tmds_valid,
						      eq_cnt_threshold, ch0);
		if (!ret_ch1)
			ret_ch1 = dw_phy_eq_test_type(act, tmds_valid,
						      eq_cnt_threshold, ch1);
		if (!ret_ch2)
			ret_ch2 = dw_phy_eq_test_type(act, tmds_valid,
						      eq_cnt_threshold, ch2);
	}

	if (ret_ch0 == DW_PHY_EQ_TEST_TYPE_BEST_SET_ERROR ||
	    ret_ch1 == DW_PHY_EQ_TEST_TYPE_BEST_SET_ERROR ||
	    ret_ch2 == DW_PHY_EQ_TEST_TYPE_BEST_SET_ERROR)
		return false;
	return true;
}

static bool dw_phy_eq_maxvsmin(u16 ch0_setting, u16 ch1_setting,
			       u16 ch2_setting, u16 min_max_length)
{
	u16 min = ch0_setting, max = ch0_setting;

	if (ch1_setting > max)
		max = ch1_setting;
	if (ch2_setting > max)
		max = ch2_setting;
	if (ch1_setting < min)
		min = ch1_setting;
	if (ch2_setting < min)
		min = ch2_setting;

	if ((max - min) > min_max_length)
		return false;
	return true;
}

static int dw_phy_eq_init(struct dw_phy_dev *dw_dev, u16 acq, bool force)
{
	u16 fat_bit_mask, eq_cnt_threshold;
	struct dw_phy_eq_ch ch0, ch1, ch2;
	int min_max_length, ret = 0;
	u16 cdr_phug_frug;
	u16 mpll_status;
	unsigned int i;

	if (!dw_dev->phy_enabled)
		return -EINVAL;

	mpll_status = dw_phy_read(dw_dev, DW_PHY_CLK_MPLL_STATUS);
	if (mpll_status == dw_dev->mpll_status && !force)
		return ret;

	dw_dev->mpll_status = mpll_status;

	ret = dw_phy_eq_test(dw_dev, &fat_bit_mask, &min_max_length,
			     &eq_cnt_threshold, &cdr_phug_frug);
	if (ret) {
		if (ret == -EINVAL) /* Means equalizer is not needed */
			ret = 0;

		/* Do not change values if we don't have clock */
		if (ret != -ETIMEDOUT) {
			dw_phy_eq_default(dw_dev);
			dw_phy_pddq(dw_dev, 1);
			dw_phy_pddq(dw_dev, 0);
		}
	} else {
		dw_phy_eq_single(dw_dev);
		dw_phy_write(dw_dev, fat_bit_mask, DW_PHY_CH0_EQ_CTRL6);
		dw_phy_write(dw_dev, fat_bit_mask, DW_PHY_CH1_EQ_CTRL6);
		dw_phy_write(dw_dev, fat_bit_mask, DW_PHY_CH2_EQ_CTRL6);
		/* config cdr */
		dw_phy_write(dw_dev, cdr_phug_frug, DW_PHY_CH0_CDR_CTRL);
		dw_phy_write(dw_dev, cdr_phug_frug, DW_PHY_CH1_CDR_CTRL);
		dw_phy_write(dw_dev, cdr_phug_frug, DW_PHY_CH2_CDR_CTRL);

		for (i = 0; i < DW_PHY_EQ_MINMAX_NTRIES; i++) {
			if (dw_phy_eq_setting_finder(dw_dev, acq,
						     eq_cnt_threshold,
						     &ch0, &ch1, &ch2)) {
				if (dw_phy_eq_maxvsmin(ch0.best_setting,
						       ch1.best_setting,
						       ch2.best_setting,
						       min_max_length))
					break;
			}

			ch0.best_setting = DW_PHY_EQ_ERROR_CABLE_SETTING;
			ch1.best_setting = DW_PHY_EQ_ERROR_CABLE_SETTING;
			ch2.best_setting = DW_PHY_EQ_ERROR_CABLE_SETTING;
		}

		dev_dbg(dw_dev->dev, "equalizer settings: ch0=0x%x, ch1=0x%x, ch1=0x%x\n",
			ch0.best_setting, ch1.best_setting,
			ch2.best_setting);

		if (i == DW_PHY_EQ_MINMAX_NTRIES)
			ret = -EINVAL;

		/* restore cdr to default settings */
		dw_phy_write(dw_dev, DW_PHY_EQ_CDR_PHUG_FRUG_DEF,
			     DW_PHY_CH0_CDR_CTRL);
		dw_phy_write(dw_dev, DW_PHY_EQ_CDR_PHUG_FRUG_DEF,
			     DW_PHY_CH1_CDR_CTRL);
		dw_phy_write(dw_dev, DW_PHY_EQ_CDR_PHUG_FRUG_DEF,
			     DW_PHY_CH2_CDR_CTRL);

		dw_phy_eq_settings(dw_dev, ch0.best_setting, ch1.best_setting,
				   ch2.best_setting);

		dw_phy_pddq(dw_dev, 1);
		dw_phy_pddq(dw_dev, 0);
	}

	return ret;
}

/* PHY e406 data */
const struct dw_hdmi_phy_data dw_phy_e406_data = {
	.name = "e406",
	.version = 406,
	.mpll_cfg = dw_phy_e406_mpll_cfg,
	.dw_phy_eq_init = dw_phy_eq_init,
};
