// SPDX-License-Identifier: GPL-2.0
/*
 * Primary to Sideband (P2SB) bridge access support
 *
 * Copyright (c) 2017, 2021-2022 Intel Corporation.
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Jonathan Yong <jonathan.yong@intel.com>
 */

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/p2sb.h>
#include <linux/printk.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#define p2sb_printk(level, bus, devfn, fmt, arg...) \
	printk(level "pci %04x:%02x:%02x.%d: " fmt, \
	       pci_domain_nr(bus), (bus)->number, PCI_SLOT(devfn), PCI_FUNC(devfn), ##arg)

#define p2sb_err(bus, devfn, fmt, arg...)	p2sb_printk(KERN_ERR, (bus), devfn, fmt, ##arg)
#define p2sb_info(bus, devfn, fmt, arg...)	p2sb_printk(KERN_INFO, (bus), devfn, fmt, ##arg)

#define P2SBC			0xe0
#define P2SBC_HIDE		BIT(8)

static const struct x86_cpu_id p2sb_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT,	PCI_DEVFN(13, 0)),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_GOLDMONT_D,	PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT_D,	PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE,		PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L,		PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE,		PCI_DEVFN(31, 1)),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L,		PCI_DEVFN(31, 1)),
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

/*
 * Here is practically a copy'n'paste of __pci_read_base() and Co,
 * modified to work with PCI bus and devfn instead of PCI device.
 *
 * Note, the PCI core doesn't want to have that being refactored
 * and reused.
 */
static u64 pci_size(u64 base, u64 maxbase, u64 mask)
{
	u64 size = mask & maxbase;	/* Find the significant bits */
	if (!size)
		return 0;

	/*
	 * Get the lowest of them to find the decode size, and from that
	 * the extent.
	 */
	size = size & ~(size-1);

	/*
	 * base == maxbase can be valid only if the BAR has already been
	 * programmed with all 1s.
	 */
	if (base == maxbase && ((base | (size - 1)) & mask) != mask)
		return 0;

	return size;
}

