// SPDX-License-Identifier: GPL-2.0
/*
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

#include "virtual-sensor.h"

struct virtual_sensor_data {
	struct list_head node;

	/* sensor interface */
	int id;
	void *sensor_data;
	const struct thermal_zone_of_device_ops *ops;
};

struct virtual_sensor {
	int count;
	struct virtual_sensor_data *sensors;
	struct thermal_zone_device *tzd;

	struct list_head node;
};

static LIST_HEAD(thermal_sensors);
static LIST_HEAD(virtual_sensors);

static int virtual_sensor_get_temp_max(void *data, int *temperature)
{
	struct virtual_sensor *sensor = data;
	int max_temp = INT_MIN;
	int temp;
	int i;

	for (i = 0; i < sensor->count; i++) {
		struct virtual_sensor_data *hw_sensor;

		hw_sensor = &sensor->sensors[i];
		if (!hw_sensor->ops)
			return -ENODEV;

		hw_sensor->ops->get_temp(hw_sensor->sensor_data, &temp);
		max_temp = max(max_temp, temp);
	}

	*temperature = max_temp;

	return 0;
}

static const struct thermal_zone_of_device_ops virtual_sensor_max_ops = {
	.get_temp = virtual_sensor_get_temp_max,
};

static int virtual_sensor_get_temp_min(void *data, int *temperature)
{
	struct virtual_sensor *sensor = data;
	int min_temp = INT_MAX;
	int temp;
	int i;

	for (i = 0; i < sensor->count; i++) {
		struct virtual_sensor_data *hw_sensor;

		hw_sensor = &sensor->sensors[i];
		if (!hw_sensor->ops)
			return -ENODEV;

		hw_sensor->ops->get_temp(hw_sensor->sensor_data, &temp);
		min_temp = min(min_temp, temp);
	}

	*temperature = min_temp;

	return 0;
}

static const struct thermal_zone_of_device_ops virtual_sensor_min_ops = {
	.get_temp = virtual_sensor_get_temp_min,
};

static int do_avg(int val1, int val2)
{
	return ((val1) / 2) + ((val2) / 2) + (((val1) % 2 + (val2) % 2) / 2);
}

static int virtual_sensor_get_temp_avg(void *data, int *temperature)
{
	struct virtual_sensor *sensor = data;
	int avg_temp = 0;
	int temp;
	int i;

	for (i = 0; i < sensor->count; i++) {
		struct virtual_sensor_data *hw_sensor;

		hw_sensor = &sensor->sensors[i];
		if (!hw_sensor->ops)
			return -ENODEV;

		hw_sensor->ops->get_temp(hw_sensor->sensor_data, &temp);
		avg_temp = do_avg(avg_temp, temp);
	}

	*temperature = avg_temp;

	return 0;
}

static const struct thermal_zone_of_device_ops virtual_sensor_avg_ops = {
	.get_temp = virtual_sensor_get_temp_avg,
};

static int register_virtual_sensor(struct virtual_sensor *sensor,
				    struct of_phandle_args args,
				    int index)
{
	struct virtual_sensor_data *sensor_data;
	int id;

	list_for_each_entry(sensor_data, &thermal_sensors, node) {
		id = args.args_count ? args.args[0] : 0;
		if (sensor_data->id == id) {
			memcpy(&sensor->sensors[index], sensor_data,
				sizeof(*sensor_data));
			return 0;
		}
	}

	return -ENODEV;
}

