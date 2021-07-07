// SPDX-License-Identifier: GPL-2.0-or-later
/* da9063-hwmon.c - Hardware monitor support for DA9063
 * Copyright (C) 2014 Dialog Semiconductor Ltd.
 * Copyright (C) 2021 Vincent Pelletier <plr.vincent@gmail.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#define DA9063_ADC_RES	(1 << (DA9063_ADC_RES_L_BITS + DA9063_ADC_RES_M_BITS))
#define DA9063_ADC_MAX	(DA9063_ADC_RES - 1)
#define DA9063_2V5	2500
#define DA9063_5V0	5000
#define DA9063_5V5	5500
#define DA9063_TJUNC_M	-398
#define DA9063_TJUNC_O	330000
#define DA9063_VBBAT_M	2048

enum da9063_adc {
	DA9063_CHAN_VSYS = DA9063_ADC_MUX_VSYS,
	DA9063_CHAN_ADCIN1 = DA9063_ADC_MUX_ADCIN1,
	DA9063_CHAN_ADCIN2 = DA9063_ADC_MUX_ADCIN2,
	DA9063_CHAN_ADCIN3 = DA9063_ADC_MUX_ADCIN3,
	DA9063_CHAN_TJUNC = DA9063_ADC_MUX_T_SENSE,
	DA9063_CHAN_VBBAT = DA9063_ADC_MUX_VBBAT,
	DA9063_CHAN_LDO_G1 = DA9063_ADC_MUX_LDO_G1,
	DA9063_CHAN_LDO_G2 = DA9063_ADC_MUX_LDO_G2,
	DA9063_CHAN_LDO_G3 = DA9063_ADC_MUX_LDO_G3
};

struct da9063_hwmon {
	struct da9063 *da9063;
	struct mutex hwmon_mutex;
	struct completion adc_ready;
	signed char tjunc_offset;
};

static int da9063_adc_manual_read(struct da9063_hwmon *hwmon, int channel)
{
	int ret;
	unsigned char val;
	unsigned char data[2];
	int adc_man;

	mutex_lock(&hwmon->hwmon_mutex);

	val = (channel & DA9063_ADC_MUX_MASK) | DA9063_ADC_MAN;
	ret = regmap_update_bits(hwmon->da9063->regmap, DA9063_REG_ADC_MAN,
				 DA9063_ADC_MUX_MASK | DA9063_ADC_MAN, val);
	if (ret < 0)
		goto err_mread;

	ret = wait_for_completion_timeout(&hwmon->adc_ready,
					  msecs_to_jiffies(100));
	reinit_completion(&hwmon->adc_ready);
	if (ret == 0)
		dev_dbg(hwmon->da9063->dev,
			"Timeout while waiting for ADC completion IRQ\n");

	ret = regmap_read(hwmon->da9063->regmap, DA9063_REG_ADC_MAN, &adc_man);
	if (ret < 0)
		goto err_mread;

	/* data value is not ready */
	if (adc_man & DA9063_ADC_MAN) {
		ret = -ETIMEDOUT;
		goto err_mread;
	}

	ret = regmap_bulk_read(hwmon->da9063->regmap,
			       DA9063_REG_ADC_RES_L, data, 2);
	if (ret < 0)
		goto err_mread;

	ret = (data[0] & DA9063_ADC_RES_L_MASK) >> DA9063_ADC_RES_L_SHIFT;
	ret |= data[1] << DA9063_ADC_RES_L_BITS;
err_mread:
	mutex_unlock(&hwmon->hwmon_mutex);
	return ret;
}

static irqreturn_t da9063_hwmon_irq_handler(int irq, void *irq_data)
{
	struct da9063_hwmon *hwmon = irq_data;

	complete(&hwmon->adc_ready);
	return IRQ_HANDLED;
}

static umode_t da9063_is_visible(const void *drvdata, enum
				 hwmon_sensor_types type, u32 attr, int channel)
{
	return 0444;
}

static const enum da9063_adc da9063_in_index[] = {
	DA9063_CHAN_VSYS, DA9063_CHAN_VBBAT
};

static int da9063_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct da9063_hwmon *hwmon = dev_get_drvdata(dev);
	enum da9063_adc adc_channel;
	int tmp;

	switch (type) {
	case hwmon_in:
		if (attr != hwmon_in_input)
			return -EOPNOTSUPP;
		adc_channel = da9063_in_index[channel];
		break;
	case hwmon_temp:
		if (attr != hwmon_temp_input)
			return -EOPNOTSUPP;
		adc_channel = DA9063_CHAN_TJUNC;
		break;
	default:
		return -EOPNOTSUPP;
	}

	tmp = da9063_adc_manual_read(hwmon, adc_channel);
	if (tmp < 0)
		return tmp;

	switch (adc_channel) {
	case DA9063_CHAN_VSYS:
		*val = ((DA9063_5V5 - DA9063_2V5) * tmp) / DA9063_ADC_MAX +
			DA9063_2V5;
		break;
	case DA9063_CHAN_TJUNC:
		tmp -= hwmon->tjunc_offset;
		*val = DA9063_TJUNC_M * tmp + DA9063_TJUNC_O;
		break;
	case DA9063_CHAN_VBBAT:
		*val = (DA9063_5V0 * tmp) / DA9063_ADC_MAX;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const char * const da9063_in_name[] = {
	"VSYS", "VBBAT"
};

static int da9063_read_string(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_in:
		if (attr != hwmon_in_label)
			return -EOPNOTSUPP;
		*str = da9063_in_name[channel];
		break;
	case hwmon_temp:
		if (attr != hwmon_temp_label)
			return -EOPNOTSUPP;
		*str = "TJUNC";
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops da9063_ops = {
	.is_visible = da9063_is_visible,
	.read = da9063_read,
	.read_string = da9063_read_string,
};

static const struct hwmon_channel_info *da9063_channel_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_chip_info da9063_chip_info = {
	.ops = &da9063_ops,
	.info = da9063_channel_info,
};

static int da9063_hwmon_probe(struct platform_device *pdev)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct da9063_hwmon *hwmon;
	struct device *hwmon_dev;
	unsigned int tmp;
	int irq;
	int ret;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(struct da9063_hwmon),
			     GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	mutex_init(&hwmon->hwmon_mutex);
	init_completion(&hwmon->adc_ready);
	hwmon->da9063 = da9063;

	irq = platform_get_irq_byname(pdev, DA9063_DRVNAME_HWMON);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					da9063_hwmon_irq_handler,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"HWMON", hwmon);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ.\n");
		return ret;
	}

	ret = regmap_read(da9063->regmap, DA9063_REG_T_OFFSET, &tmp);
	if (ret < 0) {
		tmp = 0;
		dev_warn(&pdev->dev,
			 "Temperature trimming value cannot be read (defaulting to 0)\n");
	}
	hwmon->tjunc_offset = (signed char) tmp;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "da9063",
							 hwmon,
							 &da9063_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver da9063_hwmon_driver = {
	.probe = da9063_hwmon_probe,
	.driver = {
		.name = DA9063_DRVNAME_HWMON,
	},
};
module_platform_driver(da9063_hwmon_driver);

MODULE_DESCRIPTION("Hardware monitor support device driver for Dialog DA9063");
MODULE_AUTHOR("Vincent Pelletier <plr.vincent@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DA9063_DRVNAME_HWMON);
