// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P.*/


#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#define IOP_REGS_PHYS_BASE 0xc0000000
#define IOP_REGS_VIRT_BASE 0xf0000000
#define IOP_REGS_SIZE (240*SZ_1M)
#define RESET_CMD 0x00080002

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
	void __iomem *gxp_init_regs;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "hpe,gxp-cpu-init");
	gxp_init_regs = of_iomap(np, 0);

	/*it is necessary for our SOC to reset ECHI through this*/
	/* register due to a hardware limitation*/
	__raw_writel(RESET_CMD,
		(gxp_init_regs));

}

static void gxp_restart(enum reboot_mode mode, const char *cmd)
{
	__raw_writel(1, (void __iomem *) IOP_REGS_VIRT_BASE);
}

static const char * const gxp_board_dt_compat[] = {
	"hpe,gxp",
	NULL,
};

DT_MACHINE_START(GXP_DT, "HPE GXP")
	.init_machine	= gxp_dt_init,
	.map_io		= gxp_map_io,
	.restart	= gxp_restart,
	.dt_compat	= gxp_board_dt_compat,
MACHINE_END
