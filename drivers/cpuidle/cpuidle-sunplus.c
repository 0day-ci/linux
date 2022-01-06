// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014,2015, Linaro Ltd.
 *
 * SAW power controller driver
 */
#define pr_fmt(fmt) "CPUidle arm: " fmt

#include <asm/suspend.h>
#include <linux/cpu_cooling.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_data/cpuidle-sunplus.h>

#include "dt_idle_states.h"

static int sp7021_wfi_finisher(unsigned long flags)
{
	cpu_v7_do_idle();   /* idle to WFI */

	return -1;
}

static int sp7021_enter_idle_state(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	int ret;

  /* if idx=0, call cpu_do_idle() */
	if (!idx) {
		cpu_v7_do_idle();
		return idx;
	}

	/* if idx>0, call cpu_suspend() */
	ret = cpu_pm_enter();
	if (!ret) {
	/*
	 * Pass idle state index to cpuidle_suspend which in turn
	 * will call the CPU ops suspend protocol with idle index as a
	 * parameter.
	 */
		ret = cpu_suspend(idx, sp7021_wfi_finisher);
	}
	cpu_pm_exit();

	return ret ? -1:idx;
}

static struct cpuidle_driver sp7021_idle_driver = {
	.name = "sp7021_idle",
	.owner = THIS_MODULE,
	/*
	 * State at index 0 is standby wfi and considered standard
	 * on all ARM platforms. If in some platforms simple wfi
	 * can't be used as "state 0", DT bindings must be implemented
	 * to work around this issue and allow installing a special
	 * handler for idle state index 0.
	 */
	.states[0] = {
		.enter                  = sp7021_enter_idle_state,
		.exit_latency           = 1,
		.target_residency       = 1,
		.power_usage		= UINT_MAX,
		.name                   = "WFI",
		.desc                   = "ARM WFI",
	}
};

static const struct of_device_id sp7021_idle_state_match[] = {
	{ .compatible = "sunplus,sp7021-idle-state",
		.data = sp7021_enter_idle_state },
	{ },
};

/*
 * sp7021_idle_init - Initializes sp7021 cpuidle driver
 *
 * Initializes sp7021 cpuidle driver for all CPUs, if any CPU fails
 * to register cpuidle driver then rollback to cancel all CPUs
 * registration.
 */
static int __init sp7021_idle_init(void)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	drv = kmemdup(&sp7021_idle_driver, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->cpumask = (struct cpumask *)cpumask_of(cpu);
	/*
	 * Initialize idle states data, starting at index 1.  This
	 * driver is DT only, if no DT idle states are detected (ret
	 * == 0) let the driver initialization fail accordingly since
	 * there is no reason to initialize the idle driver if only
	 * wfi is supported.
	 */
	ret = dt_init_idle_driver(drv, sp7021_idle_state_match, 1);
	if (ret <= 0)
		return ret ? : -ENODEV;

	ret = cpuidle_register_driver(drv);
	if (ret) {
		pr_err("Failed to register cpuidle driver\n");
		return ret;
	}

	for_each_possible_cpu(cpu) {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev) {
			ret = -ENOMEM;
			goto out_fail;
		}
		dev->cpu = cpu;

		ret = cpuidle_register_device(dev);
		if (ret) {
			pr_err("Failed to register cpuidle device for CPU %d\n", cpu);
			kfree(dev);
			goto out_fail;
		}
	}

	return 0;

out_fail:
	while (--cpu >= 0) {
		dev = per_cpu(cpuidle_devices, cpu);
		cpuidle_unregister_device(dev);
		kfree(dev);
	}
	cpuidle_unregister_driver(drv);

	return ret;
}

static int __init idle_init(void)
{
	int ret;

	if (of_machine_is_compatible("sunplus,sp7021-achip")) {
		sp7021_idle_init();
		ret = 0;
	}	else
		ret = -1;

	if (ret) {
		pr_err("failed to cpuidle init\n");
		return ret;
	}

	return ret;
}
device_initcall(idle_init);

MODULE_AUTHOR("Edwin Chiu <edwinchiu0505tw@gmail.com>");
MODULE_DESCRIPTION("Sunplus sp7021 cpuidle driver");
MODULE_LICENSE("GPL");
