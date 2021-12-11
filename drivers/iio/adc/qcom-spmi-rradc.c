// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Linaro Limited.
 *  Author: Caleb Connolly <caleb.connolly@linaro.org>
 *
 * This driver is for the Round Robin ADC found in the pmi8998 and pm660 PMICs.
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/types.h>
#include <soc/qcom/qcom-pmic.h>

#define RR_ADC_EN_CTL				0x46
#define RR_ADC_SKIN_TEMP_LSB			0x50
#define RR_ADC_SKIN_TEMP_MSB			0x51
#define RR_ADC_RR_ADC_CTL			0x52
#define RR_ADC_ADC_CTL_CONTINUOUS_SEL		BIT(3)
#define RR_ADC_ADC_LOG				0x53
#define RR_ADC_ADC_LOG_CLR_CTRL			BIT(0)

#define RR_ADC_FAKE_BATT_LOW_LSB		0x58
#define RR_ADC_FAKE_BATT_LOW_MSB		0x59
#define RR_ADC_FAKE_BATT_HIGH_LSB		0x5A
#define RR_ADC_FAKE_BATT_HIGH_MSB		0x5B

#define RR_ADC_BATT_ID_CTRL			0x60
#define RR_ADC_BATT_ID_CTRL_CHANNEL_CONV	BIT(0)
#define RR_ADC_BATT_ID_TRIGGER			0x61
#define RR_ADC_BATT_ID_TRIGGER_CTL		BIT(0)
#define RR_ADC_BATT_ID_STS			0x62
#define RR_ADC_BATT_ID_CFG			0x63
#define RR_ADC_BATT_ID_5_LSB			0x66
#define RR_ADC_BATT_ID_5_MSB			0x67
#define RR_ADC_BATT_ID_15_LSB			0x68
#define RR_ADC_BATT_ID_15_MSB			0x69
#define RR_ADC_BATT_ID_150_LSB			0x6A
#define RR_ADC_BATT_ID_150_MSB			0x6B

#define RR_ADC_BATT_THERM_CTRL			0x70
#define RR_ADC_BATT_THERM_TRIGGER		0x71
#define RR_ADC_BATT_THERM_STS			0x72
#define RR_ADC_BATT_THERM_CFG			0x73
#define RR_ADC_BATT_THERM_LSB			0x74
#define RR_ADC_BATT_THERM_MSB			0x75
#define RR_ADC_BATT_THERM_FREQ			0x76

#define RR_ADC_AUX_THERM_CTRL			0x80
#define RR_ADC_AUX_THERM_TRIGGER		0x81
#define RR_ADC_AUX_THERM_STS			0x82
#define RR_ADC_AUX_THERM_CFG			0x83
#define RR_ADC_AUX_THERM_LSB			0x84
#define RR_ADC_AUX_THERM_MSB			0x85

#define RR_ADC_SKIN_HOT				0x86
#define RR_ADC_SKIN_TOO_HOT			0x87

#define RR_ADC_AUX_THERM_C1			0x88
#define RR_ADC_AUX_THERM_C2			0x89
#define RR_ADC_AUX_THERM_C3			0x8A
#define RR_ADC_AUX_THERM_HALF_RANGE		0x8B

#define RR_ADC_USB_IN_V_CTRL			0x90
#define RR_ADC_USB_IN_V_TRIGGER			0x91
#define RR_ADC_USB_IN_V_EVERY_CYCLE		BIT(7)
#define RR_ADC_USB_IN_V_STS			0x92
#define RR_ADC_USB_IN_V_LSB			0x94
#define RR_ADC_USB_IN_V_MSB			0x95
#define RR_ADC_USB_IN_I_CTRL			0x98
#define RR_ADC_USB_IN_I_TRIGGER			0x99
#define RR_ADC_USB_IN_I_STS			0x9A
#define RR_ADC_USB_IN_I_LSB			0x9C
#define RR_ADC_USB_IN_I_MSB			0x9D

#define RR_ADC_DC_IN_V_CTRL			0xA0
#define RR_ADC_DC_IN_V_TRIGGER			0xA1
#define RR_ADC_DC_IN_V_STS			0xA2
#define RR_ADC_DC_IN_V_LSB			0xA4
#define RR_ADC_DC_IN_V_MSB			0xA5
#define RR_ADC_DC_IN_I_CTRL			0xA8
#define RR_ADC_DC_IN_I_TRIGGER			0xA9
#define RR_ADC_DC_IN_I_STS			0xAA
#define RR_ADC_DC_IN_I_LSB			0xAC
#define RR_ADC_DC_IN_I_MSB			0xAD

#define RR_ADC_PMI_DIE_TEMP_CTRL		0xB0
#define RR_ADC_PMI_DIE_TEMP_TRIGGER		0xB1
#define RR_ADC_PMI_DIE_TEMP_STS			0xB2
#define RR_ADC_PMI_DIE_TEMP_CFG			0xB3
#define RR_ADC_PMI_DIE_TEMP_LSB			0xB4
#define RR_ADC_PMI_DIE_TEMP_MSB			0xB5

#define RR_ADC_CHARGER_TEMP_CTRL		0xB8
#define RR_ADC_CHARGER_TEMP_TRIGGER		0xB9
#define RR_ADC_CHARGER_TEMP_STS			0xBA
#define RR_ADC_CHARGER_TEMP_CFG			0xBB
#define RR_ADC_CHARGER_TEMP_LSB			0xBC
#define RR_ADC_CHARGER_TEMP_MSB			0xBD
#define RR_ADC_CHARGER_HOT			0xBE
#define RR_ADC_CHARGER_TOO_HOT			0xBF

