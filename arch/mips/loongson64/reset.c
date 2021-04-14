// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Zhangjin Wu, wuzhangjin@gmail.com
 */
#include <linux/init.h>
#include <linux/pm.h>

#include <asm/idle.h>
#include <asm/reboot.h>

#include <loongson.h>
#include <boot_param.h>

static void loongson_restart(char *command)
{

	if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64R) {
		unsigned long base;

		base = CKSEG1ADDR(LOONGSON_REG_BASE) + ACPI_OFF;
		writel(1, (void *)(base + RST_CNT));
	} else {
		void (*fw_restart)(void) = (void *)loongson_sysconf.restart_addr;

		fw_restart();
	}
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void loongson_poweroff(void)
{

	if ((read_c0_prid() & PRID_IMP_MASK) == PRID_IMP_LOONGSON_64R) {
		unsigned long base;
		unsigned int acpi_ctrl;

		base = CKSEG1ADDR(LOONGSON_REG_BASE) + ACPI_OFF;
		acpi_ctrl = readl((void *)(base + PM1_STS));
		acpi_ctrl &= 0xffffffff;
		writel(acpi_ctrl, (void *)(base + PM1_STS));
		acpi_ctrl = SLP_EN | SLP_TYP;
		writel(acpi_ctrl, (void *)(base + PM1_CNT));
	} else {
		void (*fw_poweroff)(void) = (void *)loongson_sysconf.poweroff_addr;

		fw_poweroff();
	}
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void loongson_halt(void)
{
	pr_notice("\n\n** You can safely turn off the power now **\n\n");
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static int __init mips_reboot_setup(void)
{
	_machine_restart = loongson_restart;
	_machine_halt = loongson_halt;
	pm_power_off = loongson_poweroff;

	return 0;
}

arch_initcall(mips_reboot_setup);
