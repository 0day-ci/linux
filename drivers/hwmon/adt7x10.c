// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * adt7x10.c - Part of lm_sensors, Linux kernel modules for hardware
 *	 monitoring
 * This driver handles the ADT7410 and compatible digital temperature sensors.
 * Hartmut Knaack <knaack.h@gmx.de> 2012-07-22
 * based on lm75.c by Frodo Looijaard <frodol@dds.nl>
 * and adt7410.c from iio-staging by Sonic Zhang <sonic.zhang@analog.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "adt7x10.h"

/*
 * ADT7X10 status
 */
#define ADT7X10_STAT_T_LOW		(1 << 4)
#define ADT7X10_STAT_T_HIGH		(1 << 5)
#define ADT7X10_STAT_T_CRIT		(1 << 6)
#define ADT7X10_STAT_NOT_RDY		(1 << 7)

/*
 * ADT7X10 config
 */
#define ADT7X10_FAULT_QUEUE_MASK	(1 << 0 | 1 << 1)
#define ADT7X10_CT_POLARITY		(1 << 2)
#define ADT7X10_INT_POLARITY		(1 << 3)
#define ADT7X10_EVENT_MODE		(1 << 4)
#define ADT7X10_MODE_MASK		(1 << 5 | 1 << 6)
#define ADT7X10_FULL			(0 << 5 | 0 << 6)
#define ADT7X10_PD			(1 << 5 | 1 << 6)
#define ADT7X10_RESOLUTION		(1 << 7)

/*
 * ADT7X10 masks
 */
#define ADT7X10_T13_VALUE_MASK		0xFFF8
#define ADT7X10_T_HYST_MASK		0xF

/* straight from the datasheet */
#define ADT7X10_TEMP_MIN (-55000)
#define ADT7X10_TEMP_MAX 150000

/* Each client has this additional data */
struct adt7x10_data {
	const struct adt7x10_ops *ops;
	struct device		*bus_dev;
	struct mutex		update_lock;
	u8			config;
	u8			oldconfig;
	bool			valid;		/* true if registers valid */
	unsigned long		last_updated;	/* In jiffies */
	s16			temp[4];	/* Register values,
						   0 = input
						   1 = high
						   2 = low
						   3 = critical */
	u8			hyst;		/* hysteresis offset */
};

static int adt7x10_read_byte(struct device *dev, u8 reg)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->read_byte(dev, reg);
}

static int adt7x10_write_byte(struct device *dev, u8 reg, u8 data)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->write_byte(dev, reg, data);
}

static int adt7x10_read_word(struct device *dev, u8 reg)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->read_word(dev, reg);
}

static int adt7x10_write_word(struct device *dev, u8 reg, u16 data)
{
	struct adt7x10_data *d = dev_get_drvdata(dev);
	return d->ops->write_word(dev, reg, data);
}

static const u8 ADT7X10_REG_TEMP[4] = {
	ADT7X10_TEMPERATURE,		/* input */
	ADT7X10_T_ALARM_HIGH,		/* high */
	ADT7X10_T_ALARM_LOW,		/* low */
	ADT7X10_T_CRIT,			/* critical */
};

static irqreturn_t adt7x10_irq_handler(int irq, void *private)
{
	struct device *dev = private;
	int status;

	status = adt7x10_read_byte(dev, ADT7X10_STATUS);
	if (status < 0)
		return IRQ_HANDLED;

	if (status & ADT7X10_STAT_T_HIGH)
		sysfs_notify(&dev->kobj, NULL, "temp1_max_alarm");
	if (status & ADT7X10_STAT_T_LOW)
		sysfs_notify(&dev->kobj, NULL, "temp1_min_alarm");
	if (status & ADT7X10_STAT_T_CRIT)
		sysfs_notify(&dev->kobj, NULL, "temp1_crit_alarm");

	return IRQ_HANDLED;
}

static int adt7x10_temp_ready(struct device *dev)
{
	int i, status;

	for (i = 0; i < 6; i++) {
		status = adt7x10_read_byte(dev, ADT7X10_STATUS);
		if (status < 0)
			return status;
		if (!(status & ADT7X10_STAT_NOT_RDY))
			return 0;
		msleep(60);
	}
	return -ETIMEDOUT;
}

static int adt7x10_update_temp(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		int temp;

		dev_dbg(dev, "Starting update\n");

		ret = adt7x10_temp_ready(dev); /* check for new value */
		if (ret)
			goto abort;

		temp = adt7x10_read_word(dev, ADT7X10_REG_TEMP[0]);
		if (temp < 0) {
			ret = temp;
			dev_dbg(dev, "Failed to read value: reg %d, error %d\n",
				ADT7X10_REG_TEMP[0], ret);
			goto abort;
		}
		data->temp[0] = temp;
		data->last_updated = jiffies;
		data->valid = true;
	}

abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int adt7x10_fill_cache(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);
	int ret;
	int i;

	for (i = 1; i < ARRAY_SIZE(data->temp); i++) {
		ret = adt7x10_read_word(dev, ADT7X10_REG_TEMP[i]);
		if (ret < 0) {
			dev_dbg(dev, "Failed to read value: reg %d, error %d\n",
				ADT7X10_REG_TEMP[i], ret);
			return ret;
		}
		data->temp[i] = ret;
	}

	ret = adt7x10_read_byte(dev, ADT7X10_T_HYST);
	if (ret < 0) {
		dev_dbg(dev, "Failed to read value: reg %d, error %d\n",
				ADT7X10_T_HYST, ret);
		return ret;
	}
	data->hyst = ret;

	return 0;
}

static s16 ADT7X10_TEMP_TO_REG(long temp)
{
	return DIV_ROUND_CLOSEST(clamp_val(temp, ADT7X10_TEMP_MIN,
					       ADT7X10_TEMP_MAX) * 128, 1000);
}

static int ADT7X10_REG_TO_TEMP(struct adt7x10_data *data, s16 reg)
{
	/* in 13 bit mode, bits 0-2 are status flags - mask them out */
	if (!(data->config & ADT7X10_RESOLUTION))
		reg &= ADT7X10_T13_VALUE_MASK;
	/*
	 * temperature is stored in twos complement format, in steps of
	 * 1/128°C
	 */
	return DIV_ROUND_CLOSEST(reg * 1000, 128);
}

/*-----------------------------------------------------------------------*/

/* sysfs attributes for hwmon */

static int adt7x10_temp_read(struct adt7x10_data *data, int index, long *val)
{
	struct device *dev = data->bus_dev;

	if (index == 0) {
		int ret;

		ret = adt7x10_update_temp(dev);
		if (ret)
			return ret;
	}

	*val = ADT7X10_REG_TO_TEMP(data, data->temp[index]);

	return 0;
}

static int adt7x10_temp_write(struct adt7x10_data *data, unsigned int nr,
			      long temp)
{
	struct device *dev = data->bus_dev;
	int ret;

	mutex_lock(&data->update_lock);
	data->temp[nr] = ADT7X10_TEMP_TO_REG(temp);
	ret = adt7x10_write_word(dev, ADT7X10_REG_TEMP[nr], data->temp[nr]);
	mutex_unlock(&data->update_lock);

	return ret;
}

static int adt7x10_hyst_read(struct adt7x10_data *data, unsigned int nr,
			     long *val)
{
	int hyst;

	hyst = (data->hyst & ADT7X10_T_HYST_MASK) * 1000;

	/*
	 * hysteresis is stored as a 4 bit offset in the device, convert it
	 * to an absolute value
	 */
	if (nr == 2)	/* min has positive offset, others have negative */
		hyst = -hyst;

	*val = ADT7X10_REG_TO_TEMP(data, data->temp[nr]) - hyst;

	return 0;
}

static int adt7x10_hyst_write(struct adt7x10_data *data, long hyst)
{
	struct device *dev = data->bus_dev;
	int limit;

	/* convert absolute hysteresis value to a 4 bit delta value */
	limit = ADT7X10_REG_TO_TEMP(data, data->temp[1]);
	hyst = clamp_val(hyst, ADT7X10_TEMP_MIN, ADT7X10_TEMP_MAX);
	data->hyst = clamp_val(DIV_ROUND_CLOSEST(limit - hyst, 1000),
			       0, ADT7X10_T_HYST_MASK);
	return adt7x10_write_byte(dev, ADT7X10_T_HYST, data->hyst);
}

static int adt7x10_alarm_read(struct adt7x10_data *data, unsigned int index,
			      long *val)
{
	struct device *dev = data->bus_dev;
	int ret;

	ret = adt7x10_read_byte(dev, ADT7X10_STATUS);
	if (ret < 0)
		return ret;

	*val = !!(ret & index);

	return 0;
}

static umode_t adt7x10_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	umode_t mode = 0444;

	switch (attr) {
	case hwmon_temp_max:
	case hwmon_temp_min:
	case hwmon_temp_crit:
	case hwmon_temp_max_hyst:
		mode |= 0200;
		break;
	default:
		break;
	}

	return mode;
}

