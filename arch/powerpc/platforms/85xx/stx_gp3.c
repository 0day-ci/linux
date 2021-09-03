// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based on MPC8560 ADS and arch/ppc stx_gp3 ports
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2008 Freescale Semiconductor Inc.
 *
 * Dan Malek <dan@embeddededge.com>
 * Copyright 2004 Embedded Edge, LLC
 *
 * Copied from mpc8560_ads.c
 * Copyright 2002, 2003 Motorola Inc.
 *
 * Ported to 2.6, Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2004-2005 MontaVista Software, Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpic.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc85xx.h"

#ifdef CONFIG_CPM2
#include <asm/cpm2.h>
#endif /* CONFIG_CPM2 */

static void __init stx_gp3_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);

	mpc85xx_cpm2_pic_init();
}

/*
 * Setup the architecture
 */
static void __init stx_gp3_setup_arch(void)
{
	ppc_md_call_cond(progress)("stx_gp3_setup_arch()", 0);

	fsl_pci_assign_primary();

#ifdef CONFIG_CPM2
	cpm2_reset();
#endif
}

static void stx_gp3_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: RPC Electronics STx\n");
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));
}

machine_arch_initcall(stx_gp3, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init stx_gp3_probe(void)
{
	if (!of_machine_is_compatible("stx,gp3-8560"))
		return 0;

	ppc_md_update(setup_arch, stx_gp3_setup_arch);
	ppc_md_update(init_IRQ, stx_gp3_pic_init);
	ppc_md_update(show_cpuinfo, stx_gp3_show_cpuinfo);
	ppc_md_update(get_irq, mpic_get_irq);
	ppc_md_update(calibrate_decr, generic_calibrate_decr);
	ppc_md_update(progress, udbg_progress);

	return 1;
}

define_machine(stx_gp3) {
	.name			= "STX GP3",
	.probe			= stx_gp3_probe,
};