#define RR_ADC_GPIO_CTRL			0xC0
#define RR_ADC_GPIO_TRIGGER			0xC1
#define RR_ADC_GPIO_STS				0xC2
#define RR_ADC_GPIO_LSB				0xC4
#define RR_ADC_GPIO_MSB				0xC5

#define RR_ADC_ATEST_CTRL			0xC8
#define RR_ADC_ATEST_TRIGGER			0xC9
#define RR_ADC_ATEST_STS			0xCA
#define RR_ADC_ATEST_LSB			0xCC
#define RR_ADC_ATEST_MSB			0xCD
#define RR_ADC_SEC_ACCESS			0xD0

#define RR_ADC_PERPH_RESET_CTL2			0xD9
#define RR_ADC_PERPH_RESET_CTL3			0xDA
#define RR_ADC_PERPH_RESET_CTL4			0xDB
#define RR_ADC_INT_TEST1			0xE0
#define RR_ADC_INT_TEST_VAL			0xE1

#define RR_ADC_TM_TRIGGER_CTRLS			0xE2
#define RR_ADC_TM_ADC_CTRLS			0xE3
#define RR_ADC_TM_CNL_CTRL			0xE4
#define RR_ADC_TM_BATT_ID_CTRL			0xE5
#define RR_ADC_TM_THERM_CTRL			0xE6
#define RR_ADC_TM_CONV_STS			0xE7
#define RR_ADC_TM_ADC_READ_LSB			0xE8
#define RR_ADC_TM_ADC_READ_MSB			0xE9
#define RR_ADC_TM_ATEST_MUX_1			0xEA
#define RR_ADC_TM_ATEST_MUX_2			0xEB
#define RR_ADC_TM_REFERENCES			0xED
#define RR_ADC_TM_MISC_CTL			0xEE
#define RR_ADC_TM_RR_CTRL			0xEF

#define RR_ADC_BATT_ID_5_MA			5
#define RR_ADC_BATT_ID_15_MA			15
#define RR_ADC_BATT_ID_150_MA			150
#define RR_ADC_BATT_ID_RANGE			820

#define RR_ADC_BITS				10
#define RR_ADC_MAX_READINGS			(1 << RR_ADC_BITS)
#define RR_ADC_FS_VOLTAGE_MV			2500

/* BATT_THERM 0.25K/LSB */
#define RR_ADC_BATT_THERM_LSB_K			4

#define RR_ADC_TEMP_FS_VOLTAGE_NUM		5000000
#define RR_ADC_TEMP_FS_VOLTAGE_DEN		3
#define RR_ADC_DIE_TEMP_OFFSET			601400
#define RR_ADC_DIE_TEMP_SLOPE			2
#define RR_ADC_DIE_TEMP_OFFSET_MILLI_DEGC	25000

#define RR_ADC_CHG_TEMP_GF_OFFSET_UV		1303168
#define RR_ADC_CHG_TEMP_GF_SLOPE_UV_PER_C	3784
#define RR_ADC_CHG_TEMP_SMIC_OFFSET_UV		1338433
#define RR_ADC_CHG_TEMP_SMIC_SLOPE_UV_PER_C	3655
#define RR_ADC_CHG_TEMP_660_GF_OFFSET_UV	1309001
#define RR_ADC_CHG_TEMP_660_GF_SLOPE_UV_PER_C	3403
#define RR_ADC_CHG_TEMP_660_SMIC_OFFSET_UV	1295898
#define RR_ADC_CHG_TEMP_660_SMIC_SLOPE_UV_PER_C	3596
#define RR_ADC_CHG_TEMP_660_MGNA_OFFSET_UV	1314779
#define RR_ADC_CHG_TEMP_660_MGNA_SLOPE_UV_PER_C	3496
#define RR_ADC_CHG_TEMP_OFFSET_MILLI_DEGC	25000
#define RR_ADC_CHG_THRESHOLD_SCALE		4

#define RR_ADC_VOLT_INPUT_FACTOR		8
#define RR_ADC_CURR_INPUT_FACTOR		2000
#define RR_ADC_CURR_USBIN_INPUT_FACTOR_MIL	1886
#define RR_ADC_CURR_USBIN_660_FACTOR_MIL	9
#define RR_ADC_CURR_USBIN_660_UV_VAL		579500

#define RR_ADC_SCALE_MILLI_FACTOR		1000
#define RR_ADC_KELVINMIL_CELSIUSMIL		273150

#define RR_ADC_GPIO_FS_RANGE			5000
#define RR_ADC_COHERENT_CHECK_RETRY		5
#define RR_ADC_MAX_CONTINUOUS_BUFFER_LEN	16

#define RR_ADC_STS_CHANNEL_READING_MASK		0x3
#define RR_ADC_STS_CHANNEL_STS			0x2

#define RR_ADC_CONV_TIME_MIN_US			499
#define RR_ADC_CONV_TIME_MAX_US			501
#define RR_ADC_CONV_MAX_RETRY_CNT		100
#define RR_ADC_TP_REV_VERSION1			21
#define RR_ADC_TP_REV_VERSION2			29
#define RR_ADC_TP_REV_VERSION3			32

#define BATT_ID_SETTLE_SHIFT			5
#define RRADC_BATT_ID_DELAY_MAX			8

