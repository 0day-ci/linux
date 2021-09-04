// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Limited
 */

#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "tsens.h"

/* ----- SROT ------ */
#define SROT_HW_VER_OFF	0x0000
#define SROT_CTRL_OFF		0x0004

/* ----- TM ------ */
#define TM_INT_EN_OFF				0x0000
#define TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF	0x0004
#define TM_Sn_STATUS_OFF			0x0044
#define TM_TRDY_OFF				0x0084
#define TM_HIGH_LOW_INT_STATUS_OFF		0x0088
#define TM_HIGH_LOW_Sn_INT_THRESHOLD_OFF	0x0090

/* eeprom layout data for msm8956/76 (v1) */
#define MSM8976_BASE0_MASK	0xff
#define MSM8976_BASE1_MASK	0xff
#define MSM8976_BASE1_SHIFT	8

#define MSM8976_S0_P1_MASK	0x3f00
#define MSM8976_S1_P1_MASK	0x3f00000
#define MSM8976_S2_P1_MASK	0x3f
#define MSM8976_S3_P1_MASK	0x3f000
#define MSM8976_S4_P1_MASK	0x3f00
#define MSM8976_S5_P1_MASK	0x3f00000
#define MSM8976_S6_P1_MASK	0x3f
#define MSM8976_S7_P1_MASK	0x3f000
#define MSM8976_S8_P1_MASK	0x1f8
#define MSM8976_S9_P1_MASK	0x1f8000
#define MSM8976_S10_P1_MASK	0xf8000000
#define MSM8976_S10_P1_MASK_1	0x1

#define MSM8976_S0_P2_MASK	0xfc000
#define MSM8976_S1_P2_MASK	0xfc000000
#define MSM8976_S2_P2_MASK	0xfc0
#define MSM8976_S3_P2_MASK	0xfc0000
#define MSM8976_S4_P2_MASK	0xfc000
#define MSM8976_S5_P2_MASK	0xfc000000
#define MSM8976_S6_P2_MASK	0xfc0
#define MSM8976_S7_P2_MASK	0xfc0000
#define MSM8976_S8_P2_MASK	0x7e00
#define MSM8976_S9_P2_MASK	0x7e00000
#define MSM8976_S10_P2_MASK	0x7e

#define MSM8976_S0_P1_SHIFT	8
#define MSM8976_S1_P1_SHIFT	20
#define MSM8976_S2_P1_SHIFT	0
#define MSM8976_S3_P1_SHIFT	12
#define MSM8976_S4_P1_SHIFT	8
#define MSM8976_S5_P1_SHIFT	20
#define MSM8976_S6_P1_SHIFT	0
#define MSM8976_S7_P1_SHIFT	12
#define MSM8976_S8_P1_SHIFT	3
#define MSM8976_S9_P1_SHIFT	15
#define MSM8976_S10_P1_SHIFT	27
#define MSM8976_S10_P1_SHIFT_1	0

#define MSM8976_S0_P2_SHIFT	14
#define MSM8976_S1_P2_SHIFT	26
#define MSM8976_S2_P2_SHIFT	6
#define MSM8976_S3_P2_SHIFT	18
#define MSM8976_S4_P2_SHIFT	14
#define MSM8976_S5_P2_SHIFT	26
#define MSM8976_S6_P2_SHIFT	6
#define MSM8976_S7_P2_SHIFT	18
#define MSM8976_S8_P2_SHIFT	9
#define MSM8976_S9_P2_SHIFT	21
#define MSM8976_S10_P2_SHIFT	1

#define MSM8976_CAL_SEL_MASK	0x3

#define MSM8976_CAL_DEGC_PT1	30
#define MSM8976_CAL_DEGC_PT2	120
#define MSM8976_SLOPE_FACTOR	1000
#define MSM8976_SLOPE_DEFAULT	3200

/* eeprom layout data for qcs404/405 (v1) */
#define BASE0_MASK	0x000007f8
#define BASE1_MASK	0x0007f800
#define BASE0_SHIFT	3
#define BASE1_SHIFT	11

#define S0_P1_MASK	0x0000003f
#define S1_P1_MASK	0x0003f000
#define S2_P1_MASK	0x3f000000
#define S3_P1_MASK	0x000003f0
#define S4_P1_MASK	0x003f0000
#define S5_P1_MASK	0x0000003f
#define S6_P1_MASK	0x0003f000
#define S7_P1_MASK	0x3f000000
#define S8_P1_MASK	0x000003f0
#define S9_P1_MASK	0x003f0000

