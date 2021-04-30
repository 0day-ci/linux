// SPDX-License-Identifier: GPL-2.0-only
/*
 * vcnl3020-hwmon.c - intrusion sensor.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/iio/proximity/vcnl3020.h>

static ssize_t vcnl3020_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct vcnl3020_data *vcnl3020_data = dev_get_drvdata(dev);

	bool data = vcnl3020_is_thr_triggered(vcnl3020_data);

	return sprintf(buf, "%u\n", data);
}

static SENSOR_DEVICE_ATTR_2_RO(intrusion0_alarm, vcnl3020, 0, 0);

static struct attribute *vcnl3020_attrs[] = {
	&sensor_dev_attr_intrusion0_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(vcnl3020);

static int32_t vcnl3020_hwmon_probe(struct platform_device *pdev)
{
	struct vcnl3020_data *vcnl3020_data = platform_get_drvdata(pdev);
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev,
							   VCNL3020_DRV,
							   vcnl3020_data,
							   vcnl3020_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver vcnl3020_hwmon_driver = {
	.probe = vcnl3020_hwmon_probe,
	.driver = {
		.name = VCNL3020_DRV_HWMON,
	},
};

module_platform_driver(vcnl3020_hwmon_driver);

MODULE_AUTHOR("Ivan Mikhaylov <i.mikhaylov@yadro.com>");
MODULE_DESCRIPTION("Intrusion sensor for VCNL3020");
MODULE_LICENSE("GPL");
