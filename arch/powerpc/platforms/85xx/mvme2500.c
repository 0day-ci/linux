// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Board setup routines for the Emerson/Artesyn MVME2500
 *
 * Copyright 2014 Elettra-Sincrotrone Trieste S.C.p.A.
 *
 * Based on earlier code by:
 *
 *	Xianghua Xiao (x.xiao@freescale.com)
 *	Tom Armistead (tom.armistead@emerson.com)
 *	Copyright 2012 Emerson
 *
 * Author Alessio Igor Bogani <alessio.bogani@elettra.eu>
 */

#include <linux/pci.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc85xx.h"

void __init mvme2500_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0,
		  MPIC_BIG_ENDIAN | MPIC_SINGLE_DEST_CPU,
		0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);
}

/*
 * Setup the architecture
 */
static void __init mvme2500_setup_arch(void)
{
	ppc_md_call_cond(progress)("mvme2500_setup_arch()", 0);
	fsl_pci_assign_primary();
	pr_info("MVME2500 board from Artesyn\n");
}

machine_arch_initcall(mvme2500, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mvme2500_probe(void)
{
	if (!of_machine_is_compatible("artesyn,MVME2500"))
		return 0;

	ppc_md_update(setup_arch, mvme2500_setup_arch);
	ppc_md_update(init_IRQ, mvme2500_pic_init);
#ifdef CONFIG_PCI
	ppc_md_update(pcibios_fixup_bus, fsl_pcibios_fixup_bus);
	ppc_md_update(pcibios_fixup_phb, fsl_pcibios_fixup_phb);
#endif
	ppc_md_update(get_irq, mpic_get_irq);
	ppc_md_update(calibrate_decr, generic_calibrate_decr);
	ppc_md_update(progress, udbg_progress);

	return 1;
}

define_machine(mvme2500) {
	.name			= "MVME2500",
	.probe			= mvme2500_probe,
};
