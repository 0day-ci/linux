/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2019 Linaro Limited.
 *
 *  Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 */

 #ifndef _CPUIDLE_COOLING_CORE_H
 #define _CPUIDLE_COOLING_CORE_H

/**
 * struct cpuidle_cooling_device - data for the idle cooling device
 * @ii_dev: an atomic to keep track of the last task exiting the idle cycle
 * @state: a normalized integer giving the state of the cooling device
 */
struct cpuidle_cooling_device {
	struct idle_inject_device *ii_dev;
	unsigned long state;
};

struct thermal_cooling_device_ops *cpuidle_cooling_get_ops(void);

#endif