/*
 * Used to index rradc_chip.chans[] array.
 */
enum rradc_channel_id {
	RR_ADC_BATT_ID = 0,
	RR_ADC_BATT_THERM,
	RR_ADC_SKIN_TEMP,
	RR_ADC_USBIN_I,
	RR_ADC_USBIN_V,
	RR_ADC_DCIN_I,
	RR_ADC_DCIN_V,
	RR_ADC_DIE_TEMP,
	RR_ADC_CHG_TEMP,
	RR_ADC_GPIO,
	RR_ADC_CHG_HOT_TEMP,
	RR_ADC_CHG_TOO_HOT_TEMP,
	RR_ADC_SKIN_HOT_TEMP,
	RR_ADC_SKIN_TOO_HOT_TEMP,
	RR_ADC_MAX
};

struct rradc_chip;

/**
 * struct rradc_channel - rradc channel data
 * @datasheet_name:	Name of the channel
 * @type:		Channel type
 * @info_mask:		Channel mask
 * @enabled:		Enable/disable flag
 * @lsb:		Channel least significant byte
 * @msb:		Channel most significant byte
 * @status:		Channel status address
 * @size:		number of bytes to read
 * @trigger_addr:	Trigger address
 * @trigger_mask:	Trigger mask
 * @scale:		Channel scale callback
 */
struct rradc_channel {
	const char			*datasheet_name;
	enum iio_chan_type		type;
	long				info_mask;
	bool				enabled;
	u8				lsb;
	u8				msb;
	u8				status;
	int				size;
	int				trigger_addr;
	int				trigger_mask;
	int (*scale)(struct rradc_chip *chip, u16 adc_code, int *result);
};

struct rradc_chip {
	struct device			*dev;
	struct qcom_spmi_pmic		*pmic;
	struct mutex			lock;
	struct regmap			*regmap;
	u32				base;
	int				batt_id_delay;
	struct iio_chan_spec		*iio_chans;
	unsigned int			nchannels;
	struct rradc_channel		*chans;
	u16				batt_id_data;
};

static const int batt_id_delays[] = {0, 1, 4, 12, 20, 40, 60, 80};

static int rradc_masked_write(struct rradc_chip *chip, u16 addr, u8 mask,
						u8 val)
{
	int ret;

	ret = regmap_update_bits(chip->regmap, chip->base + addr,
								mask, val);
	if (ret) {
		dev_err(chip->dev, "spmi write failed: addr=%03X, ret=%d\n", addr, ret);
		return ret;
	}

	return ret;
}

static int rradc_read(struct rradc_chip *chip, u16 addr, u8 *data, int len)
{
	int ret = 0, retry_cnt = 0, i = 0;
	u8 data_check[RR_ADC_MAX_CONTINUOUS_BUFFER_LEN];
	bool coherent_err = false;

	if (len > RR_ADC_MAX_CONTINUOUS_BUFFER_LEN) {
		dev_err(chip->dev, "Increase the buffer length\n");
		return -EINVAL;
	}

	while (retry_cnt < RR_ADC_COHERENT_CHECK_RETRY) {
		ret = regmap_bulk_read(chip->regmap, chip->base + addr,
							data, len);
		if (ret < 0) {
			dev_err(chip->dev, "rr_adc reg 0x%x failed :%d\n", addr, ret);
			return ret;
		}

		ret = regmap_bulk_read(chip->regmap, chip->base + addr,
							data_check, len);
		if (ret < 0) {
			dev_err(chip->dev, "rr_adc reg 0x%x failed :%d\n", addr, ret);
			return ret;
		}

		for (i = 0; i < len; i++) {
			if (data[i] != data_check[i])
				coherent_err = true;
		}

		if (coherent_err) {
			retry_cnt++;
			coherent_err = false;
			dev_dbg(chip->dev, "%s() retry_cnt = %d\n", __func__, retry_cnt);
		} else {
			break;
		}
	}

	if (retry_cnt == RR_ADC_COHERENT_CHECK_RETRY)
		dev_err(chip->dev, "Retry exceeded for coherrency check\n");

	return ret;
}

