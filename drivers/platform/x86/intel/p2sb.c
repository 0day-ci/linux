// SPDX-License-Identifier: GPL-2.0
/*
 * Primary to Sideband (P2SB) bridge access support
 *
 * Copyright (c) 2017, 2021 Intel Corporation.
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Jonathan Yong <jonathan.yong@intel.com>
 */

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/p2sb.h>

/* For __pci_bus_read_base(), which is available for the PCI subsystem */
#include <../../../pci/pci.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define P2SBC_HIDE_BYTE			0xe1
#define P2SBC_HIDE_BIT			BIT(0)

static const struct x86_cpu_id p2sb_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT,	PCI_DEVFN(13, 0)),
	{}
};

static int p2sb_get_devfn(unsigned int *devfn)
{
	const struct x86_cpu_id *id;

	id = x86_match_cpu(p2sb_cpu_ids);
	if (!id)
		return -ENODEV;

	*devfn = (unsigned int)id->driver_data;
	return 0;
}

/**
 * p2sb_bar - Get Primary to Sideband (P2SB) bridge device BAR
 * @bus: PCI bus to communicate with
 * @devfn: PCI slot and function to communicate with
 * @mem: memory resource to be filled in
 *
 * The BIOS prevents the P2SB device from being enumerated by the PCI
 * subsystem, so we need to unhide and hide it back to lookup the BAR.
 *
 * if @bus is NULL, the bus 0 in domain 0 will be in use.
 * If @devfn is 0, it will be replaced by devfn of the P2SB device.
 *
 * Caller must provide a valid pointer to @mem.
 *
 * Locking is handled by pci_rescan_remove_lock mutex.
 *
 * Return:
 * 0 on success or appropriate errno value on error.
 */
int p2sb_bar(struct pci_bus *bus, unsigned int devfn, struct resource *mem)
{
	unsigned int devfn_p2sb;
	int ret;

	/* Get devfn for P2SB device itself */
	ret = p2sb_get_devfn(&devfn_p2sb);
	if (ret)
		return ret;

	/* if @pdev is NULL, use bus 0 in domain 0 */
	bus = bus ?: pci_find_bus(0, 0);

	/* If @devfn is 0, replace it with devfn of P2SB device itself */
	devfn = devfn ?: devfn_p2sb;

	pci_lock_rescan_remove();

	/* Unhide the P2SB device */
	pci_bus_write_config_byte(bus, devfn_p2sb, P2SBC_HIDE_BYTE, 0);

	/* Read the first BAR of the device in question */
	__pci_bus_read_base(bus, devfn, pci_bar_unknown, mem, PCI_BASE_ADDRESS_0, true);

	/* Hide the P2SB device */
	pci_bus_write_config_byte(bus, devfn_p2sb, P2SBC_HIDE_BYTE, P2SBC_HIDE_BIT);

	pci_unlock_rescan_remove();

	pci_bus_info(bus, devfn, "BAR: %pR\n", mem);
	return 0;
}
EXPORT_SYMBOL_GPL(p2sb_bar);
