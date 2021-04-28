// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI PHY e405
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
#include <linux/phy/dwc/dw-hdmi-phy-pdata.h>

#include "phy-dw-hdmi-e40x.h"

#define DW_PHY_EQ_WAIT_TIME_START		3
#define DW_PHY_EQ_SLEEP_TIME_CDR		30
#define DW_PHY_EQ_SLEEP_TIME_ACQ		1
#define DW_PHY_EQ_BOUNDSPREAD			20
#define DW_PHY_EQ_MIN_ACQ_STABLE		3
#define DW_PHY_EQ_ACC_LIMIT			360
#define DW_PHY_EQ_ACC_MIN_LIMIT			0
#define DW_PHY_EQ_MAX_SETTING			13
#define DW_PHY_EQ_SHORT_CABLE_SETTING		4
#define DW_PHY_EQ_ERROR_CABLE_SETTING		4
#define DW_PHY_EQ_MIN_SLOPE			50
#define DW_PHY_EQ_AVG_ACQ			5
#define DW_PHY_EQ_MINMAX_NTRIES			3
#define DW_PHY_EQ_COUNTER_VAL			512
#define DW_PHY_EQ_COUNTER_VAL_HDMI20		512
#define DW_PHY_EQ_MINMAX_MAXDIFF		4
#define DW_PHY_EQ_MINMAX_MAXDIFF_HDMI20		2
#define DW_PHY_EQ_FATBIT_MASK			0x0000
#define DW_PHY_EQ_FATBIT_MASK_4K		0x0c03
#define DW_PHY_EQ_FATBIT_MASK_HDMI20		0x0e03

/* PHY e405 mpll configuration */
static const struct dw_phy_mpll_config dw_phy_e405_mpll_cfg[] = {
	{ 0x27, 0x1B94 },
	{ 0x28, 0x16D2 },
	{ 0x29, 0x12D9 },
	{ 0x2A, 0x3249 },
	{ 0x2B, 0x3653 },
	{ 0x2C, 0x3436 },
	{ 0x2D, 0x124D },
	{ 0x2E, 0x0001 },
	{ 0xCE, 0x0505 },
	{ 0xCF, 0x0505 },
	{ 0xD0, 0x0000 },
	{ 0x00, 0x0000 },
};

/* PHY e405 equalization functions */
static int dw_phy_eq_test(struct dw_phy_dev *dw_dev,
			  u16 *fat_bit_mask, int *min_max_length)
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
		dev_dbg(dw_dev->dev, "[EQUALIZER] using HDMI 2.0 values\n");
	} else if (!(main_fsm_status & DW_PHY_PLL_RATE_MASK)) {
		/* HDMI 1.4 (pll rate = 0) */
		*fat_bit_mask = DW_PHY_EQ_FATBIT_MASK_4K;
		*min_max_length = DW_PHY_EQ_MINMAX_MAXDIFF;
		dev_dbg(dw_dev->dev, "[EQUALIZER] using HDMI 1.4@4k values\n");
	} else {
		/* HDMI 1.4 */
		*fat_bit_mask = DW_PHY_EQ_FATBIT_MASK;
		*min_max_length = DW_PHY_EQ_MINMAX_MAXDIFF;
		dev_dbg(dw_dev->dev, "[EQUALIZER] using HDMI 1.4 values\n");
	}

	return 0;
}

static void dw_phy_eq_default(struct dw_phy_dev *dw_dev)
{
	dw_phy_write(dw_dev,
		     (DW_PHY_CH0_LOOP_CTR_LIMIT(8) |
		      DW_PHY_CH0_MSTR_CTR_LIMIT(10) |
		      DW_PHY_CH0_ADAP_COMP_LIMIT(4)), DW_PHY_CH0_EQ_CTRL1);
	dw_phy_write(dw_dev, DW_PHY_CH0_LB_ACTIVE_OVR, DW_PHY_CH0_EQ_CTRL2);

	dw_phy_write(dw_dev,
		     (DW_PHY_CH1_LOOP_CTR_LIMIT(8) |
		      DW_PHY_CH1_MSTR_CTR_LIMIT(10) |
		      DW_PHY_CH1_ADAP_COMP_LIMIT(4)), DW_PHY_CH1_EQ_CTRL1);
	dw_phy_write(dw_dev, DW_PHY_CH1_LB_ACTIVE_OVR, DW_PHY_CH1_EQ_CTRL2);

	dw_phy_write(dw_dev,
		     (DW_PHY_CH2_LOOP_CTR_LIMIT(8) |
		      DW_PHY_CH2_MSTR_CTR_LIMIT(10) |
		      DW_PHY_CH2_ADAP_COMP_LIMIT(4)), DW_PHY_CH2_EQ_CTRL1);
	dw_phy_write(dw_dev, DW_PHY_CH2_LB_ACTIVE_OVR, DW_PHY_CH2_EQ_CTRL2);
}

