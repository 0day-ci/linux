// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P1010RDB Board Setup
 *
 * Copyright 2011 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc85xx.h"

void __init p1010_rdb_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
	  MPIC_SINGLE_DEST_CPU,
	  0, 256, " OpenPIC  ");

	BUG_ON(mpic == NULL);

	mpic_init(mpic);
}


/*
 * Setup the architecture
 */
static void __init p1010_rdb_setup_arch(void)
{
	ppc_md_call_cond(progress)("p1010_rdb_setup_arch()", 0);

	fsl_pci_assign_primary();

	printk(KERN_INFO "P1010 RDB board from Freescale Semiconductor\n");
}

machine_arch_initcall(p1010_rdb, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init p1010_rdb_probe(void)
{
	if (!of_machine_is_compatible("fsl,P1010RDB") &&
	    !of_machine_is_compatible("fsl,P1010RDB-PB"))
		return 0;

	ppc_md_update(setup_arch, p1010_rdb_setup_arch);
	ppc_md_update(init_IRQ, p1010_rdb_pic_init);
#ifdef CONFIG_PCI
	ppc_md_update(pcibios_fixup_bus, fsl_pcibios_fixup_bus);
	ppc_md_update(pcibios_fixup_phb, fsl_pcibios_fixup_phb);
#endif
	ppc_md_update(get_irq, mpic_get_irq);
	ppc_md_update(calibrate_decr, generic_calibrate_decr);
	ppc_md_update(progress, udbg_progress);

	return 1;
}

define_machine(p1010_rdb) {
	.name			= "P1010 RDB",
	.probe			= p1010_rdb_probe,
};