static int virtual_sensor_probe(struct platform_device *pdev)
{
	const struct thermal_zone_of_device_ops *ops;
	struct virtual_sensor *sensor;
	struct device *dev = &pdev->dev;
	struct of_phandle_args args;
	u32 type;
	int ret;
	int i;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->count = of_count_phandle_with_args(dev->of_node,
						   "thermal-sensors",
						   "#thermal-sensor-cells");
	if (sensor->count <= 0)
		return -EINVAL;

	sensor->sensors = devm_kmalloc_array(dev, sensor->count,
					     sizeof(*sensor->sensors),
					     GFP_KERNEL);
	if (!sensor->sensors)
		return -ENOMEM;

	for (i = 0; i < sensor->count; i++) {
		ret = of_parse_phandle_with_args(dev->of_node,
						 "thermal-sensors",
						 "#thermal-sensor-cells",
						 i,
						 &args);
		if (ret)
			return ret;

		ret = register_virtual_sensor(sensor, args, i);
		if (ret)
			return ret;
	}

	ret = of_property_read_u32(dev->of_node, "type", &type);
	if (ret)
		return ret;

	switch (type) {
	case VIRTUAL_SENSOR_MAX:
		ops = &virtual_sensor_max_ops;
		break;
	case VIRTUAL_SENSOR_MIN:
		ops = &virtual_sensor_min_ops;
		break;
	case VIRTUAL_SENSOR_AVG:
		ops = &virtual_sensor_avg_ops;
		break;
	default:
		return -EINVAL;
	}

	sensor->tzd = devm_thermal_zone_of_sensor_register(dev, 0, sensor, ops);
	if (IS_ERR(sensor->tzd))
		return PTR_ERR(sensor->tzd);

	platform_set_drvdata(pdev, sensor);
	list_add(&sensor->node, &virtual_sensors);

	return 0;
}

static int virtual_sensor_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct virtual_sensor *sensor;

	sensor = platform_get_drvdata(pdev);
	list_del(&sensor->node);

	devm_thermal_zone_of_sensor_unregister(dev, sensor->tzd);
	devm_kfree(dev, sensor->sensors);
	devm_kfree(dev, sensor);

	return 0;
}

static const struct of_device_id virtual_sensor_of_match[] = {
	{
		.compatible = "virtual,thermal-sensor",
	},
	{
	},
};
MODULE_DEVICE_TABLE(of, thermal_aggr_of_match);

static struct platform_driver virtual_sensor = {
	.probe = virtual_sensor_probe,
	.remove = virtual_sensor_remove,
	.driver = {
		.name = "virtual-sensor",
		.of_match_table = virtual_sensor_of_match,
	},
};

/**
 * thermal_virtual_sensor_register - registers a sensor that could by a virtual
 * sensor
 * @dev: a valid struct device pointer of a sensor device. Must contain
 *       a valid .of_node, for the sensor node.
 * @sensor_id: a sensor identifier, in case the sensor IP has more
 *             than one sensors
 * @data: a private pointer (owned by the caller) that will be passed
 *        back, when a temperature reading is needed.
 * @ops: struct thermal_zone_of_device_ops *. Must contain at least .get_temp.
 *
 * This function will register a thermal sensor to make it available for later
 * usage by a virtual sensor.
 *
 * The thermal zone temperature is provided by the @get_temp function
 * pointer. When called, it will have the private pointer @data back.
 *
 * Return: On success returns a valid struct thermal_zone_device,
 * otherwise, it returns a corresponding ERR_PTR(). Caller must
 * check the return value with help of IS_ERR() helper.
 */
struct virtual_sensor_data *thermal_virtual_sensor_register(
	struct device *dev, int sensor_id, void *data,
	const struct thermal_zone_of_device_ops *ops)
{
	struct virtual_sensor_data *sensor_data;

	sensor_data = devm_kzalloc(dev, sizeof(*sensor_data), GFP_KERNEL);
	if (!sensor_data)
		return ERR_PTR(-ENOMEM);

	sensor_data->id = sensor_id;
	sensor_data->sensor_data = data;
	sensor_data->ops = ops;

	list_add(&sensor_data->node, &thermal_sensors);

	return sensor_data;
}
EXPORT_SYMBOL_GPL(thermal_virtual_sensor_register);