static int rradc_get_fab_coeff(struct rradc_chip *chip,
		int64_t *offset, int64_t *slope)
{
	if (chip->pmic->subtype == PM660_SUBTYPE) {
		switch (chip->pmic->fab_id) {
		case PM660_FAB_ID_GF:
			*offset = RR_ADC_CHG_TEMP_660_GF_OFFSET_UV;
			*slope = RR_ADC_CHG_TEMP_660_GF_SLOPE_UV_PER_C;
			break;
		case PM660_FAB_ID_TSMC:
			*offset = RR_ADC_CHG_TEMP_660_SMIC_OFFSET_UV;
			*slope = RR_ADC_CHG_TEMP_660_SMIC_SLOPE_UV_PER_C;
			break;
		default:
			*offset = RR_ADC_CHG_TEMP_660_MGNA_OFFSET_UV;
			*slope = RR_ADC_CHG_TEMP_660_MGNA_SLOPE_UV_PER_C;
	}
	} else if (chip->pmic->subtype == PMI8998_SUBTYPE) {
		switch (chip->pmic->fab_id) {
		case PMI8998_FAB_ID_GF:
			*offset = RR_ADC_CHG_TEMP_GF_OFFSET_UV;
			*slope = RR_ADC_CHG_TEMP_GF_SLOPE_UV_PER_C;
			break;
		case PMI8998_FAB_ID_SMIC:
			*offset = RR_ADC_CHG_TEMP_SMIC_OFFSET_UV;
			*slope = RR_ADC_CHG_TEMP_SMIC_SLOPE_UV_PER_C;
			break;
		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int rradc_post_process_batt_id(struct rradc_chip *chip, u16 adc_code,
			int *result_ohms)
{
	uint32_t current_value;
	int64_t r_id;

	current_value = chip->batt_id_data;
	r_id = ((int64_t)adc_code * RR_ADC_FS_VOLTAGE_MV);
	r_id = div64_s64(r_id, (RR_ADC_MAX_READINGS * current_value));
	*result_ohms = (r_id * RR_ADC_SCALE_MILLI_FACTOR);

	return 0;
}

static int rradc_post_process_therm(struct rradc_chip *chip, u16 adc_code,
			int *result_millidegc)
{
	int64_t temp;

	/* K = code/4 */
	temp = ((int64_t)adc_code * RR_ADC_SCALE_MILLI_FACTOR);
	temp = div64_s64(temp, RR_ADC_BATT_THERM_LSB_K);
	*result_millidegc = temp - RR_ADC_KELVINMIL_CELSIUSMIL;

	return 0;
}

static int rradc_post_process_volt(struct rradc_chip *chip, u16 adc_code,
			int *result_uv)
{
	int64_t uv = 0;

	/* 8x input attenuation; 2.5V ADC full scale */
	uv = ((int64_t)adc_code * RR_ADC_VOLT_INPUT_FACTOR);
	uv *= (RR_ADC_FS_VOLTAGE_MV * RR_ADC_SCALE_MILLI_FACTOR);
	uv = div64_s64(uv, RR_ADC_MAX_READINGS);
	*result_uv = uv;

	return 0;
}

static int rradc_post_process_usbin_curr(struct rradc_chip *chip, u16 adc_code,
			int *result_ua)
{
	int64_t ua = 0;

	/* scale * V/A; 2.5V ADC full scale */
	ua = ((int64_t)adc_code * RR_ADC_CURR_USBIN_INPUT_FACTOR_MIL);
	ua *= (RR_ADC_FS_VOLTAGE_MV * RR_ADC_SCALE_MILLI_FACTOR);
	ua = div64_s64(ua, (RR_ADC_MAX_READINGS * 10));
	*result_ua = ua;

	return 0;
}

static int rradc_post_process_dcin_curr(struct rradc_chip *chip, u16 adc_code,
			int *result_ua)
{
	int64_t ua = 0;

	/* 0.5 V/A; 2.5V ADC full scale */
	ua = ((int64_t)adc_code * RR_ADC_CURR_INPUT_FACTOR);
	ua *= (RR_ADC_FS_VOLTAGE_MV * RR_ADC_SCALE_MILLI_FACTOR);
	ua = div64_s64(ua, (RR_ADC_MAX_READINGS * 1000));
	*result_ua = ua;

	return 0;
}

static int rradc_post_process_die_temp(struct rradc_chip *chip, u16 adc_code,
			int *result_millidegc)
{
	int64_t temp = 0;

	temp = ((int64_t)adc_code * RR_ADC_TEMP_FS_VOLTAGE_NUM);
	temp = div64_s64(temp, (RR_ADC_TEMP_FS_VOLTAGE_DEN *
					RR_ADC_MAX_READINGS));
	temp -= RR_ADC_DIE_TEMP_OFFSET;
	temp = div64_s64(temp, RR_ADC_DIE_TEMP_SLOPE);
	temp += RR_ADC_DIE_TEMP_OFFSET_MILLI_DEGC;
	*result_millidegc = temp;

	return 0;
}

static int rradc_post_process_chg_temp_hot(struct rradc_chip *chip, u16 adc_code,
			int *result_millidegc)
{
	int64_t uv = 0, offset = 0, slope = 0;
	int ret = 0;

	ret = rradc_get_fab_coeff(chip, &offset, &slope);
	if (ret < 0) {
		dev_err(chip->dev, "Unable to get fab id coefficients\n");
		return -EINVAL;
	}

	uv = (int64_t) adc_code * RR_ADC_CHG_THRESHOLD_SCALE;
	uv = uv * RR_ADC_TEMP_FS_VOLTAGE_NUM;
	uv = div64_s64(uv, (RR_ADC_TEMP_FS_VOLTAGE_DEN *
					RR_ADC_MAX_READINGS));
	uv = offset - uv;
	uv = div64_s64((uv * RR_ADC_SCALE_MILLI_FACTOR), slope);
	uv = uv + RR_ADC_CHG_TEMP_OFFSET_MILLI_DEGC;
	*result_millidegc = uv;

	return 0;
}

static int rradc_post_process_skin_temp_hot(struct rradc_chip *chip, u16 adc_code,
			int *result_millidegc)
{
	int64_t temp = 0;

	temp = (int64_t) adc_code;
	temp = div64_s64(temp, 2);
	temp = temp - 30;
	temp *= RR_ADC_SCALE_MILLI_FACTOR;
	*result_millidegc = temp;

	return 0;
}

static int rradc_post_process_chg_temp(struct rradc_chip *chip, u16 adc_code,
			int *result_millidegc)
{
	int64_t uv = 0, offset = 0, slope = 0;
	int ret = 0;

	ret = rradc_get_fab_coeff(chip, &offset, &slope);
	if (ret < 0) {
		dev_err(chip->dev, "Unable to get fab id coefficients\n");
		return -EINVAL;
	}

	uv = ((int64_t) adc_code * RR_ADC_TEMP_FS_VOLTAGE_NUM);
	uv = div64_s64(uv, (RR_ADC_TEMP_FS_VOLTAGE_DEN *
					RR_ADC_MAX_READINGS));
	uv = offset - uv;
	uv = div64_s64((uv * RR_ADC_SCALE_MILLI_FACTOR), slope);
	uv += RR_ADC_CHG_TEMP_OFFSET_MILLI_DEGC;
	*result_millidegc = uv;

	return 0;
}

static int rradc_post_process_gpio(struct rradc_chip *chip, u16 adc_code,
			int *result_mv)
{
	int64_t mv = 0;

	/* 5V ADC full scale, 10 bit */
	mv = ((int64_t)adc_code * RR_ADC_GPIO_FS_RANGE);
	mv = div64_s64(mv, RR_ADC_MAX_READINGS);
	*result_mv = mv;

	return 0;
}

static int rradc_enable_continuous_mode(struct rradc_chip *chip)
{
	int ret = 0;

	/* Clear channel log */
	ret = rradc_masked_write(chip, RR_ADC_ADC_LOG,
			RR_ADC_ADC_LOG_CLR_CTRL,
			RR_ADC_ADC_LOG_CLR_CTRL);
	if (ret < 0) {
		dev_err(chip->dev, "log ctrl update to clear failed:%d\n", ret);
		return ret;
	}

	ret = rradc_masked_write(chip, RR_ADC_ADC_LOG,
		RR_ADC_ADC_LOG_CLR_CTRL, 0);
	if (ret < 0) {
		dev_err(chip->dev, "log ctrl update to not clear failed:%d\n", ret);
		return ret;
	}

	/* Switch to continuous mode */
	ret = rradc_masked_write(chip, RR_ADC_RR_ADC_CTL,
		RR_ADC_ADC_CTL_CONTINUOUS_SEL,
		RR_ADC_ADC_CTL_CONTINUOUS_SEL);
	if (ret < 0) {
		dev_err(chip->dev, "Update to continuous mode failed:%d\n", ret);
		return ret;
	}

	return ret;
}

static int rradc_disable_continuous_mode(struct rradc_chip *chip)
{
	int ret = 0;

	/* Switch to non continuous mode */
	ret = rradc_masked_write(chip, RR_ADC_RR_ADC_CTL,
			RR_ADC_ADC_CTL_CONTINUOUS_SEL, 0);
	if (ret < 0) {
		dev_err(chip->dev, "Update to non-continuous mode failed:%d\n", ret);
		return ret;
	}

	return ret;
}

static bool rradc_is_ready(struct rradc_chip *chip,
		enum rradc_channel_id chan_id)
{
	struct rradc_channel *chan = &chip->chans[chan_id];
	int ret = 0;
	u8 status = 0, mask;

	/* BATT_ID STS bit does not get set initially */
	switch (chan_id) {
	case RR_ADC_BATT_ID:
		mask = RR_ADC_STS_CHANNEL_STS;
		break;
	default:
		mask = RR_ADC_STS_CHANNEL_READING_MASK;
		break;
	}

	ret = rradc_read(chip, chan->status, &status, 1);
	if (ret < 0 || !(status & mask)) {
		dev_dbg(chip->dev, "Chip not ready, ret=%d\n", status);
		return false;
	}

	return true;
}

static int rradc_read_status_in_cont_mode(struct rradc_chip *chip,
		enum rradc_channel_id chan_id)
{
	struct rradc_channel *chan = &chip->chans[chan_id];
	int ret = 0;

	ret = rradc_masked_write(chip, chan->trigger_addr,
				chan->trigger_mask,
				chan->trigger_mask);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to apply trigger for channel '%s' ret=%d\n",
			chan->datasheet_name, ret);
		return ret;
	}

	ret = rradc_enable_continuous_mode(chip);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to switch to continuous mode\n");
		goto disable_trigger;
	}

	if (!rradc_is_ready(chip, chan_id))
		dev_err(chip->dev, "%s() channel isn't ready: %d\n", __func__, ret);

	ret = rradc_disable_continuous_mode(chip);
	if (ret < 0)
		dev_err(chip->dev, "Failed to switch to non continuous mode\n");

disable_trigger:
	ret = rradc_masked_write(chip, chan->trigger_addr,
				chan->trigger_mask,
				0);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to apply trigger for channel '%s' ret=%d\n",
			chan->datasheet_name, ret);
	}

	return ret;
}

