// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nintendo Wii U board-specific support
 *
 * Copyright (C) 2022 The linux-wiiu Team
 */
#define DRV_MODULE_NAME "wiiu"
#define pr_fmt(fmt) DRV_MODULE_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/udbg.h>

#include "espresso-pic.h"
#include "latte-pic.h"
#include "udbg_latteipc.h"

static int __init wiiu_probe(void)
{
	if (!of_machine_is_compatible("nintendo,wiiu"))
		return 0;

	latteipc_udbg_init();

	return 1;
}

static void __noreturn wiiu_halt(void)
{
	for (;;)
		cpu_relax();
}

static void __init wiiu_init_irq(void)
{
	espresso_pic_init();
	latte_pic_init();
}

static const struct of_device_id wiiu_of_bus[] = {
	{
		.compatible = "nintendo,latte",
	},
	{},
};

static int __init wiiu_device_probe(void)
{
	if (!machine_is(wiiu))
		return 0;

	of_platform_populate(NULL, wiiu_of_bus, NULL, NULL);
	return 0;
}
device_initcall(wiiu_device_probe);

define_machine(wiiu) {
	.name = "wiiu",
	.probe = wiiu_probe,
	.halt = wiiu_halt,
	.progress = udbg_progress,
	.calibrate_decr = generic_calibrate_decr,
	.init_IRQ = wiiu_init_irq,
	.get_irq = espresso_pic_get_irq,
};
