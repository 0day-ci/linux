// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/platforms/83xx/mpc831x_rdb.c
 *
 * Description: MPC831x RDB board specific routines.
 * This file is based on mpc834x_sys.c
 * Author: Lo Wlison <r43300@freescale.com>
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2006. All rights reserved.
 */

#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

/*
 * Setup the architecture
 */
static void __init mpc831x_rdb_setup_arch(void)
{
	mpc83xx_setup_arch();
	mpc831x_usb_cfg();
}

static const char *board[] __initdata = {
	"MPC8313ERDB",
	"fsl,mpc8315erdb",
	NULL
};

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc831x_rdb_probe(void)
{
	if (!of_device_compatible_match(of_root, board))
		return 0;

	ppc_md_update(setup_arch, mpc831x_rdb_setup_arch);
	ppc_md_update(discover_phbs, mpc83xx_setup_pci);
	ppc_md_update(init_IRQ, mpc83xx_ipic_init_IRQ);
	ppc_md_update(get_irq, ipic_get_irq);
	ppc_md_update(restart, mpc83xx_restart);
	ppc_md_update(time_init, mpc83xx_time_init);
	ppc_md_update(calibrate_decr, generic_calibrate_decr);
	ppc_md_update(progress, udbg_progress);

	return 1;
}

machine_device_initcall(mpc831x_rdb, mpc83xx_declare_of_platform_devices);

define_machine(mpc831x_rdb) {
	.name			= "MPC831x RDB",
	.probe			= mpc831x_rdb_probe,
};