#define S0_P2_MASK	0x00000fc0
#define S1_P2_MASK	0x00fc0000
#define S2_P2_MASK_1_0	0xc0000000
#define S2_P2_MASK_5_2	0x0000000f
#define S3_P2_MASK	0x0000fc00
#define S4_P2_MASK	0x0fc00000
#define S5_P2_MASK	0x00000fc0
#define S6_P2_MASK	0x00fc0000
#define S7_P2_MASK_1_0	0xc0000000
#define S7_P2_MASK_5_2	0x0000000f
#define S8_P2_MASK	0x0000fc00
#define S9_P2_MASK	0x0fc00000

#define S0_P1_SHIFT	0
#define S0_P2_SHIFT	6
#define S1_P1_SHIFT	12
#define S1_P2_SHIFT	18
#define S2_P1_SHIFT	24
#define S2_P2_SHIFT_1_0	30

#define S2_P2_SHIFT_5_2	0
#define S3_P1_SHIFT	4
#define S3_P2_SHIFT	10
#define S4_P1_SHIFT	16
#define S4_P2_SHIFT	22

#define S5_P1_SHIFT	0
#define S5_P2_SHIFT	6
#define S6_P1_SHIFT	12
#define S6_P2_SHIFT	18
#define S7_P1_SHIFT	24
#define S7_P2_SHIFT_1_0	30

#define S7_P2_SHIFT_5_2	0
#define S8_P1_SHIFT	4
#define S8_P2_SHIFT	10
#define S9_P1_SHIFT	16
#define S9_P2_SHIFT	22

#define CAL_SEL_MASK	7
#define CAL_SEL_SHIFT	0

/* eeprom layout data for 8994 */
#define MSM8994_BASE0_MASK	0x3ff
#define MSM8994_BASE1_MASK	0xffc00
#define MSM8994_BASE0_SHIFT	0
#define MSM8994_BASE1_SHIFT	10

#define MSM8994_S0_MASK	0xf00000
#define MSM8994_S1_MASK	0xf000000
#define MSM8994_S2_MASK	0xf0000000
#define MSM8994_S3_MASK	0xf
#define MSM8994_S4_MASK	0xf0
#define MSM8994_S5_MASK	0xf00
#define MSM8994_S6_MASK	0xf000
#define MSM8994_S7_MASK	0xf0000
#define MSM8994_S8_MASK	0xf00000
#define MSM8994_S9_MASK	0xf000000
#define MSM8994_S10_MASK	0xf0000000
#define MSM8994_S11_MASK	0xf
#define MSM8994_S12_MASK	0xf0
#define MSM8994_S13_MASK	0xf00
#define MSM8994_S14_MASK	0xf000
#define MSM8994_S15_MASK	0xf0000

#define MSM8994_S0_SHIFT	20
#define MSM8994_S1_SHIFT	24
#define MSM8994_S2_SHIFT	28
#define MSM8994_S3_SHIFT	0
#define MSM8994_S4_SHIFT	4
#define MSM8994_S5_SHIFT	8
#define MSM8994_S6_SHIFT	12
#define MSM8994_S7_SHIFT	16
#define MSM8994_S8_SHIFT	20
#define MSM8994_S9_SHIFT	24
#define MSM8994_S10_SHIFT	28
#define MSM8994_S11_SHIFT	0
#define MSM8994_S12_SHIFT	4
#define MSM8994_S13_SHIFT	8
#define MSM8994_S14_SHIFT	12
#define MSM8994_S15_SHIFT	16

#define MSM8994_CAL_SEL_MASK	0x700000
#define MSM8994_CAL_SEL_SHIFT	20

#define MSM8994_BASE0_REDUN_MASK	0x7fe00000
#define MSM8994_BASE1_BIT0_REDUN_MASK	0x80000000
#define MSM8994_BASE1_BIT1_9_REDUN_MASK	0x1ff
#define MSM8994_BASE0_REDUN_SHIFT	21
#define MSM8994_BASE1_BIT0_REDUN_SHIFT_COMPUTE	31

#define MSM8994_S0_REDUN_MASK	0x1e00
#define MSM8994_S1_REDUN_MASK	0x1e000
#define MSM8994_S2_REDUN_MASK	0x1e0000
#define MSM8994_S3_REDUN_MASK	0x1e00000
#define MSM8994_S4_REDUN_MASK	0x1e000000
#define MSM8994_S5_REDUN_MASK_BIT0_2	0xe0000000
#define MSM8994_S5_REDUN_MASK_BIT3	0x800000
#define MSM8994_S6_REDUN_MASK	0xf000000
#define MSM8994_S7_REDUN_MASK	0xf0000000
#define MSM8994_S8_REDUN_MASK	0xf
#define MSM8994_S9_REDUN_MASK	0xf0
#define MSM8994_S10_REDUN_MASK	0xf00
#define MSM8994_S11_REDUN_MASK	0xf000
#define MSM8994_S12_REDUN_MASK	0xf0000
#define MSM8994_S13_REDUN_MASK	0xf00000
#define MSM8994_S14_REDUN_MASK	0xf000000
#define MSM8994_S15_REDUN_MASK	0xf0000000

