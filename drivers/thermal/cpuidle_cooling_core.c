// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Linaro Limited.
 *
 *  Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 */
#define pr_fmt(fmt) "cpuidle cooling: " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/idle_inject.h>
#include <linux/thermal.h>

#include "cpuidle_cooling_core.h"

/**
 * cpuidle_cooling_runtime - Running time computation
 * @idle_duration_us: CPU idle time to inject in microseconds
 * @state: a percentile based number
 *
 * The running duration is computed from the idle injection duration
 * which is fixed. If we reach 100% of idle injection ratio, that
 * means the running duration is zero. If we have a 50% ratio
 * injection, that means we have equal duration for idle and for
 * running duration.
 *
 * The formula is deduced as follows:
 *
 *  running = idle x ((100 / ratio) - 1)
 *
 * For precision purpose for integer math, we use the following:
 *
 *  running = (idle x 100) / ratio - idle
 *
 * For example, if we have an injected duration of 50%, then we end up
 * with 10ms of idle injection and 10ms of running duration.
 *
 * Return: An unsigned int for a usec based runtime duration.
 */
static unsigned int cpuidle_cooling_runtime(unsigned int idle_duration_us,
					    unsigned long state)
{
	if (!state)
		return 0;

	return ((idle_duration_us * 100) / state) - idle_duration_us;
}

/**
 * cpuidle_cooling_get_max_state - Get the maximum state
 * @cdev  : the thermal cooling device
 * @state : a pointer to the state variable to be filled
 *
 * The function always returns 100 as the injection ratio. It is
 * percentile based for consistency across different platforms.
 *
 * Return: The function can not fail, it is always zero
 */
static int cpuidle_cooling_get_max_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	/*
	 * Depending on the configuration or the hardware, the running
	 * cycle and the idle cycle could be different. We want to
	 * unify that to an 0..100 interval, so the set state
	 * interface will be the same whatever the platform is.
	 *
	 * The state 100% will make the cluster 100% ... idle. A 0%
	 * injection ratio means no idle injection at all and 50%
	 * means for 10ms of idle injection, we have 10ms of running
	 * time.
	 */
	*state = 100;

	return 0;
}

/**
 * cpuidle_cooling_get_cur_state - Get the current cooling state
 * @cdev: the thermal cooling device
 * @state: a pointer to the state
 *
 * The function just copies  the state value from the private thermal
 * cooling device structure, the mapping is 1 <-> 1.
 *
 * Return: The function can not fail, it is always zero
 */
static int cpuidle_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct cpuidle_cooling_device *idle_cdev = cdev->devdata;

	*state = idle_cdev->state;

	return 0;
}

/**
 * cpuidle_cooling_set_cur_state - Set the current cooling state
 * @cdev: the thermal cooling device
 * @state: the target state
 *
 * The function checks first if we are initiating the mitigation which
 * in turn wakes up all the idle injection tasks belonging to the idle
 * cooling device. In any case, it updates the internal state for the
 * cooling device.
 *
 * Return: The function can not fail, it is always zero
 */
static int cpuidle_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long state)
{
	struct cpuidle_cooling_device *idle_cdev = cdev->devdata;
	struct idle_inject_device *ii_dev = idle_cdev->ii_dev;
	unsigned long current_state = idle_cdev->state;
	unsigned int runtime_us, idle_duration_us;

	idle_cdev->state = state;

	idle_inject_get_duration(ii_dev, &runtime_us, &idle_duration_us);

	runtime_us = cpuidle_cooling_runtime(idle_duration_us, state);

	idle_inject_set_duration(ii_dev, runtime_us, idle_duration_us);

	if (current_state == 0 && state > 0)
		idle_inject_start(ii_dev);
	else if (current_state > 0 && !state)
		idle_inject_stop(ii_dev);

	return 0;
}

/**
 * cpuidle_cooling_ops - thermal cooling device ops
 */
static struct thermal_cooling_device_ops cpuidle_cooling_ops = {
	.get_max_state = cpuidle_cooling_get_max_state,
	.get_cur_state = cpuidle_cooling_get_cur_state,
	.set_cur_state = cpuidle_cooling_set_cur_state,
};

struct thermal_cooling_device_ops *cpuidle_cooling_get_ops(void)
{
	return &cpuidle_cooling_ops;
}
EXPORT_SYMBOL_GPL(cpuidle_cooling_get_ops);
