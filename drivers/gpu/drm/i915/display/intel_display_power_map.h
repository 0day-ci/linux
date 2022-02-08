/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_POWER_MAP_H__
#define __INTEL_DISPLAY_POWER_MAP_H__

struct i915_power_domains;

int intel_init_power_wells(struct i915_power_domains *power_domains);
void intel_cleanup_power_wells(struct i915_power_domains *power_domains);

#endif