static int rradc_prepare_batt_id_conversion(struct rradc_chip *chip,
		enum rradc_channel_id chan_id, u16 *data)
{
	int ret = 0, batt_id_delay;

	ret = rradc_masked_write(chip, RR_ADC_BATT_ID_CTRL,
				RR_ADC_BATT_ID_CTRL_CHANNEL_CONV,
				RR_ADC_BATT_ID_CTRL_CHANNEL_CONV);
	if (ret < 0) {
		dev_err(chip->dev, "Enabling BATT ID channel failed:%d\n", ret);
		return ret;
	}

	if (chip->batt_id_delay != -EINVAL) {
		batt_id_delay = chip->batt_id_delay << BATT_ID_SETTLE_SHIFT;
		ret = rradc_masked_write(chip, RR_ADC_BATT_ID_CFG,
				batt_id_delay, batt_id_delay);
		if (ret < 0)
			dev_err(chip->dev, "BATT_ID settling time config failed:%d\n", ret);
	}

	ret = rradc_masked_write(chip, RR_ADC_BATT_ID_TRIGGER,
				RR_ADC_BATT_ID_TRIGGER_CTL,
				RR_ADC_BATT_ID_TRIGGER_CTL);
	if (ret < 0) {
		dev_err(chip->dev, "BATT_ID trigger set failed:%d\n", ret);
		goto out_disable_batt_id;
	}