static void dw_phy_eq_single(struct dw_phy_dev *dw_dev)
{
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
}

static void dw_phy_eq_equal_setting_ch0(struct dw_phy_dev *dw_dev,
					u16 lock_vector)
{
	dw_phy_write(dw_dev, lock_vector, DW_PHY_CH0_EQ_CTRL4);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH0_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH0_LB_ACTIVE_OVR), DW_PHY_CH0_EQ_CTRL2);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH0_OVRD_LOCK |
		      DW_PHY_CH0_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH0_LB_ACTIVE_OVR), DW_PHY_CH0_EQ_CTRL2);
	dw_phy_read(dw_dev, DW_PHY_CH0_EQ_STATUS2);
}

static void dw_phy_eq_equal_setting_ch1(struct dw_phy_dev *dw_dev,
					u16 lock_vector)
{
	dw_phy_write(dw_dev, lock_vector, DW_PHY_CH1_EQ_CTRL4);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH1_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH1_LB_ACTIVE_OVR), DW_PHY_CH1_EQ_CTRL2);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH1_OVRD_LOCK |
		      DW_PHY_CH1_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH1_LB_ACTIVE_OVR), DW_PHY_CH1_EQ_CTRL2);
	dw_phy_read(dw_dev, DW_PHY_CH1_EQ_STATUS2);
}

static void dw_phy_eq_equal_setting_ch2(struct dw_phy_dev *dw_dev,
					u16 lock_vector)
{
	dw_phy_write(dw_dev, lock_vector, DW_PHY_CH2_EQ_CTRL4);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH2_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH2_LB_ACTIVE_OVR), DW_PHY_CH2_EQ_CTRL2);
	dw_phy_write(dw_dev,
		     (DW_PHY_CH2_OVRD_LOCK |
		      DW_PHY_CH2_OVRD_LOCK_VECTOR_EN |
		      DW_PHY_CH2_LB_ACTIVE_OVR), DW_PHY_CH2_EQ_CTRL2);
	dw_phy_read(dw_dev, DW_PHY_CH2_EQ_STATUS2);
}

static void dw_phy_eq_equal_setting(struct dw_phy_dev *dw_dev, u16 lock_vector)
{
	dw_phy_eq_equal_setting_ch0(dw_dev, lock_vector);
	dw_phy_eq_equal_setting_ch1(dw_dev, lock_vector);
	dw_phy_eq_equal_setting_ch2(dw_dev, lock_vector);
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
	unsigned int i;

	ch0->out_bound_acq = 0;
	ch1->out_bound_acq = 0;
	ch2->out_bound_acq = 0;
	ch0->acq = 0;
	ch1->acq = 0;
	ch2->acq = 0;

	dw_phy_eq_equal_setting(dw_dev, lock_vector);
	dw_phy_eq_auto_calib(dw_dev);

	mdelay(DW_PHY_EQ_SLEEP_TIME_CDR);
	if (!dw_phy_tmds_valid(dw_dev))
		dev_dbg(dw_dev->dev, "TMDS is NOT valid\n");

	ch0->read_acq = dw_phy_read(dw_dev, DW_PHY_CH0_EQ_STATUS3);
	ch1->read_acq = dw_phy_read(dw_dev, DW_PHY_CH1_EQ_STATUS3);
	ch2->read_acq = dw_phy_read(dw_dev, DW_PHY_CH2_EQ_STATUS3);

	ch0->acq += ch0->read_acq;
	ch1->acq += ch1->read_acq;
	ch2->acq += ch2->read_acq;

	ch0->upper_bound_acq = ch0->read_acq + DW_PHY_EQ_BOUNDSPREAD;
	ch0->lower_bound_acq = ch0->read_acq - DW_PHY_EQ_BOUNDSPREAD;
	ch1->upper_bound_acq = ch1->read_acq + DW_PHY_EQ_BOUNDSPREAD;
	ch1->lower_bound_acq = ch1->read_acq - DW_PHY_EQ_BOUNDSPREAD;
	ch2->upper_bound_acq = ch2->read_acq + DW_PHY_EQ_BOUNDSPREAD;
	ch2->lower_bound_acq = ch2->read_acq - DW_PHY_EQ_BOUNDSPREAD;

	for (i = 1; i < acq; i++) {
		dw_phy_eq_auto_calib(dw_dev);
		mdelay(DW_PHY_EQ_SLEEP_TIME_ACQ);

		if (ch0->read_acq > ch0->upper_bound_acq ||
		    ch0->read_acq < ch0->lower_bound_acq)
			ch0->out_bound_acq++;
		if (ch1->read_acq > ch1->upper_bound_acq ||
		    ch1->read_acq < ch1->lower_bound_acq)
			ch1->out_bound_acq++;
		if (ch2->read_acq > ch2->upper_bound_acq ||
		    ch2->read_acq < ch2->lower_bound_acq)
			ch2->out_bound_acq++;

		if (i == DW_PHY_EQ_MIN_ACQ_STABLE) {
			if (!ch0->out_bound_acq &&
			    !ch1->out_bound_acq &&
			    !ch2->out_bound_acq) {
				acq = 3;
				break;
			}
		}

		ch0->read_acq = dw_phy_read(dw_dev, DW_PHY_CH0_EQ_STATUS3);
		ch1->read_acq = dw_phy_read(dw_dev, DW_PHY_CH1_EQ_STATUS3);
		ch2->read_acq = dw_phy_read(dw_dev, DW_PHY_CH2_EQ_STATUS3);

		ch0->acq += ch0->read_acq;
		ch1->acq += ch1->read_acq;
		ch2->acq += ch2->read_acq;
	}

	ch0->acq /= acq;
	ch1->acq /= acq;
	ch2->acq /= acq;

	return true;
}

