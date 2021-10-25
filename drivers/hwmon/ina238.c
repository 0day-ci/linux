// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Texas Instruments INA238 power monitor chip
 * Datasheet: https://www.ti.com/product/ina238
 *
 * Copyright (C) 2021 Nathan Rossi <nathan.rossi@digi.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/util_macros.h>
#include <linux/regmap.h>

#include <linux/platform_data/ina2xx.h>

/* INA238 register definitions */
#define INA238_CONFIG			0x0
#define INA238_ADC_CONFIG		0x1
#define INA238_SHUNT_CALIBRATION	0x2
#define INA238_SHUNT_VOLTAGE		0x4
#define INA238_BUS_VOLTAGE		0x5
#define INA238_DIE_TEMP			0x6
#define INA238_CURRENT			0x7
#define INA238_POWER			0x8
#define INA238_DIAG_ALERT		0xb
#define INA238_SHUNT_OVER_VOLTAGE	0xc
#define INA238_SHUNT_UNDER_VOLTAGE	0xd
#define INA238_BUS_OVER_VOLTAGE		0xe
#define INA238_BUS_UNDER_VOLTAGE	0xf
#define INA238_TEMP_LIMIT		0x10
#define INA238_POWER_LIMIT		0x11
#define INA238_DEVICE_ID		0x3f

#define INA238_REGISTERS		0x11

#define INA238_RSHUNT_DEFAULT		10000 /* uOhm */

/* Default configuration of device on reset. */
#define INA238_CONFIG_DEFAULT		0
/* 16 sample averaging, 1052us conversion time, continuous mode */
#define INA238_ADC_CONFIG_DEFAULT	0xfb6a
/*
 * This driver uses a fixed calibration value in order to scale current/power
 * based on a fixed shunt resistor value. This allows for conversion within the
 * device to avoid integer limits whilst current/power accuracy is scaled
 * relative to the shunt resistor value within the driver. This is similar to
 * how the ina2xx driver handles current/power scaling.
 *
 * The end result of this is that increasing shunt values (from a fixed 20 mOhm
 * shunt) increase the effective current/power accuracy whilst limiting the
 * range and decreasing shunt values decrease the effective accuracy but
 * increase the range.
 *
 * The value of the Current register is calculated given the following:
 *   Current (A) = (shunt voltage register * 5) * calibration / 81920
 *
 * The maximum shunt voltage is 163.835 mV (0x7fff, ADC_RANGE = 0). With the
 * maximum current value of 0x7fff and a fixed shunt value results in a
 * calibration value of 16384 (0x4000).
 *
 *   0x7fff = (0x7fff * 5) * calibration / 81920
 *   calibration = 0x4000
 *
 * Equivalent calibration is applied for the Power register (maximum value for
 * bus voltage is 102396.875 mV, 0x7fff), where the maximum power that can
 * occur is ~16776192 uW (register value 0x147a8):
 *
 * This scaling means the resulting values for Current and Power registers need
 * to be scaled by the difference between the fixed shunt resistor and the
 * actual shunt resistor:
 *
 *  shunt = 0x4000 / (819.2 * 10^6) / 0.001 = 20000 uOhms (with 1mA/lsb)
 *
 *  Current (mA) = register value * 20000 / rshunt
 *  Power (W) = 0.2 * register value * 20000 / rshunt
 */
#define INA238_CALIBRATION_VALUE	16384
#define INA238_FIXED_SHUNT		20000

#define INA238_SHUNT_VOLTAGE_LSB	5 /* 5 uV/lsb */
#define INA238_BUS_VOLTAGE_LSB		3125 /* 3.125 mV/lsb */
#define INA238_DIE_TEMP_LSB		125 /* 125 mC/lsb */

static struct regmap_config ina238_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
};

struct ina238_data {
	struct i2c_client *client;
	struct mutex config_lock;
	struct regmap *regmap;
	long rshunt;
};