	ret = rradc_read_status_in_cont_mode(chip, chan_id);
	if (ret < 0)
		dev_err(chip->dev, "Error reading in continuous mode:%d\n", ret);

	ret = rradc_masked_write(chip, RR_ADC_BATT_ID_TRIGGER,
			RR_ADC_BATT_ID_TRIGGER_CTL, 0);
	if (ret < 0)
		dev_err(chip->dev, "BATT_ID trigger re-set failed:%d\n", ret);

out_disable_batt_id:
	ret = rradc_masked_write(chip, RR_ADC_BATT_ID_CTRL,
				RR_ADC_BATT_ID_CTRL_CHANNEL_CONV, 0);
	if (ret < 0)
		dev_err(chip->dev, "Disabling BATT ID channel failed:%d\n", ret);

	return ret;
}

static int rradc_do_conversion(struct rradc_chip *chip, enum rradc_channel_id chan_id, u16 *data)
{
	struct rradc_channel *chan = &chip->chans[chan_id];
	int ret = 0;
	u8 buf[6];
	u16 batt_id_5 = 0, batt_id_15 = 0, batt_id_150 = 0;

	mutex_lock(&chip->lock);

	/*
	 * First we need to do channel specific configuration.
	 */
	switch (chan_id) {
	case RR_ADC_BATT_ID:
		ret = rradc_prepare_batt_id_conversion(chip, chan_id, data);
		if (ret < 0) {
			dev_err(chip->dev, "Battery ID conversion failed:%d\n", ret);
			goto unlock_out;
		}
		break;

	case RR_ADC_USBIN_V:
	case RR_ADC_DIE_TEMP:
		ret = rradc_read_status_in_cont_mode(chip, chan_id);
		if (ret < 0) {
			dev_err(chip->dev, "Error reading in continuous mode:%d\n", ret);
			goto unlock_out;
		}
		break;
	case RR_ADC_CHG_HOT_TEMP:
	case RR_ADC_CHG_TOO_HOT_TEMP:
	case RR_ADC_SKIN_HOT_TEMP:
	case RR_ADC_SKIN_TOO_HOT_TEMP:
		break;
	default:
		if (!rradc_is_ready(chip, chan_id)) {
			dev_err(chip->dev, "%s() channel '%s' is not ready\n", __func__,
				chan->datasheet_name);
			ret = -ENODATA;
			goto unlock_out;
		}
		break;
	}

	/*
	 * Then we can read the data.
	 */
	ret = rradc_read(chip, chan->lsb, buf, chan->size);
	if (ret) {
		dev_err(chip->dev, "read data failed\n");
		goto unlock_out;
	}

	/*
	 * For the battery ID we read the register for every ID ADC and then
	 * see which one is actually connected.
	 */
	if (chan_id == RR_ADC_BATT_ID) {
		batt_id_150 = (buf[5] << 8) | buf[4];
		batt_id_15 = (buf[3] << 8) | buf[2];
		batt_id_5 = (buf[1] << 8) | buf[0];
		if ((!batt_id_150) && (!batt_id_15) && (!batt_id_5)) {
			dev_err(chip->dev, "Invalid batt_id values with all zeros\n");
			ret = -EINVAL;
			goto unlock_out;
		}

		if (batt_id_150 <= RR_ADC_BATT_ID_RANGE) {
			*data = batt_id_150;
			chip->batt_id_data = RR_ADC_BATT_ID_150_MA;
		} else if (batt_id_15 <= RR_ADC_BATT_ID_RANGE) {
			*data = batt_id_15;
			chip->batt_id_data = RR_ADC_BATT_ID_15_MA;
		} else {
			*data = batt_id_5;
			chip->batt_id_data = RR_ADC_BATT_ID_5_MA;
		}
	} else {
		/*
		 * All of the other channels are either 1 or 2 bytes.
		 * We can rely on the second byte being 0 for 1-byte channels.
		 */
		*data = (buf[1] << 8) | buf[0];
	}

unlock_out:
	mutex_unlock(&chip->lock);

	return ret;
}