#define MSM8994_S0_REDUN_SHIFT	9
#define MSM8994_S1_REDUN_SHIFT	13
#define MSM8994_S2_REDUN_SHIFT	17
#define MSM8994_S3_REDUN_SHIFT	21
#define MSM8994_S4_REDUN_SHIFT	25
#define MSM8994_S5_REDUN_SHIFT_BIT0_2	29
#define MSM8994_S5_REDUN_SHIFT_BIT3	23
#define MSM8994_S6_REDUN_SHIFT	24
#define MSM8994_S7_REDUN_SHIFT	28
#define MSM8994_S8_REDUN_SHIFT	0
#define MSM8994_S9_REDUN_SHIFT	4
#define MSM8994_S10_REDUN_SHIFT	8
#define MSM8994_S11_REDUN_SHIFT	12
#define MSM8994_S12_REDUN_SHIFT	16
#define MSM8994_S13_REDUN_SHIFT	20
#define MSM8994_S14_REDUN_SHIFT	24
#define MSM8994_S15_REDUN_SHIFT	28

#define MSM8994_REDUN_SEL_MASK		0x7
#define MSM8994_CAL_SEL_REDUN_MASK	0xe0000000
#define MSM8994_CAL_SEL_REDUN_SHIFT	29

#define BKP_SEL			0x3
#define BKP_REDUN_SEL		0xe0000000
#define BKP_REDUN_SHIFT		29

static void compute_intercept_slope_8976(struct tsens_priv *priv,
			      u32 *p1, u32 *p2, u32 mode)
{
	int i;

	priv->sensor[0].slope = 3313;
	priv->sensor[1].slope = 3275;
	priv->sensor[2].slope = 3320;
	priv->sensor[3].slope = 3246;
	priv->sensor[4].slope = 3279;
	priv->sensor[5].slope = 3257;
	priv->sensor[6].slope = 3234;
	priv->sensor[7].slope = 3269;
	priv->sensor[8].slope = 3255;
	priv->sensor[9].slope = 3239;
	priv->sensor[10].slope = 3286;

	for (i = 0; i < priv->num_sensors; i++) {
		priv->sensor[i].offset = (p1[i] * MSM8976_SLOPE_FACTOR) -
				(MSM8976_CAL_DEGC_PT1 *
				priv->sensor[i].slope);
	}
}

/* HW-specific calculations forwardported from msm-3.10 kernel */
static void compute_intercept_slope_8994(struct tsens_priv *priv,
			      u32 base0, u32 base1, u32 *p, u32 mode)
{
	int adc_code_of_tempx, i, num, den, slope;

	/* slope (m, dy/dx) = SLOPE_FACTOR * (adc_code2 - adc_code1)/(temp_120_degc - temp_30_degc) */
	num = base1 - base0;
	num *= SLOPE_FACTOR;
	den = CAL_DEGC_PT2 - CAL_DEGC_PT1;
	slope = num / den;

	for (i = 0; i < priv->num_sensors; i++) {
		adc_code_of_tempx = base0 + p[i];
		priv->sensor[i].offset = (adc_code_of_tempx * SLOPE_FACTOR) -
				(CAL_DEGC_PT1 *	priv->sensor[i].slope);
		priv->sensor[i].slope = (mode == TWO_PT_CALIB) ? slope : SLOPE_DEFAULT;

		dev_dbg(priv->dev, "%s: offset:%d, slope:%d\n", __func__,
			priv->sensor[i].offset, priv->sensor[i].slope);
	}
}

