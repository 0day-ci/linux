// SPDX-License-Identifier: GPL-2.0
/*
 * virtual_thermal_sensor.c - DT-based virtual thermal sensor driver.
 *
 * Copyright (c) 2021 BayLibre
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/string.h>

#include <dt-bindings/thermal/virtual-sensor.h>

struct virtual_thermal_zone_device {
	struct thermal_zone_device *zone;
	struct module *owner;
};

struct virtual_thermal_sensor {
	int count;
	struct virtual_thermal_zone_device *zones;
	struct thermal_zone_device *tzd;
	int (*aggr_temp)(int temp1, int temp2);

	struct list_head node;
};

static int max_temp(int temp1, int temp2)
{
	return max(temp1, temp2);
}

static int min_temp(int temp1, int temp2)
{
	return min(temp1, temp2);
}

static int avg_temp(int temp1, int temp2)
{
	return (temp1 + temp2) / 2;
}

static int virtual_thermal_sensor_get_temp(void *data, int *temperature)
{
	struct virtual_thermal_sensor *sensor = data;
	int max_temp = INT_MIN;
	int temp;
	int i;

	for (i = 0; i < sensor->count; i++) {
		struct thermal_zone_device *zone;

		zone = sensor->zones[i].zone;
		zone->ops->get_temp(zone, &temp);
		max_temp = sensor->aggr_temp(max_temp, temp);
	}

	*temperature = max_temp;

	return 0;
}

static const struct thermal_zone_of_device_ops virtual_thermal_sensor_ops = {
	.get_temp = virtual_thermal_sensor_get_temp,
};

int virtual_thermal_sensor_get_module(struct virtual_thermal_zone_device *zone,
				      const char *name)
{
		struct platform_device *sensor_pdev;
		struct device_node *node;

		node = of_find_node_by_name(NULL, name);
		if (!node)
			return -ENODEV;

		node = of_parse_phandle(node, "thermal-sensors", 0);
		if (!node)
			return -ENODEV;

		sensor_pdev = of_find_device_by_node(node);
		if (!sensor_pdev)
			return -ENODEV;

		if (!sensor_pdev->dev.driver)
			return -EPROBE_DEFER;

		if (!try_module_get(sensor_pdev->dev.driver->owner))
			return -ENODEV;

		zone->owner = sensor_pdev->dev.driver->owner;

		return 0;
}

void virtual_thermal_sensor_put_modules(struct virtual_thermal_sensor *sensor)
{
	int i;

	for (i = 0; i < sensor->count; i++) {
		if (sensor->zones[i].zone)
			module_put(sensor->zones[i].owner);
	}
}

static int virtual_thermal_sensor_probe(struct platform_device *pdev)
{
	struct virtual_thermal_sensor *sensor;
	struct device *dev = &pdev->dev;
	struct property *prop;
	const char *name;
	u32 type;
	int ret;
	int i = 0;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->count = of_property_count_strings(dev->of_node, "thermal-sensors");
	if (sensor->count <= 0)
		return -EINVAL;

	sensor->zones = devm_kmalloc_array(dev, sensor->count,
					     sizeof(*sensor->zones),
					     GFP_KERNEL);
	if (!sensor->zones)
		return -ENOMEM;

	of_property_for_each_string(dev->of_node, "thermal-sensors", prop, name) {
		struct virtual_thermal_zone_device *virtual_zone;
		struct thermal_zone_device *zone;

		virtual_zone = &sensor->zones[i++];

		zone = thermal_zone_get_zone_by_name(name);
		if (IS_ERR(zone))
			return PTR_ERR(zone);

		ret = virtual_thermal_sensor_get_module(virtual_zone, name);
		if (ret)
			goto err;

		virtual_zone->zone = zone;
	}

	ret = of_property_read_u32(dev->of_node, "aggregation-function", &type);
	if (ret)
		return ret;

	switch (type) {
	case VIRTUAL_THERMAL_SENSOR_MAX_VAL:
		sensor->aggr_temp = max_temp;
		break;
	case VIRTUAL_THERMAL_SENSOR_MIN_VAL:
		sensor->aggr_temp = min_temp;
		break;
	case VIRTUAL_THERMAL_SENSOR_AVG_VAL:
		sensor->aggr_temp = avg_temp;
		break;
	default:
		return -EINVAL;
	}

	sensor->tzd = devm_thermal_zone_of_sensor_register(dev, 0, sensor,
							   &virtual_thermal_sensor_ops);
	if (IS_ERR(sensor->tzd))
		return PTR_ERR(sensor->tzd);

	platform_set_drvdata(pdev, sensor);

	return 0;

err:
	virtual_thermal_sensor_put_modules(sensor);

	return ret;
}

static int virtual_thermal_sensor_remove(struct platform_device *pdev)
{
	struct virtual_thermal_sensor *sensor;

	sensor = platform_get_drvdata(pdev);
	list_del(&sensor->node);

	virtual_thermal_sensor_put_modules(sensor);

	return 0;
}

static const struct of_device_id virtual_thermal_sensor_of_match[] = {
	{
		.compatible = "virtual,thermal-sensor",
	},
	{
	},
};
MODULE_DEVICE_TABLE(of, virtual_thermal_sensor_of_match);

static struct platform_driver virtual_thermal_sensor = {
	.probe = virtual_thermal_sensor_probe,
	.remove = virtual_thermal_sensor_remove,
	.driver = {
		.name = "virtual-thermal-sensor",
		.of_match_table = virtual_thermal_sensor_of_match,
	},
};

module_platform_driver(virtual_thermal_sensor);
MODULE_AUTHOR("Alexandre Bailon <abailon@baylibre.com>");
MODULE_DESCRIPTION("Virtual thermal sensor");
MODULE_LICENSE("GPL v2");