static int rradc_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan_spec, int *val, int *val2,
			 long mask)
{
	struct rradc_chip *chip = iio_priv(indio_dev);
	struct rradc_channel *chan;
	int ret;
	u16 adc_code;

	if (chan_spec->address >= RR_ADC_MAX) {
		dev_err(chip->dev, "Invalid channel index:%ld\n", chan_spec->address);
		return -EINVAL;
	}

	chan = &chip->chans[chan_spec->address];
	ret = rradc_do_conversion(chip, chan_spec->address, &adc_code);
	if (ret < 0)
		return ret;
	dev_dbg(chip->dev, "channel: %s, raw adc value = %d\n", chan->datasheet_name, adc_code);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = (int) adc_code;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		chan->scale(chip, adc_code, val);
		dev_dbg(chip->dev, "%s() processed chan_id = %ld, data = %d, ret = %d",
			__func__, chan_spec->address, *val, ret);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info rradc_info = {
	.read_raw	= &rradc_read_raw,
};

#define CHAN_INIT(_name, _type, _mask, _scale, _lsb, _msb, _sts, _bytes, _trig, _trig_mask) \
	{ \
		.datasheet_name = _name, \
		.type = _type, \
		.info_mask = _mask, \
		.scale = _scale, \
		.lsb = _lsb, \
		.msb = _msb, \
		.status = _sts, \
		.enabled = true, \
		.size = _bytes, \
		.trigger_addr = _trig, \
		.trigger_mask = _trig_mask, \
	}

static int rradc_init_channels(struct rradc_chip *chip)
{
	int i = 0;

	chip->nchannels = RR_ADC_MAX;
	chip->chans = devm_kcalloc(chip->dev, chip->nchannels,
			sizeof(*chip->chans), GFP_KERNEL);
	if (!chip->chans)
		return -ENOMEM;

	chip->iio_chans = devm_kcalloc(chip->dev, chip->nchannels,
				       sizeof(*chip->iio_chans), GFP_KERNEL);
	if (!chip->iio_chans)
		return -ENOMEM;

	chip->chans[RR_ADC_BATT_ID] =
		(struct rradc_channel) CHAN_INIT("batt_id", IIO_RESISTANCE,
			BIT(IIO_CHAN_INFO_PROCESSED), rradc_post_process_batt_id,
			RR_ADC_BATT_ID_5_LSB, RR_ADC_BATT_ID_5_MSB, RR_ADC_BATT_ID_STS, 6,
			RR_ADC_BATT_ID_TRIGGER, RR_ADC_BATT_ID_TRIGGER_CTL);
	chip->chans[RR_ADC_BATT_THERM] =
		(struct rradc_channel) CHAN_INIT("batt_therm", IIO_TEMP,
			BIT(IIO_CHAN_INFO_RAW), rradc_post_process_therm,
			RR_ADC_BATT_THERM_LSB, RR_ADC_BATT_THERM_MSB, RR_ADC_BATT_THERM_STS, 2,
			RR_ADC_BATT_THERM_TRIGGER, RR_ADC_BATT_THERM_TRIGGER);
	chip->chans[RR_ADC_SKIN_TEMP] =
		(struct rradc_channel) CHAN_INIT("skin_temp", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_therm,
			RR_ADC_SKIN_TEMP_LSB, RR_ADC_SKIN_TEMP_MSB, RR_ADC_AUX_THERM_STS, 2,
			RR_ADC_AUX_THERM_TRIGGER, RR_ADC_AUX_THERM_TRIGGER);
	chip->chans[RR_ADC_USBIN_I] =
		(struct rradc_channel) CHAN_INIT("usbin_i", IIO_CURRENT,
			BIT(IIO_CHAN_INFO_PROCESSED), rradc_post_process_usbin_curr,
			RR_ADC_USB_IN_I_LSB, RR_ADC_USB_IN_I_MSB, RR_ADC_USB_IN_I_STS, 2,
			RR_ADC_USB_IN_I_TRIGGER, RR_ADC_USB_IN_I_TRIGGER);
	chip->chans[RR_ADC_USBIN_V] =
		(struct rradc_channel) CHAN_INIT("usbin_v", IIO_VOLTAGE,
			BIT(IIO_CHAN_INFO_PROCESSED), rradc_post_process_volt,
			RR_ADC_USB_IN_V_LSB, RR_ADC_USB_IN_V_MSB, RR_ADC_USB_IN_V_STS, 2,
			RR_ADC_USB_IN_V_TRIGGER, RR_ADC_USB_IN_V_EVERY_CYCLE);
	chip->chans[RR_ADC_DCIN_I] =
		(struct rradc_channel) CHAN_INIT("dcin_i", IIO_CURRENT,
			BIT(IIO_CHAN_INFO_PROCESSED), rradc_post_process_dcin_curr,
			RR_ADC_DC_IN_I_LSB, RR_ADC_DC_IN_I_MSB, RR_ADC_DC_IN_I_STS, 2,
			RR_ADC_DC_IN_I_TRIGGER, RR_ADC_DC_IN_I_TRIGGER);
	chip->chans[RR_ADC_DCIN_V] =
		(struct rradc_channel) CHAN_INIT("dcin_v", IIO_VOLTAGE,
			BIT(IIO_CHAN_INFO_PROCESSED), rradc_post_process_volt,
			RR_ADC_DC_IN_V_LSB, RR_ADC_DC_IN_V_MSB, RR_ADC_DC_IN_V_STS, 2,
			RR_ADC_DC_IN_V_TRIGGER, RR_ADC_DC_IN_V_TRIGGER);
	chip->chans[RR_ADC_DIE_TEMP] =
		(struct rradc_channel) CHAN_INIT("die_temp", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_die_temp,
			RR_ADC_PMI_DIE_TEMP_LSB, RR_ADC_PMI_DIE_TEMP_MSB, RR_ADC_PMI_DIE_TEMP_STS,
			2, RR_ADC_PMI_DIE_TEMP_TRIGGER,
			RR_ADC_PMI_DIE_TEMP_TRIGGER);
	chip->chans[RR_ADC_CHG_TEMP] =
		(struct rradc_channel) CHAN_INIT("chg_temp", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_chg_temp,
			RR_ADC_CHARGER_TEMP_LSB, RR_ADC_CHARGER_TEMP_MSB,
			RR_ADC_CHARGER_TEMP_STS, 2, RR_ADC_CHARGER_TEMP_TRIGGER,
			RR_ADC_CHARGER_TEMP_TRIGGER);
	chip->chans[RR_ADC_GPIO] =
		(struct rradc_channel) CHAN_INIT("gpio", IIO_VOLTAGE,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_gpio,
			RR_ADC_GPIO_LSB, RR_ADC_GPIO_MSB, RR_ADC_GPIO_STS, 2,
			RR_ADC_GPIO_TRIGGER, RR_ADC_GPIO_TRIGGER);
	chip->chans[RR_ADC_CHG_HOT_TEMP] =
		(struct rradc_channel) CHAN_INIT("chg_temp_hot", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_chg_temp_hot,
			RR_ADC_CHARGER_HOT, RR_ADC_CHARGER_HOT, RR_ADC_CHARGER_TEMP_STS, 1,
			RR_ADC_CHARGER_TEMP_TRIGGER, RR_ADC_CHARGER_TEMP_TRIGGER);
	chip->chans[RR_ADC_CHG_TOO_HOT_TEMP] =
		(struct rradc_channel) CHAN_INIT("chg_temp_too_hot", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_chg_temp_hot,
			RR_ADC_CHARGER_TOO_HOT, RR_ADC_CHARGER_TOO_HOT, RR_ADC_CHARGER_TEMP_STS, 1,
			RR_ADC_CHARGER_TEMP_TRIGGER, RR_ADC_CHARGER_TEMP_TRIGGER);
	chip->chans[RR_ADC_SKIN_HOT_TEMP] =
		(struct rradc_channel) CHAN_INIT("skin_temp_hot", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_skin_temp_hot,
			RR_ADC_SKIN_HOT, RR_ADC_SKIN_HOT, RR_ADC_AUX_THERM_STS, 1,
			RR_ADC_AUX_THERM_TRIGGER, RR_ADC_AUX_THERM_TRIGGER);
	chip->chans[RR_ADC_SKIN_TOO_HOT_TEMP] =
		(struct rradc_channel) CHAN_INIT("skin_temp_too_hot", IIO_TEMP,
			BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_PROCESSED),
			rradc_post_process_skin_temp_hot,
			RR_ADC_SKIN_TOO_HOT, RR_ADC_SKIN_TOO_HOT, RR_ADC_AUX_THERM_STS, 1,
			RR_ADC_AUX_THERM_TRIGGER, RR_ADC_AUX_THERM_TRIGGER);

	for (i = 0; i < RR_ADC_MAX; i++) {
		chip->iio_chans[i].datasheet_name = chip->chans[i].datasheet_name;
		chip->iio_chans[i].extend_name = chip->chans[i].datasheet_name;
		chip->iio_chans[i].info_mask_separate = chip->chans[i].info_mask;
		chip->iio_chans[i].type = chip->chans[i].type;
		chip->iio_chans[i].address = i;
		chip->iio_chans[i].channel = i;
	}

	return 0;
}

