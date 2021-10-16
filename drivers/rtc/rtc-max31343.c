// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtc class driver for the Maxim MAX31343 chip
 *
 * Copyright (C) 2021 Alexey Firago <alexey_firago@mentor.com>
 *
 * Datasheet - https://datasheets.maximintegrated.com/en/ds/MAX31343.pdf
 *
 */

#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

/* REGISTERS */
/* Control section */
#define MAX31343_REG_STATUS	(0x00)
#define  STATUS_A1F	BIT(0) /* Alarm1 interrupt flag */
#define  STATUS_A2F	BIT(1) /* Alarm2 interrupt flag */
#define  STATUS_TIF	BIT(2) /* Timer interrupt flag */
#define  STATUS_TSF	BIT(3) /* Temp sense data flag */
#define  STATUS_PFAIL	BIT(5) /* Power-fail flag */
#define  STATUS_OSF	BIT(6) /* Oscillator stop flag */
#define  STATUS_PSDECT	BIT(7) /* Power source indicator */

#define MAX31343_REG_INT_EN	(0x01)
#define  INT_EN_A1IE	BIT(0) /* Alarm1 interrupt enable */
#define  INT_EN_A2IE	BIT(1) /* Alarm2 interrupt enable */
#define  INT_EN_TIE	BIT(2) /* Timer interrupt enable */
#define  INT_EN_TSIE	BIT(3) /* Temp sense ready enable */
#define  INT_EN_PFAILE	BIT(5) /* Power-fail interrupt enable */
#define  INT_EN_DOSF	BIT(6) /* Disable oscillator flag */

#define MAX31343_REG_RTC_RESET	(0x02)
#define	 RTC_RESET_SWRST	BIT(0) /* Software reset bit */

#define MAX31343_REG_RTC_CFG1	(0x03)
#define  RTC_CFG1_ENOSC		BIT(1) /* Enable for oscillator */
#define  RTC_CFG1_I2C_TIMEOUT	BIT(3) /* I2C timeout enable */
#define  RTC_CFG1_DATA_RET	BIT(4) /* Data retention bit */

#define MAX31343_REG_RTC_CFG2	(0x04)
#define  RTC_CFG2_SQW_HZ	GENMASK(2, 0) /* SQW clock frequency */
#define  RTC_CFG2_CLKO_HZ	GENMASK(6, 3) /* Clock frequency on CLKO */
#define  RTC_CFG2_ENCLKO	       BIT(7) /* CLKO enable */

#define MAX31343_REG_TIMER_CFG	(0x05)
#define  TIMER_CFG_TFS		GENMASK(1, 0) /* Timer frequency */
#define  TIMER_CFG_TRPT		BIT(2) /* Timer repeat mode */
#define  TIMER_CFG_TPAUSE	BIT(3) /* Timer Pause */
#define  TIMER_CFG_TE		BIT(4) /* Timer enable */

/* RTC section */
#define MAX31343_REG_SEC	(0x06)
#define  SEC10_MASK	GENMASK(6, 4) /* RTC seconds in multiples of 10 */
#define  SEC_MASK	GENMASK(3, 0) /* RTC seconds value */

#define MAX31343_REG_MIN	(0x07)
#define  MIN10_MASK	GENMASK(6, 4) /* RTC minutes in multiples of 10 */
#define  MIN_MASK	GENMASK(3, 0) /* RTC minutes value */

#define MAX31343_REG_HRS	(0x08)
#define  HRS10_MASK	GENMASK(5, 4) /* RTC hours in multiples of 10 */
#define  HRS_MASK	GENMASK(3, 0) /* RTC hours value */

#define MAX31343_REG_DAY	(0x09)
#define  WDAY_MASK	GENMASK(2, 0) /* RTC days */

#define MAX31343_REG_DATE	(0x0a)
#define  DATE10_MASK	GENMASK(5, 4) /* RTC date in multiples of 10 */
#define  DATE_MASK	GENMASK(3, 0) /* RTC date */

#define MAX31343_REG_MONTH	(0x0b)
#define  CENTURY		BIT(7) /* Century bit */
#define  MON10_MASK		BIT(4) /* RTC months in multiples of 10 */
#define  MON_MASK		GENMASK(3, 0) /* RTC months */

#define MAX31343_REG_YEAR	(0x0c)
#define  YEAR_10	GENMASK(7, 4) /* RTC years in multiples of 10 */
#define  YEAR		GENMASK(3, 0) /* RTC years */

