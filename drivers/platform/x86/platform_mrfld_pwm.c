/*
 * Intel Merrifield PWM platform data initilization file
 *
 * Copyright (C) 2016, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/machine.h>

static unsigned long pwm_config[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_DISABLE, 0),
};

static const struct pinctrl_map pwm_mapping[] = {
	PIN_MAP_MUX_GROUP_DEFAULT("0000:00:17.0",  "INTC1002:00", "pwm0_grp", "pwm0"),
	PIN_MAP_MUX_GROUP_DEFAULT("0000:00:17.0",  "INTC1002:00", "pwm1_grp", "pwm1"),
	PIN_MAP_MUX_GROUP_DEFAULT("0000:00:17.0",  "INTC1002:00", "pwm2_grp", "pwm2"),
	PIN_MAP_MUX_GROUP_DEFAULT("0000:00:17.0",  "INTC1002:00", "pwm3_grp", "pwm3"),
	PIN_MAP_CONFIGS_PIN_DEFAULT("0000:00:17.0", "INTC1002:00", "GP12_PWM0", pwm_config),
	PIN_MAP_CONFIGS_PIN_DEFAULT("0000:00:17.0", "INTC1002:00", "GP13_PWM1", pwm_config),
	PIN_MAP_CONFIGS_PIN_DEFAULT("0000:00:17.0", "INTC1002:00", "GP182_PWM2", pwm_config),
	PIN_MAP_CONFIGS_PIN_DEFAULT("0000:00:17.0", "INTC1002:00", "GP183_PWM3", pwm_config),
};

static int __init mrfld_pwm_init(void)
{
	return pinctrl_register_mappings(pwm_mapping, ARRAY_SIZE(pwm_mapping));
}
postcore_initcall(mrfld_pwm_init);