static int rradc_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct rradc_chip *chip;
	int ret = 0, i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap) {
		dev_err(dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	chip->dev = dev;
	mutex_init(&chip->lock);

	/* Get the peripheral address */
	ret = of_property_read_u32(node, "reg", &chip->base);
	if (ret < 0) {
		dev_err(chip->dev,
			"Couldn't find reg in node = %s ret = %d\n",
			node->name, ret);
		return ret;
	}

	chip->batt_id_delay = -EINVAL;
	ret = of_property_read_u32(node, "qcom,batt-id-delay-ms",
			&chip->batt_id_delay);
	if (!ret) {
		for (i = 0; i < RRADC_BATT_ID_DELAY_MAX; i++) {
			if (chip->batt_id_delay == batt_id_delays[i])
				break;
		}
		if (i == RRADC_BATT_ID_DELAY_MAX)
			chip->batt_id_delay = -EINVAL;
	}

	/* Get the PMIC revision ID, we need to handle some varying coefficients */
	chip->pmic = (struct qcom_spmi_pmic *)
		spmi_device_get_drvdata(to_spmi_device(pdev->dev.parent));
	qcom_pmic_print_info(chip->dev, chip->pmic);

	ret = rradc_init_channels(chip);
	if (ret < 0) {
		dev_err(dev, "Couldn't initialize channels\n");
		return ret;
	}

	indio_dev->dev.parent = dev;
	indio_dev->dev.of_node = node;
	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &rradc_info;
	indio_dev->channels = chip->iio_chans;
	indio_dev->num_channels = chip->nchannels;

	ret = devm_iio_device_register(dev, indio_dev);
	return ret;
}

static const struct of_device_id rradc_match_table[] = {
	{ .compatible = "qcom,pm660-rradc" },
	{ .compatible = "qcom,pmi8998-rradc" },
	{ }
};
MODULE_DEVICE_TABLE(of, rradc_match_table);

static struct platform_driver rradc_driver = {
	.driver		= {
		.name		= "qcom-rradc",
		.of_match_table	= rradc_match_table,
	},
	.probe = rradc_probe,
};
module_platform_driver(rradc_driver);

MODULE_DESCRIPTION("QCOM SPMI PMIC RR ADC driver");
MODULE_AUTHOR("Caleb Connolly <caleb.connolly@linaro.org>");
MODULE_LICENSE("GPL v2");
