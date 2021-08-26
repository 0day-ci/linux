// SPDX-License-Identifier: GPL-2.0+
/*
 * ACPI quirks for Brcm2711 PCIe host controller
 * As used on the Raspberry Pi Compute Module 4
 *
 * Copyright (C) 2021 Arm Ltd.
 */

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include "../pci.h"
#include "pcie-brcmstb.h"

static int brcm_acpi_init(struct pci_config_window *cfg)
{
	/*
	 * This platform doesn't technically have anything that could be called
	 * ECAM. Its config region has root port specific registers between
	 * standard PCIe defined config registers. Thus the region setup by the
	 * generic ECAM code needs to be adjusted. The HW can access bus 0-ff
	 * but the footprint isn't a nice power of 2 (40k). For purposes of
	 * mapping the config region we are just going to squash the standard
	 * and nonstandard registers together rather than mapping them separately.
	 */
	iounmap(cfg->win);
	cfg->win = pci_remap_cfgspace(cfg->res.start, resource_size(&cfg->res));
	if (!cfg->win)
		goto err_exit;

	/* MSI is nonstandard as well */
	pci_no_msi();

	return 0;
err_exit:
	dev_err(cfg->parent, "PCI: Failed to remap config\n");
	return -ENOMEM;
}

static void __iomem *brcm_pcie_map_conf2(struct pci_bus *bus,
					unsigned int devfn, int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	void __iomem *base = cfg->win;
	int idx;
	u32 up;

	/* Accesses to the RC go right to the RC registers if slot==0 */
	if (pci_is_root_bus(bus))
		return PCI_SLOT(devfn) ? NULL : base + where;

	/*
	 * Assure the link is up before sending requests downstream. This is done
	 * to avoid sending transactions to EPs that don't exist. Link flap
	 * conditions/etc make this race more probable. The resulting unrecoverable
	 * SERRORs will result in the machine crashing.
	 */
	up = readl(base + PCIE_MISC_PCIE_STATUS);
	if (!(up & PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE_MASK))
		return NULL;

	if (!(up & PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP_MASK))
		return NULL;

	/* For devices, write to the config space index register */
	idx = PCIE_ECAM_OFFSET(bus->number, devfn, 0);
	writel(idx, base + PCIE_EXT_CFG_INDEX);
	return base + PCIE_EXT_CFG_DATA + where;
}

const struct pci_ecam_ops bcm2711_pcie_ops = {
	.init		= brcm_acpi_init,
	.bus_shift	= 1,
	.pci_ops	= {
		.map_bus	= brcm_pcie_map_conf2,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};