/**
 * thermal_virtual_sensor_unregister - unregisters a sensor
 * @dev: a valid struct device pointer of a sensor device.
 * @sensor_data: a pointer to struct virtual_sensor_data to unregister.
 *
 * This function removes the sensor from the list of available thermal sensors.
 * If the sensor is in use, then the next call to .get_temp will return -ENODEV.
 */
void thermal_virtual_sensor_unregister(struct device *dev,
				       struct virtual_sensor_data *sensor_data)
{
	struct virtual_sensor_data *temp;
	struct virtual_sensor *sensor;
	int i;

	list_del(&sensor_data->node);

	list_for_each_entry(sensor, &virtual_sensors, node) {
		for (i = 0; i < sensor->count; i++) {
			temp = &sensor->sensors[i];
			if (temp->id == sensor_data->id &&
				temp->sensor_data == sensor_data->sensor_data) {
				temp->ops = NULL;
			}
		}
	}
	devm_kfree(dev, sensor_data);
}
EXPORT_SYMBOL_GPL(thermal_virtual_sensor_unregister);

static void devm_thermal_virtual_sensor_release(struct device *dev, void *res)
{
	thermal_virtual_sensor_unregister(dev,
					  *(struct virtual_sensor_data **)res);
}

static int devm_thermal_virtual_sensor_match(struct device *dev, void *res,
					     void *data)
{
	struct virtual_sensor_data **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}


/**
 * devm_thermal_virtual_sensor_register - Resource managed version of
 *				thermal_virtual_sensor_register()
 * @dev: a valid struct device pointer of a sensor device. Must contain
 *       a valid .of_node, for the sensor node.
 * @sensor_id: a sensor identifier, in case the sensor IP has more
 *	       than one sensors
 * @data: a private pointer (owned by the caller) that will be passed
 *	  back, when a temperature reading is needed.
 * @ops: struct thermal_zone_of_device_ops *. Must contain at least .get_temp.
 *
 * Refer thermal_zone_of_sensor_register() for more details.
 *
 * Return: On success returns a valid struct virtual_sensor_data,
 * otherwise, it returns a corresponding ERR_PTR(). Caller must
 * check the return value with help of IS_ERR() helper.
 * Registered virtual_sensor_data device will automatically be
 * released when device is unbounded.
 */
struct virtual_sensor_data *devm_thermal_virtual_sensor_register(
	struct device *dev, int sensor_id,
	void *data, const struct thermal_zone_of_device_ops *ops)
{
	struct virtual_sensor_data **ptr, *sensor_data;

	ptr = devres_alloc(devm_thermal_virtual_sensor_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	sensor_data = thermal_virtual_sensor_register(dev, sensor_id, data, ops);
	if (IS_ERR(sensor_data)) {
		devres_free(ptr);
		return sensor_data;
	}

	*ptr = sensor_data;
	devres_add(dev, ptr);

	return sensor_data;
}
EXPORT_SYMBOL_GPL(devm_thermal_virtual_sensor_register);

/**
 * devm_thermal_virtual_sensor_unregister - Resource managed version of
 *				thermal_virtual_sensor_unregister().
 * @dev: Device for which resource was allocated.
 * @sensor: a pointer to struct thermal_zone_device where the sensor is registered.
 *
 * This function removes the sensor from the list of sensors registered with
 * devm_thermal_virtual_sensor_register() API.
 * Normally this function will not need to be called and the resource
 * management code will ensure that the resource is freed.
 */
void devm_thermal_virtual_sensor_unregister(struct device *dev,
					    struct virtual_sensor *sensor)
{
	WARN_ON(devres_release(dev, devm_thermal_virtual_sensor_release,
			       devm_thermal_virtual_sensor_match, sensor));
}
EXPORT_SYMBOL_GPL(devm_thermal_virtual_sensor_unregister);

module_platform_driver(virtual_sensor);
MODULE_AUTHOR("Alexandre Bailon <abailon@baylibre.com>");
MODULE_DESCRIPTION("Virtual thermal sensor");
MODULE_LICENSE("GPL v2");