static int dw_phy_eq_test_type(u16 setting, bool tmds_valid,
			       struct dw_phy_eq_ch *ch)
{
	u16 step_slope = 0;

	if (ch->acq < ch->last_acq && tmds_valid) {
		/* Long cable equalization */
		ch->acc += ch->last_acq - ch->acq;
		if (!ch->valid_long_setting && ch->acq < 512 && ch->acc) {
			ch->best_long_setting = setting;
			ch->valid_long_setting = 1;
		}
		step_slope = ch->last_acq - ch->acq;
	}

	if (tmds_valid && !ch->valid_short_setting) {
		/* Short cable equalization */
		if (setting < DW_PHY_EQ_SHORT_CABLE_SETTING &&
		    ch->acq < DW_PHY_EQ_COUNTER_VAL) {
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
			ret_ch0 = dw_phy_eq_test_type(act, tmds_valid, ch0);
		if (!ret_ch1)
			ret_ch1 = dw_phy_eq_test_type(act, tmds_valid, ch1);
		if (!ret_ch2)
			ret_ch2 = dw_phy_eq_test_type(act, tmds_valid, ch2);
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
	struct dw_phy_pdata *phy = dw_dev->config;
	u16 fat_bit_mask, lock_vector = 0x1;
	struct dw_phy_eq_ch ch0, ch1, ch2;
	int min_max_length, ret = 0;
	u16 mpll_status;
	unsigned int i;

	if (phy->version < 401)
		return ret;

	if (!dw_dev->phy_enabled)
		return -EINVAL;

	mpll_status = dw_phy_read(dw_dev, DW_PHY_CLK_MPLL_STATUS);
	if (mpll_status == dw_dev->mpll_status && !force)
		return ret;

	dw_dev->mpll_status = mpll_status;

	dw_phy_write(dw_dev, 0x00, DW_PHY_MAINFSM_OVR2);
	dw_phy_write(dw_dev, 0x00, DW_PHY_CH0_EQ_CTRL3);
	dw_phy_write(dw_dev, 0x00, DW_PHY_CH1_EQ_CTRL3);
	dw_phy_write(dw_dev, 0x00, DW_PHY_CH2_EQ_CTRL3);

	ret = dw_phy_eq_test(dw_dev, &fat_bit_mask, &min_max_length);
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
		dw_phy_eq_equal_setting(dw_dev, 0x0001);
		dw_phy_write(dw_dev, fat_bit_mask, DW_PHY_CH0_EQ_CTRL6);
		dw_phy_write(dw_dev, fat_bit_mask, DW_PHY_CH1_EQ_CTRL6);
		dw_phy_write(dw_dev, fat_bit_mask, DW_PHY_CH2_EQ_CTRL6);

		for (i = 0; i < DW_PHY_EQ_MINMAX_NTRIES; i++) {
			if (dw_phy_eq_setting_finder(dw_dev, acq,
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
			ch0.best_setting, ch1.best_setting, ch2.best_setting);

		if (i == DW_PHY_EQ_MINMAX_NTRIES)
			ret = -EINVAL;

		lock_vector = 0x1 << ch0.best_setting;
		dw_phy_eq_equal_setting_ch0(dw_dev, lock_vector);

		lock_vector = 0x1 << ch1.best_setting;
		dw_phy_eq_equal_setting_ch1(dw_dev, lock_vector);

		lock_vector = 0x1 << ch2.best_setting;
		dw_phy_eq_equal_setting_ch2(dw_dev, lock_vector);

		dw_phy_pddq(dw_dev, 1);
		dw_phy_pddq(dw_dev, 0);
	}

	return ret;
}

/* PHY e405 data */
const struct dw_hdmi_phy_data dw_phy_e405_data = {
	.name = "e405",
	.version = 405,
	.mpll_cfg = dw_phy_e405_mpll_cfg,
	.dw_phy_eq_init = dw_phy_eq_init,
};