static int calibrate_v1(struct tsens_priv *priv)
{
	u32 base0 = 0, base1 = 0;
	u32 p1[10], p2[10];
	u32 mode = 0, lsb = 0, msb = 0;
	u32 *qfprom_cdata;
	int i;

	qfprom_cdata = (u32 *)qfprom_read(priv->dev, "calib");
	if (IS_ERR(qfprom_cdata))
		return PTR_ERR(qfprom_cdata);

	mode = (qfprom_cdata[4] & CAL_SEL_MASK) >> CAL_SEL_SHIFT;
	dev_dbg(priv->dev, "calibration mode is %d\n", mode);

	switch (mode) {
	case TWO_PT_CALIB:
		base1 = (qfprom_cdata[4] & BASE1_MASK) >> BASE1_SHIFT;
		p2[0] = (qfprom_cdata[0] & S0_P2_MASK) >> S0_P2_SHIFT;
		p2[1] = (qfprom_cdata[0] & S1_P2_MASK) >> S1_P2_SHIFT;
		/* This value is split over two registers, 2 bits and 4 bits */
		lsb   = (qfprom_cdata[0] & S2_P2_MASK_1_0) >> S2_P2_SHIFT_1_0;
		msb   = (qfprom_cdata[1] & S2_P2_MASK_5_2) >> S2_P2_SHIFT_5_2;
		p2[2] = msb << 2 | lsb;
		p2[3] = (qfprom_cdata[1] & S3_P2_MASK) >> S3_P2_SHIFT;
		p2[4] = (qfprom_cdata[1] & S4_P2_MASK) >> S4_P2_SHIFT;
		p2[5] = (qfprom_cdata[2] & S5_P2_MASK) >> S5_P2_SHIFT;
		p2[6] = (qfprom_cdata[2] & S6_P2_MASK) >> S6_P2_SHIFT;
		/* This value is split over two registers, 2 bits and 4 bits */
		lsb   = (qfprom_cdata[2] & S7_P2_MASK_1_0) >> S7_P2_SHIFT_1_0;
		msb   = (qfprom_cdata[3] & S7_P2_MASK_5_2) >> S7_P2_SHIFT_5_2;
		p2[7] = msb << 2 | lsb;
		p2[8] = (qfprom_cdata[3] & S8_P2_MASK) >> S8_P2_SHIFT;
		p2[9] = (qfprom_cdata[3] & S9_P2_MASK) >> S9_P2_SHIFT;
		for (i = 0; i < priv->num_sensors; i++)
			p2[i] = ((base1 + p2[i]) << 2);
		fallthrough;
	case ONE_PT_CALIB2:
		base0 = (qfprom_cdata[4] & BASE0_MASK) >> BASE0_SHIFT;
		p1[0] = (qfprom_cdata[0] & S0_P1_MASK) >> S0_P1_SHIFT;
		p1[1] = (qfprom_cdata[0] & S1_P1_MASK) >> S1_P1_SHIFT;
		p1[2] = (qfprom_cdata[0] & S2_P1_MASK) >> S2_P1_SHIFT;
		p1[3] = (qfprom_cdata[1] & S3_P1_MASK) >> S3_P1_SHIFT;
		p1[4] = (qfprom_cdata[1] & S4_P1_MASK) >> S4_P1_SHIFT;
		p1[5] = (qfprom_cdata[2] & S5_P1_MASK) >> S5_P1_SHIFT;
		p1[6] = (qfprom_cdata[2] & S6_P1_MASK) >> S6_P1_SHIFT;
		p1[7] = (qfprom_cdata[2] & S7_P1_MASK) >> S7_P1_SHIFT;
		p1[8] = (qfprom_cdata[3] & S8_P1_MASK) >> S8_P1_SHIFT;
		p1[9] = (qfprom_cdata[3] & S9_P1_MASK) >> S9_P1_SHIFT;
		for (i = 0; i < priv->num_sensors; i++)
			p1[i] = (((base0) + p1[i]) << 2);
		break;
	default:
		for (i = 0; i < priv->num_sensors; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
		break;
	}

	compute_intercept_slope(priv, p1, p2, mode);
	kfree(qfprom_cdata);

	return 0;
}

static int calibrate_8976(struct tsens_priv *priv)
{
	int base0 = 0, base1 = 0, i;
	u32 p1[11], p2[11];
	int mode = 0, tmp = 0;
	u32 *qfprom_cdata;

	qfprom_cdata = (u32 *)qfprom_read(priv->dev, "calib");
	if (IS_ERR(qfprom_cdata))
		return PTR_ERR(qfprom_cdata);

	mode = (qfprom_cdata[4] & MSM8976_CAL_SEL_MASK);
	dev_dbg(priv->dev, "calibration mode is %d\n", mode);

	switch (mode) {
	case TWO_PT_CALIB:
		base1 = (qfprom_cdata[2] & MSM8976_BASE1_MASK) >> MSM8976_BASE1_SHIFT;
		p2[0] = (qfprom_cdata[0] & MSM8976_S0_P2_MASK) >> MSM8976_S0_P2_SHIFT;
		p2[1] = (qfprom_cdata[0] & MSM8976_S1_P2_MASK) >> MSM8976_S1_P2_SHIFT;
		p2[2] = (qfprom_cdata[1] & MSM8976_S2_P2_MASK) >> MSM8976_S2_P2_SHIFT;
		p2[3] = (qfprom_cdata[1] & MSM8976_S3_P2_MASK) >> MSM8976_S3_P2_SHIFT;
		p2[4] = (qfprom_cdata[2] & MSM8976_S4_P2_MASK) >> MSM8976_S4_P2_SHIFT;
		p2[5] = (qfprom_cdata[2] & MSM8976_S5_P2_MASK) >> MSM8976_S5_P2_SHIFT;
		p2[6] = (qfprom_cdata[3] & MSM8976_S6_P2_MASK) >> MSM8976_S6_P2_SHIFT;
		p2[7] = (qfprom_cdata[3] & MSM8976_S7_P2_MASK) >> MSM8976_S7_P2_SHIFT;
		p2[8] = (qfprom_cdata[4] & MSM8976_S8_P2_MASK) >> MSM8976_S8_P2_SHIFT;
		p2[9] = (qfprom_cdata[4] & MSM8976_S9_P2_MASK) >> MSM8976_S9_P2_SHIFT;
		p2[10] = (qfprom_cdata[5] & MSM8976_S10_P2_MASK) >> MSM8976_S10_P2_SHIFT;

		for (i = 0; i < priv->num_sensors; i++)
			p2[i] = ((base1 + p2[i]) << 2);
		fallthrough;
	case ONE_PT_CALIB2:
		base0 = qfprom_cdata[0] & MSM8976_BASE0_MASK;
		p1[0] = (qfprom_cdata[0] & MSM8976_S0_P1_MASK) >> MSM8976_S0_P1_SHIFT;
		p1[1] = (qfprom_cdata[0] & MSM8976_S1_P1_MASK) >> MSM8976_S1_P1_SHIFT;
		p1[2] = (qfprom_cdata[1] & MSM8976_S2_P1_MASK) >> MSM8976_S2_P1_SHIFT;
		p1[3] = (qfprom_cdata[1] & MSM8976_S3_P1_MASK) >> MSM8976_S3_P1_SHIFT;
		p1[4] = (qfprom_cdata[2] & MSM8976_S4_P1_MASK) >> MSM8976_S4_P1_SHIFT;
		p1[5] = (qfprom_cdata[2] & MSM8976_S5_P1_MASK) >> MSM8976_S5_P1_SHIFT;
		p1[6] = (qfprom_cdata[3] & MSM8976_S6_P1_MASK) >> MSM8976_S6_P1_SHIFT;
		p1[7] = (qfprom_cdata[3] & MSM8976_S7_P1_MASK) >> MSM8976_S7_P1_SHIFT;
		p1[8] = (qfprom_cdata[4] & MSM8976_S8_P1_MASK) >> MSM8976_S8_P1_SHIFT;
		p1[9] = (qfprom_cdata[4] & MSM8976_S9_P1_MASK) >> MSM8976_S9_P1_SHIFT;
		p1[10] = (qfprom_cdata[4] & MSM8976_S10_P1_MASK) >> MSM8976_S10_P1_SHIFT;
		tmp = (qfprom_cdata[5] & MSM8976_S10_P1_MASK_1) << MSM8976_S10_P1_SHIFT_1;
		p1[10] |= tmp;

		for (i = 0; i < priv->num_sensors; i++)
			p1[i] = (((base0) + p1[i]) << 2);
		break;
	default:
		for (i = 0; i < priv->num_sensors; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
		break;
	}

	compute_intercept_slope_8976(priv, p1, p2, mode);
	kfree(qfprom_cdata);

	return 0;
}

static int calibrate_8994(struct tsens_priv *priv)
{
	int base0 = 0, base1 = 0, i;
	u32 p[16] = { [0 ... 15] = 532 }; /* HW-specific, undocumented magic value */
	int mode = 0;
	u32 *calib0, *calib1, *calib2, *calib_mode, *calib_rsel;
	u32 calib_redun_sel;

	/* 0x40d0-0x40dc */
	calib0 = (u32 *)qfprom_read(priv->dev, "calib");
	if (IS_ERR(calib0))
		return PTR_ERR(calib0);

	dev_dbg(priv->dev, "%s: calib0: [0] = %u, [1] = %u, [2] = %u\n",
		__func__, calib0[0], calib0[1], calib0[2]);

	/* 0x41c0-0x41c8 */
	calib1 = (u32 *)qfprom_read(priv->dev, "calib_redun1_2");
	if (IS_ERR(calib1))
		return PTR_ERR(calib1);

	dev_dbg(priv->dev, "%s: calib1: [0] = %u, [1] = %u\n",
		__func__, calib1[0], calib1[1]);

	/* 0x41cc-0x41d0 */
	calib2 = (u32 *)qfprom_read(priv->dev, "calib_redun3");
	if (IS_ERR(calib2))
		return PTR_ERR(calib2);

	dev_dbg(priv->dev, "%s: calib2: [0] = %u\n", __func__, calib2[0]);

	/* 0x4440-0x4448 */
	calib_mode = (u32 *)qfprom_read(priv->dev, "calib_redun4_5");
	if (IS_ERR(calib_mode))
		return PTR_ERR(calib_mode);

	dev_dbg(priv->dev, "%s: calib_mode: [0] = %u, [1] = %u\n",
		__func__, calib1[0], calib1[1]);

	/* 0x4464-0x4468 */
	calib_rsel = (u32 *)qfprom_read(priv->dev, "calib_rsel");
	if (IS_ERR(calib_mode))
		return PTR_ERR(calib_mode);

	dev_dbg(priv->dev, "%s: calib_rsel: [0] = %u\n", __func__, calib_rsel[0]);

	calib_redun_sel =  calib_rsel[0] & MSM8994_CAL_SEL_REDUN_MASK;
	calib_redun_sel >>= MSM8994_CAL_SEL_REDUN_SHIFT;

	if (calib_redun_sel == BKP_SEL) {
		dev_dbg(priv->dev, "%s: Calibrating in REDUN mode, calib_redun_sel = %u",
			__func__, calib_redun_sel);
		mode = calib_mode[1] & MSM8994_REDUN_SEL_MASK;

		if (mode == TWO_PT_CALIB) {
			dev_dbg(priv->dev, "%s: REDUN TWO_PT mode, mode = %u", __func__, mode);
			base0 = (calib1[0] & MSM8994_BASE0_REDUN_MASK) >> MSM8994_BASE0_REDUN_SHIFT;
			base1 = (calib1[0] & MSM8994_BASE1_BIT0_REDUN_MASK) >> MSM8994_BASE1_BIT0_REDUN_SHIFT_COMPUTE;
			base1 |= calib1[1] & MSM8994_BASE1_BIT1_9_REDUN_MASK;
			p[0] = (calib1[1] & MSM8994_S0_REDUN_MASK) >> MSM8994_S0_REDUN_SHIFT;
			p[1] = (calib1[1] & MSM8994_S1_REDUN_MASK) >> MSM8994_S1_REDUN_SHIFT;
			p[2] = (calib1[1] & MSM8994_S2_REDUN_MASK) >> MSM8994_S2_REDUN_SHIFT;
			p[3] = (calib1[1] & MSM8994_S3_REDUN_MASK) >> MSM8994_S3_REDUN_SHIFT;
			p[4] = (calib1[1] & MSM8994_S4_REDUN_MASK) >> MSM8994_S4_REDUN_SHIFT;
			p[5] = (calib1[1] & MSM8994_S5_REDUN_MASK_BIT0_2) >> MSM8994_S5_REDUN_SHIFT_BIT0_2;
			p[5] |= (calib2[0] & MSM8994_S5_REDUN_MASK_BIT3) >> MSM8994_S5_REDUN_SHIFT_BIT3;
			p[6] = (calib2[0] & MSM8994_S6_REDUN_MASK) >> MSM8994_S6_REDUN_SHIFT;
			p[7] = (calib2[0] & MSM8994_S7_REDUN_MASK) >> MSM8994_S7_REDUN_SHIFT;
			p[8] = (calib2[0] & MSM8994_S8_REDUN_MASK) >> MSM8994_S8_REDUN_SHIFT;
			p[9] = (calib2[0] & MSM8994_S9_REDUN_MASK) >> MSM8994_S9_REDUN_SHIFT;
			p[10] = (calib2[0] & MSM8994_S10_REDUN_MASK) >> MSM8994_S10_REDUN_SHIFT;
			p[11] = (calib2[0] & MSM8994_S11_REDUN_MASK) >> MSM8994_S11_REDUN_SHIFT;
			p[12] = (calib2[0] & MSM8994_S12_REDUN_MASK) >> MSM8994_S12_REDUN_SHIFT;
			p[13] = (calib2[0] & MSM8994_S13_REDUN_MASK) >> MSM8994_S13_REDUN_SHIFT;
			p[14] = (calib2[0] & MSM8994_S14_REDUN_MASK) >> MSM8994_S14_REDUN_SHIFT;
			p[15] = (calib2[0] & MSM8994_S15_REDUN_MASK) >> MSM8994_S15_REDUN_SHIFT;
		} else {
			dev_dbg(priv->dev, "%s: REDUN NON-TWO_PT mode, mode = %u", __func__, mode);
		}
	} else {
		dev_dbg(priv->dev, "%s: Calibrating in NOT-REDUN mode, calib_redun_sel = %u",
			__func__, calib_redun_sel);
		mode = (calib0[2] & MSM8994_CAL_SEL_MASK) >> MSM8994_CAL_SEL_SHIFT;

		if (mode == TWO_PT_CALIB) {
			dev_dbg(priv->dev, "%s: NOT-REDUN TWO_PT mode, mode = %u", __func__, mode);
			base0 = (calib0[0] & MSM8994_BASE0_MASK) >> MSM8994_BASE0_SHIFT;
			base1 = (calib0[0] & MSM8994_BASE1_MASK) >> MSM8994_BASE1_SHIFT;
			p[0] = (calib0[0] & MSM8994_S0_MASK) >> MSM8994_S0_SHIFT;
			p[1] = (calib0[0] & MSM8994_S1_MASK) >> MSM8994_S1_SHIFT;
			p[2] = (calib0[1] & MSM8994_S2_MASK) >> MSM8994_S2_SHIFT;
			p[3] = (calib0[1] & MSM8994_S3_MASK) >> MSM8994_S3_SHIFT;
			p[4] = (calib0[1] & MSM8994_S4_MASK) >> MSM8994_S4_SHIFT;
			p[5] = (calib0[1] & MSM8994_S5_MASK) >> MSM8994_S5_SHIFT;
			p[6] = (calib0[1] & MSM8994_S6_MASK) >> MSM8994_S6_SHIFT;
			p[7] = (calib0[1] & MSM8994_S7_MASK) >> MSM8994_S7_SHIFT;
			p[8] = (calib0[1] & MSM8994_S8_MASK) >> MSM8994_S8_SHIFT;
			p[9] = (calib0[1] & MSM8994_S9_MASK) >> MSM8994_S9_SHIFT;
			p[10] = (calib0[1] & MSM8994_S10_MASK) >> MSM8994_S10_SHIFT;
			p[11] = (calib0[2] & MSM8994_S11_MASK) >> MSM8994_S11_SHIFT;
			p[12] = (calib0[2] & MSM8994_S12_MASK) >> MSM8994_S12_SHIFT;
			p[13] = (calib0[2] & MSM8994_S13_MASK) >> MSM8994_S13_SHIFT;
			p[14] = (calib0[2] & MSM8994_S14_MASK) >> MSM8994_S14_SHIFT;
			p[15] = (calib0[2] & MSM8994_S15_MASK) >> MSM8994_S15_SHIFT;
		} else {
			dev_dbg(priv->dev, "%s: NOT-REDUN NON-TWO_PT mode, mode = %u", __func__, mode);
			for (i = 0; i < 16; i++)
				p[i] = 532;
		}
	}

	/* 8992 features less sensors and remaps some */
	if (priv->num_sensors == 13) {
		p[6] = p[7];
		p[7] = p[9];
		p[8] = p[10];
		p[9] = p[11];
		p[10] = p[12];
		p[11] = p[13];
		p[12] = p[14];
	}

	compute_intercept_slope_8994(priv, base0, base1, p, mode);
	kfree(calib0);
	kfree(calib1);
	kfree(calib2);
	kfree(calib_mode);

	return 0;
}

/* v1.x: msm8956/8976, msm8994 (v1.2), qcs404/qcs405 */

static struct tsens_features tsens_v1_feat = {
	.ver_major	= VER_1_X,
	.crit_int	= 0,
	.adc		= 1,
	.srot_split	= 1,
	.max_sensors	= 16,
};

static const struct reg_field tsens_v1_regfields[MAX_REGFIELDS] = {
	/* ----- SROT ------ */
	/* VERSION */
	[VER_MAJOR] = REG_FIELD(SROT_HW_VER_OFF, 28, 31),
	[VER_MINOR] = REG_FIELD(SROT_HW_VER_OFF, 16, 27),
	[VER_STEP]  = REG_FIELD(SROT_HW_VER_OFF,  0, 15),
	/* CTRL_OFFSET */
	[TSENS_EN]     = REG_FIELD(SROT_CTRL_OFF, 0,  0),
	[TSENS_SW_RST] = REG_FIELD(SROT_CTRL_OFF, 1,  1),
	[SENSOR_EN]    = REG_FIELD(SROT_CTRL_OFF, 3, 13),

	/* ----- TM ------ */
	/* INTERRUPT ENABLE */
	[INT_EN]     = REG_FIELD(TM_INT_EN_OFF, 0, 0),

	/* UPPER/LOWER TEMPERATURE THRESHOLDS */
	REG_FIELD_FOR_EACH_SENSOR16(LOW_THRESH,    TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF,  0,  9),
	REG_FIELD_FOR_EACH_SENSOR16(UP_THRESH,     TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 10, 19),

	/* UPPER/LOWER INTERRUPTS [CLEAR/STATUS] */
	REG_FIELD_FOR_EACH_SENSOR16(LOW_INT_CLEAR, TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 20, 20),
	REG_FIELD_FOR_EACH_SENSOR16(UP_INT_CLEAR,  TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 21, 21),
	[LOW_INT_STATUS_0] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  0,  0),
	[LOW_INT_STATUS_1] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  1,  1),
	[LOW_INT_STATUS_2] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  2,  2),
	[LOW_INT_STATUS_3] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  3,  3),
	[LOW_INT_STATUS_4] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  4,  4),
	[LOW_INT_STATUS_5] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  5,  5),
	[LOW_INT_STATUS_6] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  6,  6),
	[LOW_INT_STATUS_7] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  7,  7),
	[UP_INT_STATUS_0]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  8,  8),
	[UP_INT_STATUS_1]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  9,  9),
	[UP_INT_STATUS_2]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 10, 10),
	[UP_INT_STATUS_3]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 11, 11),
	[UP_INT_STATUS_4]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 12, 12),
	[UP_INT_STATUS_5]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 13, 13),
	[UP_INT_STATUS_6]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 14, 14),
	[UP_INT_STATUS_7]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 15, 15),

	/* NO CRITICAL INTERRUPT SUPPORT on v1 */

	/* Sn_STATUS */
	REG_FIELD_FOR_EACH_SENSOR16(LAST_TEMP,    TM_Sn_STATUS_OFF,  0,  9),
	REG_FIELD_FOR_EACH_SENSOR16(VALID,        TM_Sn_STATUS_OFF, 14, 14),
	/* xxx_STATUS bits: 1 == threshold violated */
	REG_FIELD_FOR_EACH_SENSOR16(MIN_STATUS,   TM_Sn_STATUS_OFF, 10, 10),
	REG_FIELD_FOR_EACH_SENSOR16(LOWER_STATUS, TM_Sn_STATUS_OFF, 11, 11),
	REG_FIELD_FOR_EACH_SENSOR16(UPPER_STATUS, TM_Sn_STATUS_OFF, 12, 12),
	/* No CRITICAL field on v1.x */
	REG_FIELD_FOR_EACH_SENSOR16(MAX_STATUS,   TM_Sn_STATUS_OFF, 13, 13),

	/* TRDY: 1=ready, 0=in progress */
	[TRDY] = REG_FIELD(TM_TRDY_OFF, 0, 0),
};

