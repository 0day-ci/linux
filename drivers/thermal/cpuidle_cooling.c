// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Linaro Limited.
 *
 *  Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 */
#define pr_fmt(fmt) "cpuidle cooling: " fmt

#include <linux/cpu_cooling.h>
#include <linux/cpuidle.h>
#include <linux/err.h>
#include <linux/idle_inject.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "cpuidle_cooling_core.h"

/**
 * __cpuidle_cooling_register: register the cooling device
 * @drv: a cpuidle driver structure pointer
 * @np: a device node structure pointer used for the thermal binding
 *
 * This function is in charge of allocating the cpuidle cooling device
 * structure, the idle injection, initialize them and register the
 * cooling device to the thermal framework.
 *
 * Return: zero on success, a negative value returned by one of the
 * underlying subsystem in case of error
 */
static int __cpuidle_cooling_register(struct device_node *np,
				      struct cpuidle_driver *drv)
{
	struct idle_inject_device *ii_dev;
	struct cpuidle_cooling_device *idle_cdev;
	struct thermal_cooling_device *cdev;
	struct device *dev;
	unsigned int idle_duration_us = TICK_USEC;
	unsigned int latency_us = UINT_MAX;
	char *name;
	int ret;

	idle_cdev = kzalloc(sizeof(*idle_cdev), GFP_KERNEL);
	if (!idle_cdev) {
		ret = -ENOMEM;
		goto out;
	}

	ii_dev = idle_inject_register(drv->cpumask);
	if (!ii_dev) {
		ret = -EINVAL;
		goto out_kfree;
	}

	of_property_read_u32(np, "duration-us", &idle_duration_us);
	of_property_read_u32(np, "exit-latency-us", &latency_us);

	idle_inject_set_duration(ii_dev, TICK_USEC, idle_duration_us);
	idle_inject_set_latency(ii_dev, latency_us);

	idle_cdev->ii_dev = ii_dev;

	dev = get_cpu_device(cpumask_first(drv->cpumask));

	name = kasprintf(GFP_KERNEL, "idle-%s", dev_name(dev));
	if (!name) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	cdev = thermal_of_cooling_device_register(np, name, idle_cdev,
						  cpuidle_cooling_get_ops());
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		goto out_kfree_name;
	}

	pr_debug("%s: Idle injection set with idle duration=%u, latency=%u\n",
		 name, idle_duration_us, latency_us);

	kfree(name);

	return 0;

out_kfree_name:
	kfree(name);
out_unregister:
	idle_inject_unregister(ii_dev);
out_kfree:
	kfree(idle_cdev);
out:
	return ret;
}

/**
 * cpuidle_cooling_register - Idle cooling device initialization function
 * @drv: a cpuidle driver structure pointer
 *
 * This function is in charge of creating a cooling device per cpuidle
 * driver and register it to the thermal framework.
 *
 * Return: zero on success, or negative value corresponding to the
 * error detected in the underlying subsystems.
 */
void cpuidle_cooling_register(struct cpuidle_driver *drv)
{
	struct device_node *cooling_node;
	struct device_node *cpu_node;
	int cpu, ret;

	for_each_cpu(cpu, drv->cpumask) {

		cpu_node = of_cpu_device_node_get(cpu);

		cooling_node = of_get_child_by_name(cpu_node, "thermal-idle");

		of_node_put(cpu_node);

		if (!cooling_node) {
			pr_debug("'thermal-idle' node not found for cpu%d\n", cpu);
			continue;
		}

		ret = __cpuidle_cooling_register(cooling_node, drv);

		of_node_put(cooling_node);

		if (ret) {
			pr_err("Failed to register the cpuidle cooling device" \
			       "for cpu%d: %d\n", cpu, ret);
			break;
		}
	}
}
