// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/init.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>

#define IOP_REGS_PHYS_BASE 0xc0000000
#define IOP_REGS_VIRT_BASE 0xf0000000
#define IOP_REGS_SIZE (240*SZ_1M)

#define IOP_EHCI_USBCMD 0x0efe0010

static struct map_desc gxp_io_desc[] __initdata = {
	{
	.virtual	= (unsigned long)IOP_REGS_VIRT_BASE,
	.pfn		= __phys_to_pfn(IOP_REGS_PHYS_BASE),
	.length		= IOP_REGS_SIZE,
	.type		= MT_DEVICE,
	},
};

void __init gxp_map_io(void)
{
	iotable_init(gxp_io_desc, ARRAY_SIZE(gxp_io_desc));
}

static void __init gxp_dt_init(void)
{
	/*reset EHCI host controller for clear start*/
	__raw_writel(0x00080002,
		(void __iomem *)(IOP_REGS_VIRT_BASE + IOP_EHCI_USBCMD));
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static void gxp_restart(enum reboot_mode mode, const char *cmd)
{
	__raw_writel(1, (void __iomem *) IOP_REGS_VIRT_BASE);
}

static const char * const gxp_board_dt_compat[] = {
	"HPE,GXP",
	NULL,
};

DT_MACHINE_START(GXP_DT, "HPE GXP")
	.init_machine	= gxp_dt_init,
	.map_io		= gxp_map_io,
	.restart	= gxp_restart,
	.dt_compat	= gxp_board_dt_compat,
MACHINE_END
