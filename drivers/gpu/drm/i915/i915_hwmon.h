/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_HWMON_H__
#define __INTEL_HWMON_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include "i915_reg.h"

struct i915_hwmon_reg {
	i915_reg_t pkg_power_sku_unit;
	i915_reg_t pkg_power_sku;
	i915_reg_t pkg_energy_status;
	i915_reg_t pkg_rapl_limit;
	i915_reg_t pkg_rapl_limit_udw;
};

struct i915_hwmon {
	struct device *dev;
	struct mutex hwmon_lock;	/* counter overflow logic and rmw */

	struct i915_hwmon_reg rg;

	u32 energy_counter_overflow;
	u32 energy_counter_prev;
	u32 power_max_initial_value;

	int scl_shift_power;
	int scl_shift_energy;
	int scl_shift_time;
};

struct drm_i915_private;

void i915_hwmon_register(struct drm_i915_private *i915);
void i915_hwmon_unregister(struct drm_i915_private *i915);

#endif