/* Alarm 1 section */
#define MAX31343_REG_ALM1_SEC		(0x0d)
#define  A1M1		       BIT(7) /* Alarm1 mask bit for seconds */
#define  ALM1SEC_10	GENMASK(6, 4) /* Alarm1 seconds in multiples of 10 */
#define  ALM1SEC	GENMASK(3, 0) /* Alarm1 seconds */

#define MAX31343_REG_ALM1_MIN		(0x0e)
#define  A1M2		       BIT(7) /* Alarm1 mask bit for minutes */
#define  ALM1MIN_10	GENMASK(6, 4) /* Alarm1 minutes in multiples of 10 */
#define  ALM1MIN	GENMASK(3, 0) /* Alarm1 minutes */

#define MAX31343_REG_ALM1_HRS		(0x0f)
#define  A1M3		       BIT(7) /* Alarm1 mask bit for hours */
#define  ALM1HRS_10	       BIT(4) /* Alarm1 hours in multiples of 10 */
#define  ALM1HRS	GENMASK(3, 0) /* Alarm1 hours */

#define MAX31343_REG_ALM1DAY_DATE	(0x10)
#define  A1M4		       BIT(7) /* Alarm1 mask bit for day/date */
#define  ALM1DY_DT	       BIT(6) /* Alarm1 date/day match switch */
#define  ALM1DATE_10	GENMASK(5, 4) /* Alarm1 date in multiples of 10 */
#define  ALM1DAYDATE	GENMASK(3, 0) /* Alarm1 day/date */

#define MAX31343_REG_ALM1_MON		(0x11)
#define  A1M5		       BIT(7) /* Alarm1 mask bit for month */
#define  A1M6		       BIT(6) /* Alarm1 mask bit for year */
#define  ALM1MON_10	       BIT(4) /* Alarm1 months in multiples of 10 */
#define  ALM1MON	GENMASK(3, 0) /* Alarm1 months */

#define MAX31343_REG_ALM1_YEAR		(0x12)
#define  ALM1YEAR_10	GENMASK(7, 4) /* Alarm1 year in multiples of 10 */
#define  ALM1YEAR	GENMASK(3, 0) /* Alarm1 years */

/* Alarm 2 section */
#define MAX31343_REG_ALM2_MIN		(0x13)
#define  A2M2		       BIT(7) /* Alarm2 mask bit for minutes */
#define  ALM2MIN_10	GENMASK(6, 4) /* Alarm2 minutes in multiples of 10 */
#define  ALM2MIN	GENMASK(3, 0) /* Alarm2 minutes */

#define MAX31343_REG_ALM2_HRS		(0x14)
#define  A2M3		       BIT(7) /* Alarm2 mask bit for hours */
#define  ALM2HRS_10	       BIT(4) /* Alarm2 hours in multiples of 10 */
#define  ALM2HRS	GENMASK(3, 0) /* Alarm2 hours */

#define MAX31343_REG_ALM2_DAY_DATE	(0x15)
#define  A2M4		       BIT(7) /* Alarm2 mask bit for day/date */
#define  ALM2DY_DT	       BIT(6) /* Alarm2 date/day match switch */
#define  ALM2DATE_10	GENMASK(5, 4) /* Alarm2 date in multiples of 10 */
#define  ALM2DAYDATE	GENMASK(3, 0) /* Alarm2 day/date */

/* Countdown Timer section */
#define MAX31343_REG_TIMER_COUNT	(0x16)
#define MAX31343_REG_TIMER_INIT		(0x17)

/* Power Management section */
#define MAX31343_REG_PWR_MGMT		(0x18)
#define  PFVT		GENMASK(5, 4) /* Power fail threshold voltage */
#define  D_VBACK_SEL	       BIT(3) /* Backup battery select */
#define  D_MAN_SEL	       BIT(2) /* Power supply selection method */

#define MAX31343_REG_TRICKLE_REG	(0x19)
#define  TCHE		GENMASK(7, 4) /* Trickle charger enable */
#define  D_TRICKLE	GENMASK(3, 0) /* Charging path for trickle charger */

/* Temperature Sensor section */
#define MAX31343_REG_TEMP_MSB	(0x1a)
#define MAX31343_REG_TEMP_LSB	(0x1b)

#define MAX31343_REG_TS_CONFIG	(0x1c)
#define  AUTOMODE	       BIT(7) /* Automatic temp. measurement */
#define  ONESHOTMODE	       BIT(6) /* One-shot temp. measurement */
#define  TTSINT		GENMASK(5, 3) /* Temp. measurement interval */

