/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 BayLibre
 */

#ifndef __THERMAL_VIRTUAL_SENSOR_H__
#define __THERMAL_VIRTUAL_SENSOR_H__

struct virtual_sensor;
struct virtual_sensor_data;

#ifdef CONFIG_VIRTUAL_THERMAL
struct virtual_sensor_data *
thermal_virtual_sensor_register(struct device *dev, int sensor_id, void *data,
				const struct thermal_zone_of_device_ops *ops);
void thermal_virtual_sensor_unregister(struct device *dev,
				       struct virtual_sensor_data *sensor_data);
struct virtual_sensor_data *
devm_thermal_virtual_sensor_register(struct device *dev, int sensor_id, void *data,
				     const struct thermal_zone_of_device_ops *ops);

void devm_thermal_virtual_sensor_unregister(struct device *dev,
					    struct virtual_sensor *sensor);
#else
static inline struct virtual_sensor_data *
thermal_virtual_sensor_register(struct device *dev, int sensor_id, void *data,
				const struct thermal_zone_of_device_ops *ops)
{
	return ERR_PTR(-ENODEV);
}

void thermal_virtual_sensor_unregister(struct device *dev,
				       struct virtual_sensor_data *sensor_data)
{
}

static inline struct virtual_sensor_data *
devm_thermal_virtual_sensor_register(struct device *dev, int sensor_id, void *data,
				     const struct thermal_zone_of_device_ops *ops)
{
	return ERR_PTR(-ENODEV);
}

static inline
void devm_thermal_virtual_sensor_unregister(struct device *dev,
					    struct virtual_sensor *sensor)
{
}
#endif

#endif /* __THERMAL_VIRTUAL_SENSOR_H__ */