static ssize_t ina238_value_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina238_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	long long val = 0;
	u8 regdata[3];
	int err;

	if (attr->index == INA238_POWER) {
		/* Handle reading the POWER register as 24-bit */
		err = i2c_smbus_read_i2c_block_data(data->client, attr->index, 3,
						    regdata);
		if (err != 3)
			return err;
		regval = (regdata[0] << 16) | (regdata[1] << 8) | regdata[2];
	} else {
		err = regmap_read(data->regmap, attr->index, &regval);
		if (err < 0)
			return err;
	}

	switch (attr->index) {
	case INA238_SHUNT_VOLTAGE:
		/* Signed register, result in mV */
		val = div_s64((s16)regval * INA238_SHUNT_VOLTAGE_LSB,
					1000);
		break;
	case INA238_BUS_VOLTAGE:
		/* Result in mV */
		val = div_s64((s16)regval * INA238_BUS_VOLTAGE_LSB, 1000);
		break;
	case INA238_CURRENT:
		/* Signed register, fixed 1mA current lsb. result in mA */
		val = div_s64((s16)regval * INA238_FIXED_SHUNT, data->rshunt);
		break;
	case INA238_POWER:
		/* Fixed 1mA lsb, scaled by 1000000 to have result in uW */
		val = div_u64(regval * 1000LL * INA238_FIXED_SHUNT, 5 * data->rshunt);
		break;
	case INA238_DIE_TEMP:
		/* Bits 15-4 of register, result in mC */
		val = ((s16)regval >> 4) * INA238_DIE_TEMP_LSB;
		break;
	case INA238_SHUNT_CALIBRATION:
		val = regval;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return snprintf(buf, PAGE_SIZE, "%lld\n", val);
}

static int ina238_set_shunt(struct device *dev, struct ina238_data *data,
			    long val)
{
	if (val == 0)
		return -EINVAL;

	mutex_lock(&data->config_lock);
	data->rshunt = val;
	mutex_unlock(&data->config_lock);

	return 0;
}

static ssize_t ina238_shunt_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct ina238_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%li\n", data->rshunt);
}

static ssize_t ina238_shunt_store(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int status;

	status = kstrtoul(buf, 10, &val);
	if (status < 0)
		return status;

	status = ina238_set_shunt(dev, data, val);
	if (status < 0)
		return status;
	return count;
}

static ssize_t ina238_alert_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina238_data *data = dev_get_drvdata(dev);
	long long val = 0;
	int regval;
	int ret;

	ret = regmap_read(data->regmap, attr->index, &regval);
	if (ret)
		return ret;

	switch (attr->index) {
	case INA238_SHUNT_OVER_VOLTAGE:
	case INA238_SHUNT_UNDER_VOLTAGE:
		val = div_s64((s16)regval * INA238_SHUNT_VOLTAGE_LSB, 1000);
		break;
	case INA238_BUS_OVER_VOLTAGE:
	case INA238_BUS_UNDER_VOLTAGE:
		val = div_u64(regval * INA238_BUS_VOLTAGE_LSB, 1000);
		break;
	case INA238_POWER_LIMIT:
		/*
		 * Truncated 24-bit compare register, lower 8-bits are
		 * truncated. Same conversion to/from uW as POWER register.
		 */
		val = div_u64((regval << 8) * 1000ULL * INA238_FIXED_SHUNT,
			      5 * data->rshunt);
		break;
	case INA238_TEMP_LIMIT:
		/* Signed, bits 15-4 of register */
		val = ((s16)regval >> 4) * INA238_DIE_TEMP_LSB;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return snprintf(buf, PAGE_SIZE, "%lld\n", val);
}

