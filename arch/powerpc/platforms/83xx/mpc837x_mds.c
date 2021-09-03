// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/platforms/83xx/mpc837x_mds.c
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * MPC837x MDS board specific routines
 */

#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <asm/prom.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

#define BCSR12_USB_SER_MASK	0x8a
#define BCSR12_USB_SER_PIN	0x80
#define BCSR12_USB_SER_DEVICE	0x02

static int mpc837xmds_usb_cfg(void)
{
	struct device_node *np;
	const void *phy_type, *mode;
	void __iomem *bcsr_regs = NULL;
	u8 bcsr12;
	int ret;

	ret = mpc837x_usb_cfg();
	if (ret)
		return ret;
	/* Map BCSR area */
	np = of_find_compatible_node(NULL, NULL, "fsl,mpc837xmds-bcsr");
	if (np) {
		bcsr_regs = of_iomap(np, 0);
		of_node_put(np);
	}
	if (!bcsr_regs)
		return -1;

	np = of_find_node_by_name(NULL, "usb");
	if (!np) {
		ret = -ENODEV;
		goto out;
	}
	phy_type = of_get_property(np, "phy_type", NULL);
	if (phy_type && !strcmp(phy_type, "ulpi")) {
		clrbits8(bcsr_regs + 12, BCSR12_USB_SER_PIN);
	} else if (phy_type && !strcmp(phy_type, "serial")) {
		mode = of_get_property(np, "dr_mode", NULL);
		bcsr12 = in_8(bcsr_regs + 12) & ~BCSR12_USB_SER_MASK;
		bcsr12 |= BCSR12_USB_SER_PIN;
		if (mode && !strcmp(mode, "peripheral"))
			bcsr12 |= BCSR12_USB_SER_DEVICE;
		out_8(bcsr_regs + 12, bcsr12);
	} else {
		printk(KERN_ERR "USB DR: unsupported PHY\n");
	}

	of_node_put(np);
out:
	iounmap(bcsr_regs);
	return ret;
}

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc837x_mds_setup_arch(void)
{
	mpc83xx_setup_arch();
	mpc837xmds_usb_cfg();
}

machine_device_initcall(mpc837x_mds, mpc83xx_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc837x_mds_probe(void)
{
	if (!of_machine_is_compatible("fsl,mpc837xmds"))
		return 0;

	ppc_md_update(setup_arch, mpc837x_mds_setup_arch);
	ppc_md_update(discover_phbs, mpc83xx_setup_pci);
	ppc_md_update(init_IRQ, mpc83xx_ipic_init_IRQ);
	ppc_md_update(get_irq, ipic_get_irq);
	ppc_md_update(restart, mpc83xx_restart);
	ppc_md_update(time_init, mpc83xx_time_init);
	ppc_md_update(calibrate_decr, generic_calibrate_decr);
	ppc_md_update(progress, udbg_progress);

	return 1;
}

define_machine(mpc837x_mds) {
	.name			= "MPC837x MDS",
	.probe			= mpc837x_mds_probe,
};