/* RAM section */
#define MAX31343_REG_RAM	(0x22)
#define MAX31343_RAM_SIZE	64

struct max31343_rtc_data {
	struct rtc_device *rtc;
	struct regmap *regmap;
};

static int max31343_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max31343_rtc_data *max31343 = dev_get_drvdata(dev);
	u8 date[7];
	int ret, status;

	ret = regmap_read(max31343->regmap, MAX31343_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	if (status & STATUS_PFAIL)
		return -EINVAL;

	ret = regmap_bulk_read(max31343->regmap, MAX31343_REG_SEC, date, sizeof(date));
	if (ret)
		return ret;

	tm->tm_sec  = bcd2bin(date[0] & (SEC10_MASK | SEC_MASK));
	tm->tm_min  = bcd2bin(date[1] & (MIN10_MASK | MIN_MASK));
	tm->tm_hour = bcd2bin(date[2] & (HRS10_MASK | HRS_MASK));
	tm->tm_wday = date[3] & WDAY_MASK;
	tm->tm_mday = bcd2bin(date[4] & (DATE10_MASK | DATE_MASK));
	tm->tm_mon  = bcd2bin(date[5] & (MON10_MASK | MON_MASK)) - 1;
	tm->tm_year = bcd2bin(date[6]) +
			     ((date[5] & CENTURY) ? 200 : 100);
	return 0;
}

static int max31343_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max31343_rtc_data *max31343 = dev_get_drvdata(dev);
	u8 date[7];
	int ret;

	dev_dbg(dev, "RTC set time %04d-%02d-%02d %02d/%02d/%02d\n",
		tm->tm_year + 1900, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	date[0] = bin2bcd(tm->tm_sec);
	date[1] = bin2bcd(tm->tm_min);
	date[2] = bin2bcd(tm->tm_hour);
	date[3] = tm->tm_wday;
	date[4] = bin2bcd(tm->tm_mday);
	date[5] = bin2bcd(tm->tm_mon + 1);

	if (tm->tm_year >= 200)
		date[5] |= CENTURY;
	date[6] = bin2bcd(tm->tm_year % 100);

	ret = regmap_bulk_write(max31343->regmap, MAX31343_REG_SEC, date,
				sizeof(date));
	return ret;
}

static int max31343_read_temp(struct device *dev, long *mC)
{
	struct max31343_rtc_data *max31343 = dev_get_drvdata(dev);
	u8 buf[2];
	int temp;
	int ret;

	/* Check for AUTOMODE/ONESHOTMODE first */
	ret = regmap_test_bits(max31343->regmap, MAX31343_REG_TS_CONFIG, AUTOMODE);
	if (ret < 0)
		return ret;

	/* If not in AUTOMODE, set ONESHOTMODE and wait till measurement is ready */
	if (ret == 0) {
		ret = regmap_update_bits(max31343->regmap, MAX31343_REG_TS_CONFIG,
					 ONESHOTMODE, ONESHOTMODE);
		if (ret < 0)
			return ret;
		do {
			ret = regmap_test_bits(max31343->regmap, MAX31343_REG_TS_CONFIG,
					       ONESHOTMODE);
			if (ret < 0)
				return ret;
		} while (ret != 0);
	}

	ret = regmap_bulk_read(max31343->regmap, MAX31343_REG_TEMP_MSB, buf,
			       sizeof(buf));
	if (ret)
		return ret;
	temp = sign_extend32(buf[0], 7) << 2;
	temp |= FIELD_GET(GENMASK(7, 6), buf[1]);
	*mC = (temp * 1000) / 4;
	return 0;
}

static umode_t max31343_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int max31343_read_temp_interval(struct device *dev, long *val)
{
	struct max31343_rtc_data *max31343 = dev_get_drvdata(dev);
	unsigned int reg;
	int ret;

	ret = regmap_read(max31343->regmap, MAX31343_REG_TS_CONFIG, &reg);
	if (ret)
		return ret;

	/*
	 * Automatic measurement mode disabled.
	 * Measurements performed upon request, just return 0 interval.
	 */
	if (!(reg & AUTOMODE)) {
		*val = 0;
		return 0;
	}

	/*
	 * Bits [5:3] are storing interval code (TTSINT).
	 * Supported codes are [0x0:0x7], which maps to [1sec:128sec].
	 * Value in seconds is 2^TTSINT
	 */
	*val = int_pow(2, ((reg & TTSINT) >> 3));
	return 0;
}