static const struct tsens_ops ops_generic_v1 = {
	.init		= init_common,
	.calibrate	= calibrate_v1,
	.get_temp	= get_temp_tsens_valid,
};

struct tsens_plat_data data_tsens_v1 = {
	.ops		= &ops_generic_v1,
	.feat		= &tsens_v1_feat,
	.fields	= tsens_v1_regfields,
};

static const struct tsens_ops ops_8976 = {
	.init		= init_common,
	.calibrate	= calibrate_8976,
	.get_temp	= get_temp_tsens_valid,
};

/* Valid for both MSM8956 and MSM8976. */
struct tsens_plat_data data_8976 = {
	.num_sensors	= 11,
	.ops		= &ops_8976,
	.hw_ids		= (unsigned int[]){0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
	.feat		= &tsens_v1_feat,
	.fields		= tsens_v1_regfields,
};

static const struct tsens_ops ops_8992 = {
	.init		= init_common,
	.calibrate	= calibrate_8994,
	.get_temp	= get_temp_tsens_valid,
};

struct tsens_plat_data data_8992 = {
	.num_sensors	= 13,
	.ops		= &ops_8992,
	.hw_ids		= (unsigned int []){ 0, 1, 2, 3, 4, 5, 7, 9, 10, 11, 12, 13, 14 },
	.feat		= &tsens_v1_feat,
	.fields	= tsens_v1_regfields,
};

static const struct tsens_ops ops_8994 = {
	.init		= init_common,
	.calibrate	= calibrate_8994,
	.get_temp	= get_temp_tsens_valid,
};

struct tsens_plat_data data_8994 = {
	.num_sensors	= 16,
	.ops		= &ops_8994,
	.hw_ids		= (unsigned int []){ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	.feat		= &tsens_v1_feat,
	.fields	= tsens_v1_regfields,
};
