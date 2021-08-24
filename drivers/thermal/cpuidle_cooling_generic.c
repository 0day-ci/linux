// SPDX-License-Identifier: GPL-2.0
/*
 * Generic cpu idle cooling driver
 * Copyright (c) 2021, Intel Corporation.
 * All rights reserved.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufeature.h>
#include <linux/cpuhotplug.h>
#include <linux/idle_inject.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/topology.h>

#include "cpuidle_cooling_core.h"

#define IDLE_DURATION   10000
#define IDLE_LATENCY    5000

static int idle_duration_us = IDLE_DURATION;
static int idle_latency_us = IDLE_LATENCY;

module_param(idle_duration_us, int, 0644);
MODULE_PARM_DESC(idle_duration_us,
		 "Idle duration in us.");

module_param(idle_latency_us, int, 0644);
MODULE_PARM_DESC(idle_latency_us,
		 "Idle latency in us.");

struct cpuidle_cooling {
	struct thermal_cooling_device *cdev;
	struct idle_inject_device *ii_dev;
	struct cpuidle_cooling_device *idle_cdev;
};
static DEFINE_PER_CPU(struct cpuidle_cooling, cooling_devs);
static cpumask_t cpuidle_cpu_mask;

static int cpuidle_cooling_register(int cpu)
{
	struct cpuidle_cooling *cooling_dev = &per_cpu(cooling_devs, cpu);
	struct cpuidle_cooling_device *idle_cdev;
	struct thermal_cooling_device *cdev;
	struct idle_inject_device *ii_dev;
	char *name;
	int ret;

	if (cpumask_test_cpu(cpu, &cpuidle_cpu_mask))
		return 0;

	idle_cdev = kzalloc(sizeof(*idle_cdev), GFP_KERNEL);
	if (!idle_cdev) {
		ret = -ENOMEM;
		goto out;
	}

	ii_dev = idle_inject_register((struct cpumask *)cpumask_of(cpu));
	if (!ii_dev) {
		pr_err("idle_inject_register failed for cpu:%d\n", cpu);
		ret = -EINVAL;
		goto out_kfree;
	}

	idle_inject_set_duration(ii_dev, TICK_USEC, idle_duration_us);
	idle_inject_set_latency(ii_dev, idle_latency_us);

	idle_cdev->ii_dev = ii_dev;

	name = kasprintf(GFP_KERNEL, "idle-%d", cpu);
	if (!name) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	cdev = thermal_cooling_device_register(name, idle_cdev,
					       cpuidle_cooling_get_ops());
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		goto out_kfree_name;
	}

	pr_debug("%s: Idle injection set with idle duration=%u, latency=%u\n",
		 name, idle_duration_us, idle_latency_us);

	kfree(name);

	cooling_dev->cdev = cdev;
	cooling_dev->ii_dev = ii_dev;
	cooling_dev->idle_cdev = idle_cdev;
	cpumask_set_cpu(cpu, &cpuidle_cpu_mask);

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

static void cpuidle_cooling_unregister(int cpu)
{
	struct cpuidle_cooling *cooling_dev = &per_cpu(cooling_devs, cpu);

	thermal_cooling_device_unregister(cooling_dev->cdev);
	idle_inject_unregister(cooling_dev->ii_dev);
	kfree(cooling_dev->idle_cdev);
}

static int cpuidle_cooling_cpu_online(unsigned int cpu)
{
	cpuidle_cooling_register(cpu);

	return 0;
}

static enum cpuhp_state cpuidle_cooling_hp_state __read_mostly;

static int __init cpuidle_cooling_init(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"thermal/cpuidle_cooling:online",
				cpuidle_cooling_cpu_online, NULL);
	if (ret < 0)
		return ret;

	cpuidle_cooling_hp_state = ret;

	return 0;
}
module_init(cpuidle_cooling_init)

static void __exit cpuidle_cooling_exit(void)
{
	int i;

	cpuhp_remove_state(cpuidle_cooling_hp_state);

	for_each_cpu(i,	&cpuidle_cpu_mask) {
		cpuidle_cooling_unregister(i);
	}
}
module_exit(cpuidle_cooling_exit)

MODULE_LICENSE("GPL v2");