static int max31343_write_temp_interval(struct device *dev, long val)
{
	struct max31343_rtc_data *max31343 = dev_get_drvdata(dev);
	unsigned int reg;

	/* 0 interval means one-shot measurment mode, just clear AUTOMODE */
	if (val == 0)
		return regmap_update_bits(max31343->regmap, MAX31343_REG_TS_CONFIG,
					  AUTOMODE, 0);
	if (val > 128)
		val = 128;
	if (val < 1)
		val = 1;
	reg = ilog2(val) << 3;
	reg |= AUTOMODE;
	return regmap_write(max31343->regmap, MAX31343_REG_TS_CONFIG, reg);
}

static int max31343_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return max31343_write_temp_interval(dev, val);
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int max31343_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return max31343_read_temp_interval(dev, val);
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return max31343_read_temp(dev, val);
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static const struct hwmon_channel_info *max31343_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops max31343_hwmon_hwmon_ops = {
	.is_visible = max31343_hwmon_is_visible,
	.read = max31343_hwmon_read,
	.write = max31343_hwmon_write,
};

static const struct hwmon_chip_info max31343_hwmon_chip_info = {
	.ops = &max31343_hwmon_hwmon_ops,
	.info = max31343_hwmon_info,
};

static void max31343_hwmon_register(struct device *dev)
{
	struct max31343_data *max31343 = dev_get_drvdata(dev);
	struct device *hwmon_dev;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "max31343", max31343,
							 &max31343_hwmon_chip_info, NULL);
	if (IS_ERR(hwmon_dev)) {
		dev_warn(dev, "unable to register hwmon device %ld\n",
			 PTR_ERR(hwmon_dev));
	}
}

static int max31343_nvram_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	return regmap_bulk_write(priv, MAX31343_REG_RAM + offset, val, bytes);
}

static int max31343_nvram_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	return regmap_bulk_read(priv, MAX31343_REG_RAM + offset, val, bytes);
}

static const struct rtc_class_ops max31343_rtc_ops = {
	.read_time = max31343_rtc_read_time,
	.set_time = max31343_rtc_set_time,
};

static const struct regmap_config max31343_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX31343_REG_RAM + MAX31343_RAM_SIZE,
};

static int
max31343_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct max31343_rtc_data *max31343 = NULL;
	int ret, status;
	struct nvmem_config nvmem_cfg = {
		.name = "max31343_nvram",
		.word_size = 1,
		.stride = 1,
		.size = MAX31343_RAM_SIZE,
		.type = NVMEM_TYPE_BATTERY_BACKED,
		.reg_read = max31343_nvram_read,
		.reg_write = max31343_nvram_write,
	};

	max31343 = devm_kzalloc(&client->dev, sizeof(struct max31343_rtc_data),
				GFP_KERNEL);
	if (!max31343)
		return -ENOMEM;

	max31343->regmap = devm_regmap_init_i2c(client, &max31343_regmap_config);
	if (IS_ERR(max31343->regmap))
		return PTR_ERR(max31343->regmap);

	i2c_set_clientdata(client, max31343);

	ret = regmap_read(max31343->regmap, MAX31343_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	max31343->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(max31343->rtc))
		return PTR_ERR(max31343->rtc);

	max31343->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	max31343->rtc->range_max = RTC_TIMESTAMP_END_2199;
	max31343->rtc->ops = &max31343_rtc_ops;
	ret = devm_rtc_register_device(max31343->rtc);
	if (ret)
		return ret;

	nvmem_cfg.priv = max31343->regmap;
	devm_rtc_nvmem_register(max31343->rtc, &nvmem_cfg);
	max31343_hwmon_register(&client->dev);
	return 0;
}

static const struct i2c_device_id max31343_id[] = {
	{ "max31343", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max31343_id);

static const __maybe_unused struct of_device_id max31343_of_match[] = {
	{ .compatible = "maxim,max31343" },
	{ }
};
MODULE_DEVICE_TABLE(of, max31343_of_match);

static struct i2c_driver max31343_driver = {
	.driver = {
		   .name = "rtc-max31343",
		   .of_match_table = of_match_ptr(max31343_of_match),
		   },
	.probe = max31343_probe,
	.id_table = max31343_id,
};

module_i2c_driver(max31343_driver);

MODULE_DESCRIPTION("Maxim MAX31343 RTC driver");
MODULE_AUTHOR("Alexey Firago <alexey_firago@mentor.com>");
MODULE_LICENSE("GPL");
