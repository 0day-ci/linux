// SPDX-License-Identifier: GPL-2.0+
/*
 * Functions to access SY3686A power management chip temperature
 *
 * Copyright (C) 2019 reMarkable AS - http://www.remarkable.com/
 *
 * Authors: Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>
 *          Alistair Francis <alistair@alistair23.me>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include <linux/mfd/sy7636a.h>

static int sy7636a_get_temp(void *arg, int *res)
{
	unsigned int mode_ctr;
	int ret, reg_val;
	struct regmap *regmap = arg;
	bool isVoltageActive;

	ret = regmap_read(regmap,
			SY7636A_REG_OPERATION_MODE_CRL, &mode_ctr);
	if (ret)
		return ret;

	isVoltageActive = mode_ctr & SY7636A_OPERATION_MODE_CRL_ONOFF;

	/* If operation mode isn't set to control, then let's set it. */
	if (!isVoltageActive) {
		ret = regmap_write(regmap,
				SY7636A_REG_OPERATION_MODE_CRL,
				mode_ctr | SY7636A_OPERATION_MODE_CRL_ONOFF);
		if (ret)
			return ret;
	}

	ret = regmap_read(regmap,
			SY7636A_REG_TERMISTOR_READOUT, &reg_val);
	if (ret)
		return ret;

	/* Restore the operation mode if it wasn't set */
	if (!isVoltageActive) {
		ret = regmap_write(regmap,
				SY7636A_REG_OPERATION_MODE_CRL,
				mode_ctr);
		if (ret)
			return ret;
	}

	*res = reg_val * 1000;

	return ret;
}

static const struct thermal_zone_of_device_ops ops = {
	.get_temp	= sy7636a_get_temp,
};

static int sy7636a_thermal_probe(struct platform_device *pdev)
{
	struct regmap *regmap = dev_get_regmap(pdev->dev.parent, NULL);
	struct thermal_zone_device *thermal_zone_dev;

	thermal_zone_dev = devm_thermal_zone_of_sensor_register(
			pdev->dev.parent,
			0,
			regmap,
			&ops);

	return PTR_ERR_OR_ZERO(thermal_zone_dev);
}

static const struct platform_device_id sy7636a_thermal_id_table[] = {
	{ "sy7636a-thermal", },
	{ }
};
MODULE_DEVICE_TABLE(platform, sy7636a_thermal_id_table);

static struct platform_driver sy7636a_thermal_driver = {
	.driver = {
		.name = "sy7636a-thermal",
	},
	.probe = sy7636a_thermal_probe,
	.id_table = sy7636a_thermal_id_table,
};
module_platform_driver(sy7636a_thermal_driver);

MODULE_AUTHOR("Lars Ivar Miljeteig <lars.ivar.miljeteig@remarkable.com>");
MODULE_DESCRIPTION("SY7636A thermal driver");
MODULE_LICENSE("GPL v2");
