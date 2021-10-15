// SPDX-License-Identifier: GPL-2.0+
/*
 * Functions to access SY3686A power management chip temperature
 *
 * Copyright (C) 2019 reMarkable AS - http://www.remarkable.com/
 *
 * Authors: Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>
 *          Alistair Francis <alistair@alistair23.me>
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>

#include <linux/mfd/sy7636a.h>

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	unsigned int reg_val;
	struct regmap *regmap = dev_get_drvdata(dev);
	int ret;

	ret = regmap_read(regmap, SY7636A_REG_TERMISTOR_READOUT, &reg_val);
	if (ret)
		return ret;

	return snprintf(buf, PAGE_SIZE, "%d\n", reg_val);
}

static SENSOR_DEVICE_ATTR(temp0, 0444, show_temp, NULL, 0);

static struct attribute *sy7636a_attrs[] = {
	&sensor_dev_attr_temp0.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(sy7636a);

static int sy7636a_sensor_probe(struct platform_device *pdev)
{
	struct regmap *regmap = dev_get_regmap(pdev->dev.parent, NULL);
	struct device *hwmon_dev;
	int err;

	if (!regmap)
		return -EPROBE_DEFER;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
			"sy7636a_temperature", regmap, NULL, sy7636a_groups);

	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		dev_err(&pdev->dev, "Unable to register hwmon device, returned %d\n", err);
		return err;
	}

	return 0;
}

static struct platform_driver sy7636a_sensor_driver = {
	.probe = sy7636a_sensor_probe,
	.driver = {
		.name = "sy7636a-temperature",
	},
};
module_platform_driver(sy7636a_sensor_driver);

MODULE_DESCRIPTION("SY7636A sensor driver");
MODULE_LICENSE("GPL");