static inline unsigned long decode_bar(u32 bar)
{
	u32 mem_type;
	unsigned long flags;

	if ((bar & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO) {
		flags = bar & ~PCI_BASE_ADDRESS_IO_MASK;
		flags |= IORESOURCE_IO;
		return flags;
	}

	flags = bar & ~PCI_BASE_ADDRESS_MEM_MASK;
	flags |= IORESOURCE_MEM;
	if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
		flags |= IORESOURCE_PREFETCH;

	mem_type = bar & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
	switch (mem_type) {
	case PCI_BASE_ADDRESS_MEM_TYPE_32:
		break;
	case PCI_BASE_ADDRESS_MEM_TYPE_1M:
		/* 1M mem BAR treated as 32-bit BAR */
		break;
	case PCI_BASE_ADDRESS_MEM_TYPE_64:
		flags |= IORESOURCE_MEM_64;
		break;
	default:
		/* mem unknown type treated as 32-bit BAR */
		break;
	}
	return flags;
}

/**
 * __pci_bus_read_base - Read a PCI BAR
 * @bus: the PCI bus
 * @devfn: the PCI device and function
 * @res: resource buffer to be filled in
 * @pos: BAR position in the config space
 *
 * Returns 1 if the BAR is 64-bit, or 0 if 32-bit.
 * In case of error resulting @res->flags is 0.
 */
static int __pci_bus_read_base(struct pci_bus *bus, unsigned int devfn,
			       struct resource *res, unsigned int pos)
{
	u32 l = 0, sz = 0, mask = ~0;
	u64 l64, sz64, mask64;
	struct pci_bus_region region, inverted_region;

	pci_bus_read_config_dword(bus, devfn, pos, &l);
	pci_bus_write_config_dword(bus, devfn, pos, l | mask);
	pci_bus_read_config_dword(bus, devfn, pos, &sz);
	pci_bus_write_config_dword(bus, devfn, pos, l);

	/*
	 * All bits set in sz means the device isn't working properly.
	 * If the BAR isn't implemented, all bits must be 0.  If it's a
	 * memory BAR or a ROM, bit 0 must be clear; if it's an io BAR, bit
	 * 1 must be clear.
	 */
	if (PCI_POSSIBLE_ERROR(sz))
		sz = 0;

	/*
	 * I don't know how l can have all bits set.  Copied from old code.
	 * Maybe it fixes a bug on some ancient platform.
	 */
	if (PCI_POSSIBLE_ERROR(l))
		l = 0;

	res->flags = decode_bar(l);
	res->flags |= IORESOURCE_SIZEALIGN;
	if (res->flags & IORESOURCE_IO) {
		l64 = l & PCI_BASE_ADDRESS_IO_MASK;
		sz64 = sz & PCI_BASE_ADDRESS_IO_MASK;
		mask64 = PCI_BASE_ADDRESS_IO_MASK & (u32)IO_SPACE_LIMIT;
	} else {
		l64 = l & PCI_BASE_ADDRESS_MEM_MASK;
		sz64 = sz & PCI_BASE_ADDRESS_MEM_MASK;
		mask64 = (u32)PCI_BASE_ADDRESS_MEM_MASK;
	}

	if (res->flags & IORESOURCE_MEM_64) {
		pci_bus_read_config_dword(bus, devfn, pos + 4, &l);
		pci_bus_write_config_dword(bus, devfn, pos + 4, ~0);
		pci_bus_read_config_dword(bus, devfn, pos + 4, &sz);
		pci_bus_write_config_dword(bus, devfn, pos + 4, l);

		l64 |= ((u64)l << 32);
		sz64 |= ((u64)sz << 32);
		mask64 |= ((u64)~0 << 32);
	}

	if (!sz64)
		goto fail;

	sz64 = pci_size(l64, sz64, mask64);
	if (!sz64) {
		p2sb_info(bus, devfn, FW_BUG "reg 0x%x: invalid BAR (can't size)\n", pos);
		goto fail;
	}

	if (res->flags & IORESOURCE_MEM_64) {
		if ((sizeof(pci_bus_addr_t) < 8 || sizeof(resource_size_t) < 8)
		    && sz64 > 0x100000000ULL) {
			res->flags |= IORESOURCE_UNSET | IORESOURCE_DISABLED;
			res->start = 0;
			res->end = 0;
			p2sb_err(bus, devfn,
				 "reg 0x%x: can't handle BAR larger than 4GB (size %#010llx)\n",
				 pos, (unsigned long long)sz64);
			goto out;
		}

		if ((sizeof(pci_bus_addr_t) < 8) && l) {
			/* Above 32-bit boundary; try to reallocate */
			res->flags |= IORESOURCE_UNSET;
			res->start = 0;
			res->end = sz64 - 1;
			p2sb_info(bus, devfn,
				  "reg 0x%x: can't handle BAR above 4GB (bus address %#010llx)\n",
				  pos, (unsigned long long)l64);
			goto out;
		}
	}

	region.start = l64;
	region.end = l64 + sz64 - 1;

	pcibios_bus_to_resource(bus, res, &region);
	pcibios_resource_to_bus(bus, &inverted_region, res);

	/*
	 * If "A" is a BAR value (a bus address), "bus_to_resource(A)" is
	 * the corresponding resource address (the physical address used by
	 * the CPU.  Converting that resource address back to a bus address
	 * should yield the original BAR value:
	 *
	 *     resource_to_bus(bus_to_resource(A)) == A
	 *
	 * If it doesn't, CPU accesses to "bus_to_resource(A)" will not
	 * be claimed by the device.
	 */
	if (inverted_region.start != region.start) {
		res->flags |= IORESOURCE_UNSET;
		res->start = 0;
		res->end = region.end - region.start;
		p2sb_info(bus, devfn, "reg 0x%x: initial BAR value %#010llx invalid\n",
			  pos, (unsigned long long)region.start);
	}

	goto out;

fail:
	res->flags = 0;
out:
	if (res->flags)
		p2sb_info(bus, devfn, "reg 0x%x: %pR\n", pos, res);

	return (res->flags & IORESOURCE_MEM_64) ? 1 : 0;
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
	u32 value = P2SBC_HIDE;
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

	/* Unhide the P2SB device, if needed */
	pci_bus_read_config_dword(bus, devfn_p2sb, P2SBC, &value);
	if ((value & P2SBC_HIDE) == P2SBC_HIDE)
		pci_bus_write_config_dword(bus, devfn_p2sb, P2SBC, 0);

	/* Read the first BAR of the device in question */
	__pci_bus_read_base(bus, devfn, mem, PCI_BASE_ADDRESS_0);

	/* Hide the P2SB device, if it was hidden */
	if (value & P2SBC_HIDE)
		pci_bus_write_config_dword(bus, devfn_p2sb, P2SBC, P2SBC_HIDE);

	pci_unlock_rescan_remove();

	if (mem->flags == 0) {
		p2sb_err(bus, devfn, "Can't read BAR0: %pR\n", mem);
		return -ENODEV;
	}

	p2sb_info(bus, devfn, "BAR: %pR\n", mem);
	return 0;
}
EXPORT_SYMBOL_GPL(p2sb_bar);
