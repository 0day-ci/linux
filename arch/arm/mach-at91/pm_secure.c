// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012, Bootlin
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/parser.h>
#include <linux/string.h>
#include "generic.h"
#include "sam_secure.h"
#include "pm.h"

static int suspend_mode = AT91_PM_ULP0;

static void at91_pm_secure_init(void)
{
	struct arm_smccc_res res;

	res = sam_smccc_call(SAMA5_SMC_SIP_SET_SUSPEND_MODE, suspend_mode, 0);
	if (res.a0 == 0) {
		pr_info("AT91: Secure PM: suspend mode set to %s\n",
			pm_modes[suspend_mode].pattern);
		return;
	}

	pr_warn("AT91: Secure PM: %s mode not supported !\n",
		pm_modes[suspend_mode].pattern);

	res = sam_smccc_call(SAMA5_SMC_SIP_GET_SUSPEND_MODE, 0, 0);
	if (res.a0 == 0) {
		pr_warn("AT91: Secure PM: failed to get default mode\n");
		return;
	}
	suspend_mode = res.a1;

	pr_info("AT91: Secure PM: using default suspend mode %s\n",
		pm_modes[suspend_mode].pattern);
}

void __init sama5_pm_init(void)
{
}

void __init sama5d2_pm_init(void)
{
	at91_pm_secure_init();
}

int at91_suspend_entering_slow_clock(void)
{
	return (suspend_mode >= AT91_PM_ULP0);
}
EXPORT_SYMBOL(at91_suspend_entering_slow_clock);

static int __init at91_pm_modes_select(char *str)
{
	int dummy;

	pr_warn("AT91: Secure PM: ignoring standby mode\n");

	return at91_pm_common_modes_select(str, &dummy, &suspend_mode);
}
early_param("atmel.pm_modes", at91_pm_modes_select);