static ssize_t ina238_alert_store(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina238_data *data = dev_get_drvdata(dev);
	long long val;
	int regval;
	int ret;

	ret = kstrtoll(buf, 10, &val);
	if (ret < 0)
		return ret;

	/* convert decimal to register value */
	switch (attr->index) {
	case INA238_SHUNT_OVER_VOLTAGE:
	case INA238_SHUNT_UNDER_VOLTAGE:
		/* signed */
		regval = div_s64((val * 1000), INA238_SHUNT_VOLTAGE_LSB);
		if (regval > S16_MAX || regval < S16_MIN) {
			ret = -EINVAL;
			goto abort;
		}
		break;
	case INA238_BUS_OVER_VOLTAGE:
	case INA238_BUS_UNDER_VOLTAGE:
		regval = div_u64((val * 1000), INA238_BUS_VOLTAGE_LSB);
		if (regval > U16_MAX || regval < 0) {
			ret = -EINVAL;
			goto abort;
		}
		break;
	case INA238_POWER_LIMIT:
		/*
		 * Compared against the 24-bit power register, lower 8-bits are
		 * truncated. Same conversion to/from uW as POWER register.
		 */
		regval = div_u64(val * 5 * data->rshunt,
				 1000 * INA238_FIXED_SHUNT) >> 8;
		if (regval > U16_MAX || regval < 0) {
			ret = -EINVAL;
			goto abort;
		}
		break;
	case INA238_TEMP_LIMIT:
		/* Bits 15-4 of register */
		regval = (div_s64(val, INA238_DIE_TEMP_LSB) << 4);
		if (regval > S16_MAX || regval < S16_MIN) {
			ret = -EINVAL;
			goto abort;
		}
		regval = regval & 0xfff0;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	mutex_lock(&data->config_lock);

	ret = regmap_write(data->regmap, attr->index, regval);
	if (ret < 0)
		goto abort;

	ret = count;
abort:
	mutex_unlock(&data->config_lock);
	return ret;
}

/* shunt voltage */
static SENSOR_DEVICE_ATTR_RO(in0_input, ina238_value, INA238_SHUNT_VOLTAGE);
/* shunt voltage over/under alert */
static SENSOR_DEVICE_ATTR_RW(in0_crit, ina238_alert, INA238_SHUNT_OVER_VOLTAGE);
static SENSOR_DEVICE_ATTR_RW(in0_lcrit, ina238_alert,
			     INA238_SHUNT_UNDER_VOLTAGE);

/* bus voltage */
static SENSOR_DEVICE_ATTR_RO(in1_input, ina238_value, INA238_BUS_VOLTAGE);
/* bus voltage over/under alert */
static SENSOR_DEVICE_ATTR_RW(in1_crit, ina238_alert, INA238_BUS_OVER_VOLTAGE);
static SENSOR_DEVICE_ATTR_RW(in1_lcrit, ina238_alert, INA238_BUS_UNDER_VOLTAGE);

/* calculated current */
static SENSOR_DEVICE_ATTR_RO(curr1_input, ina238_value, INA238_CURRENT);

/* calculated power */
static SENSOR_DEVICE_ATTR_RO(power1_input, ina238_value, INA238_POWER);
static SENSOR_DEVICE_ATTR_RW(power1_crit, ina238_alert, INA238_POWER_LIMIT);

/* die temperature */
static SENSOR_DEVICE_ATTR_RO(temp1_input, ina238_value, INA238_DIE_TEMP);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, ina238_alert, INA238_TEMP_LIMIT);

/* shunt resistance */
static SENSOR_DEVICE_ATTR_RW(shunt_resistor, ina238_shunt,
			     INA238_SHUNT_CALIBRATION);

static struct attribute *ina238_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_crit.dev_attr.attr,
	&sensor_dev_attr_in0_lcrit.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_crit.dev_attr.attr,
	&sensor_dev_attr_in1_lcrit.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_power1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_shunt_resistor.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ina238);

static int ina238_probe(struct i2c_client *client)
{
	struct ina2xx_platform_data *pdata = dev_get_platdata(&client->dev);
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct ina238_data *data;
	u32 val;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	/* set the device type */
	mutex_init(&data->config_lock);

	ina238_regmap_config.max_register = INA238_REGISTERS;
	data->regmap = devm_regmap_init_i2c(client, &ina238_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	/* load shunt value */
	val = INA238_RSHUNT_DEFAULT;
	if (device_property_read_u32(dev, "shunt-resistor", &val) < 0 && pdata)
		val = pdata->shunt_uohms;
	ret = ina238_set_shunt(dev, data, val);
	if (ret) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return ret;
	}

	/* Setup CONFIG register */
	ret = regmap_write(data->regmap, INA238_CONFIG, INA238_CONFIG_DEFAULT);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	/* Setup ADC_CONFIG register */
	ret = regmap_write(data->regmap, INA238_ADC_CONFIG,
			   INA238_ADC_CONFIG_DEFAULT);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	/* Setup SHUNT_CALIBRATION register with fixed value */
	ret = regmap_write(data->regmap, INA238_SHUNT_CALIBRATION,
			   INA238_CALIBRATION_VALUE);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, ina238_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "power monitor %s (Rshunt = %li uOhm)\n",
		 client->name, data->rshunt);

	return 0;
}

static const struct i2c_device_id ina238_id[] = {
	{ "ina238", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina238_id);

static const struct of_device_id __maybe_unused ina238_of_match[] = {
	{ .compatible = "ti,ina238" },
	{ },
};
MODULE_DEVICE_TABLE(of, ina238_of_match);

static struct i2c_driver ina238_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "ina238",
		.of_match_table = of_match_ptr(ina238_of_match),
	},
	.probe_new	= ina238_probe,
	.id_table	= ina238_id,
};

module_i2c_driver(ina238_driver);

MODULE_AUTHOR("Nathan Rossi <nathan.rossi@digi.com>");
MODULE_DESCRIPTION("ina238 driver");
MODULE_LICENSE("GPL");
