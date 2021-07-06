/* da9063-hwmon.c - Hardware monitor support for DA9063
 * Copyright (C) 2014 Dialog Semiconductor Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/regmap.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mod_devicetable.h>

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

static int da9063_adc_convert(struct da9063_hwmon *hwmon, int channel,
			      int *value)
{
	int val = *value;
	int ret = 0;

	switch (channel) {
	case DA9063_CHAN_VSYS:
		val = ((DA9063_5V5 - DA9063_2V5) * val) / DA9063_ADC_MAX +
			DA9063_2V5;
		break;
	case DA9063_CHAN_TJUNC:
		val -= hwmon->tjunc_offset;
		val = DA9063_TJUNC_M * val + DA9063_TJUNC_O;
		break;
	case DA9063_CHAN_VBBAT:
		val = (DA9063_5V0 * val) / DA9063_ADC_MAX;
		break;
	default:
		ret = -EINVAL;
		goto err_convert;
	}

	*value = val;
err_convert:
	return ret;
}


static int da9063_adc_manual_read(struct da9063_hwmon *hwmon, int channel)
{
	int ret;
	unsigned char val;
	unsigned char data[2];
	int adc_man;

	mutex_lock(&hwmon->hwmon_mutex);

	init_completion(&hwmon->adc_ready);

	val = (channel & DA9063_ADC_MUX_MASK) | DA9063_ADC_MAN;
	ret = regmap_update_bits(hwmon->da9063->regmap, DA9063_REG_ADC_MAN,
				 DA9063_ADC_MUX_MASK | DA9063_ADC_MAN, val);
	if (ret < 0)
		goto err_mread;

	ret = wait_for_completion_timeout(&hwmon->adc_ready,
					  msecs_to_jiffies(1000));
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto err_mread;
	}

	ret = regmap_read(hwmon->da9063->regmap, DA9063_REG_ADC_MAN, &adc_man);
	if (ret < 0)
		goto err_mread;

	/* data value is not ready */
	if (adc_man & DA9063_ADC_MAN) {
		ret = -EINVAL;
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

static ssize_t da9063_adc_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct da9063_hwmon *hwmon = dev_get_drvdata(dev);
	int channel = to_sensor_dev_attr(devattr)->index;
	int val;
	int ret;

	switch (channel) {
	case DA9063_CHAN_VSYS:
	case DA9063_CHAN_TJUNC:
	case DA9063_CHAN_VBBAT:
		/* fallthrough for internal measures */
		val = da9063_adc_manual_read(hwmon, channel);
		if (val < 0) {
			dev_err(dev, "ADC read error %d\n", val);
			return val;
		}
		break;

	default:
		/* error case */
		ret = -EINVAL;
		goto err_read;
	}

	ret = da9063_adc_convert(hwmon, channel, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to convert ADC value %d\n", ret);
		goto err_read;
	}

	return sprintf(buf, "%d\n", val);
err_read:
	return ret;
}


static ssize_t da9063_label_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	int channel = to_sensor_dev_attr(devattr)->index;
	char *label;

	switch (channel) {
	case DA9063_CHAN_VSYS:
		label = "VSYS";
		break;
	case DA9063_CHAN_TJUNC:
		label = "TJUNC";
		break;
	case DA9063_CHAN_VBBAT:
		label = "VBBAT";
		break;
	default:
		label = "UNKNOWN";
	}

	return sprintf(buf, "%s\n", label);
}

static SENSOR_DEVICE_ATTR_RO(in0_input,
			  da9063_adc, DA9063_CHAN_VSYS);
static SENSOR_DEVICE_ATTR_RO(in0_label,
			  da9063_label, DA9063_CHAN_VSYS);

static SENSOR_DEVICE_ATTR_RO(in4_input,
			  da9063_adc, DA9063_CHAN_VBBAT);
static SENSOR_DEVICE_ATTR_RO(in4_label,
			  da9063_label, DA9063_CHAN_VBBAT);

static SENSOR_DEVICE_ATTR_RO(temp1_input,
			  da9063_adc, DA9063_CHAN_TJUNC);
static SENSOR_DEVICE_ATTR_RO(temp1_label,
			  da9063_label, DA9063_CHAN_TJUNC);

static struct attribute *da9063_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(da9063);

static int da9063_hwmon_probe(struct platform_device *pdev)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct da9063_hwmon *hwmon;
	struct device *hwmon_dev;
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

	platform_set_drvdata(pdev, hwmon);

	/* set trim temperature offset to value read at startup */
	hwmon->tjunc_offset = (signed char)hwmon->da9063->t_offset;

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev,
							   "da9063",
							   hwmon, da9063_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id da9063_dt_ids[] = {
	{ .compatible = DA9063_DRVNAME_HWMON, },
	{ }
};
MODULE_DEVICE_TABLE(of, da9063_dt_ids);

static struct platform_driver da9063_hwmon_driver = {
	.probe = da9063_hwmon_probe,
	.driver = {
		.name = DA9063_DRVNAME_HWMON,
		.of_match_table = da9063_dt_ids,
	},
};
module_platform_driver(da9063_hwmon_driver);

MODULE_DESCRIPTION("Hardware monitor support device driver for Dialog DA9063");
MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DA9063_DRVNAME_HWMON);
