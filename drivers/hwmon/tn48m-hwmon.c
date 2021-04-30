// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delta TN48M CPLD HWMON driver
 *
 * Copyright 2020 Sartura Ltd
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 */

#include <linux/bitfield.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/tn48m.h>

#define PSU1_PRESENT_MASK	BIT(0)
#define PSU2_PRESENT_MASK	BIT(1)
#define PSU1_POWERGOOD_MASK	BIT(2)
#define PSU2_POWERGOOD_MASK	BIT(3)
#define PSU1_ALERT_MASK		BIT(4)
#define PSU2_ALERT_MASK		BIT(5)

static int board_id_get(struct tn48m_data *data)
{
	unsigned int regval;

	regmap_read(data->regmap, BOARD_ID, &regval);

	switch (regval) {
	case BOARD_ID_TN48M:
		return BOARD_ID_TN48M;
	case BOARD_ID_TN48M_P:
		return BOARD_ID_TN48M_P;
	default:
		return -EINVAL;
	}
}

static ssize_t psu_present_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct tn48m_data *data = dev_get_drvdata(dev);
	unsigned int regval, status;

	if (board_id_get(data) == BOARD_ID_TN48M_P) {
		regmap_read(data->regmap, attr2->nr, &regval);

		if (attr2->index == 1)
			status = !FIELD_GET(PSU1_PRESENT_MASK, regval);
		else
			status = !FIELD_GET(PSU2_PRESENT_MASK, regval);
	} else
		status = 0;

	return sprintf(buf, "%d\n", status);
}

static ssize_t psu_pg_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct tn48m_data *data = dev_get_drvdata(dev);
	unsigned int regval, status;

	regmap_read(data->regmap, attr2->nr, &regval);

	if (attr2->index == 1)
		status = FIELD_GET(PSU1_POWERGOOD_MASK, regval);
	else
		status = FIELD_GET(PSU2_POWERGOOD_MASK, regval);

	return sprintf(buf, "%d\n", status);
}

static ssize_t psu_alert_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct tn48m_data *data = dev_get_drvdata(dev);
	unsigned int regval, status;

	if (board_id_get(data) == BOARD_ID_TN48M_P) {
		regmap_read(data->regmap, attr2->nr, &regval);

		if (attr2->index == 1)
			status = !FIELD_GET(PSU1_ALERT_MASK, regval);
		else
			status = !FIELD_GET(PSU2_ALERT_MASK, regval);
	} else
		status = 0;

	return sprintf(buf, "%d\n", status);
}

static SENSOR_DEVICE_ATTR_2_RO(psu1_present, psu_present, PSU_STATUS, 1);
static SENSOR_DEVICE_ATTR_2_RO(psu2_present, psu_present, PSU_STATUS, 2);
static SENSOR_DEVICE_ATTR_2_RO(psu1_pg, psu_pg, PSU_STATUS, 1);
static SENSOR_DEVICE_ATTR_2_RO(psu2_pg, psu_pg, PSU_STATUS, 2);
static SENSOR_DEVICE_ATTR_2_RO(psu1_alert, psu_alert, PSU_STATUS, 1);
static SENSOR_DEVICE_ATTR_2_RO(psu2_alert, psu_alert, PSU_STATUS, 2);

static struct attribute *tn48m_hwmon_attrs[] = {
	&sensor_dev_attr_psu1_present.dev_attr.attr,
	&sensor_dev_attr_psu2_present.dev_attr.attr,
	&sensor_dev_attr_psu1_pg.dev_attr.attr,
	&sensor_dev_attr_psu2_pg.dev_attr.attr,
	&sensor_dev_attr_psu1_alert.dev_attr.attr,
	&sensor_dev_attr_psu2_alert.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(tn48m_hwmon);

static int tn48m_hwmon_probe(struct platform_device *pdev)
{
	struct tn48m_data *data = dev_get_drvdata(pdev->dev.parent);
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev,
							   "tn48m_hwmon",
							   data,
							   tn48m_hwmon_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct platform_device_id tn48m_hwmon_id_table[] = {
	{ "delta,tn48m-hwmon", },
	{ }
};
MODULE_DEVICE_TABLE(platform, tn48m_hwmon_id_table);

static struct platform_driver tn48m_hwmon_driver = {
	.driver = {
		.name = "tn48m-hwmon",
	},
	.probe = tn48m_hwmon_probe,
	.id_table = tn48m_hwmon_id_table,
};
module_platform_driver(tn48m_hwmon_driver);

MODULE_AUTHOR("Robert Marko <robert.marko@sartura.hr>");
MODULE_DESCRIPTION("Delta TN48M CPLD HWMON driver");
MODULE_LICENSE("GPL");