static int adt7x10_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_input:
		return adt7x10_temp_read(data, 0, val);
	case hwmon_temp_max:
		return adt7x10_temp_read(data, 1, val);
	case hwmon_temp_min:
		return adt7x10_temp_read(data, 2, val);
	case hwmon_temp_crit:
		return adt7x10_temp_read(data, 3, val);
	case hwmon_temp_max_hyst:
		return adt7x10_hyst_read(data, 1, val);
	case hwmon_temp_min_hyst:
		return adt7x10_hyst_read(data, 2, val);
	case hwmon_temp_crit_hyst:
		return adt7x10_hyst_read(data, 3, val);
	case hwmon_temp_min_alarm:
		return adt7x10_alarm_read(data, ADT7X10_STAT_T_LOW, val);
	case hwmon_temp_max_alarm:
		return adt7x10_alarm_read(data, ADT7X10_STAT_T_HIGH, val);
	case hwmon_temp_crit_alarm:
		return adt7x10_alarm_read(data, ADT7X10_STAT_T_CRIT, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int adt7x10_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_max:
		return adt7x10_temp_write(data, 1, val);
	case hwmon_temp_min:
		return adt7x10_temp_write(data, 2, val);
	case hwmon_temp_crit:
		return adt7x10_temp_write(data, 3, val);
	case hwmon_temp_max_hyst:
		return adt7x10_hyst_write(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *adt7x10_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MIN |
			   HWMON_T_CRIT | HWMON_T_MAX_HYST | HWMON_T_MIN_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM),
	NULL,
};

static const struct hwmon_ops adt7x10_hwmon_ops = {
	.is_visible = adt7x10_is_visible,
	.read = adt7x10_read,
	.write = adt7x10_write,
};

static const struct hwmon_chip_info adt7x10_chip_info = {
	.ops = &adt7x10_hwmon_ops,
	.info = adt7x10_info,
};

static void adt7x10_restore_config(void *private)
{
	struct adt7x10_data *data = private;
	struct device *dev = data->bus_dev;

	if (data->oldconfig == data->config)
		return;

	adt7x10_write_byte(dev, ADT7X10_CONFIG, data->oldconfig);
}

int adt7x10_probe(struct device *dev, const char *name, int irq,
		  const struct adt7x10_ops *ops)
{
	struct adt7x10_data *data;
	struct device *hdev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ops = ops;
	data->bus_dev = dev;

	dev_set_drvdata(dev, data);
	mutex_init(&data->update_lock);

	/* configure as specified */
	ret = adt7x10_read_byte(dev, ADT7X10_CONFIG);
	if (ret < 0) {
		dev_dbg(dev, "Can't read config? %d\n", ret);
		return ret;
	}
	data->oldconfig = ret;

	/*
	 * Set to 16 bit resolution, continous conversion and comparator mode.
	 */
	data->config = data->oldconfig;
	data->config &= ~(ADT7X10_MODE_MASK | ADT7X10_CT_POLARITY |
			ADT7X10_INT_POLARITY);
	data->config |= ADT7X10_FULL | ADT7X10_RESOLUTION | ADT7X10_EVENT_MODE;

	if (data->config != data->oldconfig) {
		ret = adt7x10_write_byte(dev, ADT7X10_CONFIG, data->config);
		if (ret)
			return ret;
	}
	dev_dbg(dev, "Config %02x\n", data->config);

	ret = devm_add_action_or_reset(dev, adt7x10_restore_config, data);
	if (ret)
		return ret;

	ret = adt7x10_fill_cache(dev);
	if (ret)
		return ret;

	hdev = devm_hwmon_device_register_with_info(dev, name, data,
						    &adt7x10_chip_info, NULL);
	if (IS_ERR(hdev)) {
		ret = PTR_ERR(hdev);
		return ret;
	}

	if (irq > 0) {
		ret = devm_request_threaded_irq(dev, irq, NULL,
						adt7x10_irq_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						dev_name(dev), dev);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adt7x10_probe);

void adt7x10_remove(struct device *dev, int irq)
{
}
EXPORT_SYMBOL_GPL(adt7x10_remove);

#ifdef CONFIG_PM_SLEEP

static int adt7x10_suspend(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return adt7x10_write_byte(dev, ADT7X10_CONFIG,
		data->config | ADT7X10_PD);
}

static int adt7x10_resume(struct device *dev)
{
	struct adt7x10_data *data = dev_get_drvdata(dev);

	return adt7x10_write_byte(dev, ADT7X10_CONFIG, data->config);
}

SIMPLE_DEV_PM_OPS(adt7x10_dev_pm_ops, adt7x10_suspend, adt7x10_resume);
EXPORT_SYMBOL_GPL(adt7x10_dev_pm_ops);

#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("Hartmut Knaack");
MODULE_DESCRIPTION("ADT7410/ADT7420, ADT7310/ADT7320 common code");
MODULE_LICENSE("GPL");
