// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/ioremap.c
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * Hacked for ARM by Phil Blundell <philb@gnu.org>
 * Hacked to allow all architectures to build, and various cleanups
 * by Russell King
 * Copyright (C) 2012 ARM Ltd.
 */

#define pr_fmt(fmt)	"ioremap: " fmt

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>

#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/hypervisor.h>

static DEFINE_STATIC_KEY_FALSE(ioremap_guard_key);

void ioremap_page_range_hook(unsigned long addr, unsigned long end,
			     phys_addr_t phys_addr, pgprot_t prot)
{
	size_t size = end - addr;

	if (!static_branch_unlikely(&ioremap_guard_key))
		return;

	if (pfn_valid(__phys_to_pfn(phys_addr)))
		return;

	while (size) {
		struct arm_smccc_res res;

		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_MAP_FUNC_ID,
				  phys_addr, prot, &res);
		if (res.a0 != SMCCC_RET_SUCCESS) {
			pr_warn_ratelimited("Failed to register %llx\n",
					    phys_addr);
			return;
		}

		size -= PAGE_SIZE;
		phys_addr += PAGE_SIZE;
	}
}

void iounmap_page_range_hook(phys_addr_t phys_addr, size_t size)
{
	if (!static_branch_unlikely(&ioremap_guard_key))
		return;

	VM_BUG_ON(phys_addr & ~PAGE_MASK || size & ~PAGE_MASK);

	while (size) {
		struct arm_smccc_res res;

		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_UNMAP_FUNC_ID,
				  phys_addr, &res);
		if (res.a0 != SMCCC_RET_SUCCESS) {
			pr_warn_ratelimited("Failed to unregister %llx\n",
					    phys_addr);
			return;
		}

		size -= PAGE_SIZE;
		phys_addr += PAGE_SIZE;
	}
}

static void __iomem *__ioremap_caller(phys_addr_t phys_addr, size_t size,
				      pgprot_t prot, void *caller)
{
	unsigned long last_addr;
	unsigned long offset = phys_addr & ~PAGE_MASK;
	int err;
	unsigned long addr;
	struct vm_struct *area;

	/*
	 * Page align the mapping address and size, taking account of any
	 * offset.
	 */
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(size + offset);

	/*
	 * Don't allow wraparound, zero size or outside PHYS_MASK.
	 */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr || (last_addr & ~PHYS_MASK))
		return NULL;

	/*
	 * Don't allow RAM to be mapped.
	 */
	if (WARN_ON(pfn_is_map_memory(__phys_to_pfn(phys_addr))))
		return NULL;

	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;
	addr = (unsigned long)area->addr;
	area->phys_addr = phys_addr;

	err = ioremap_page_range(addr, addr + size, phys_addr, prot);
	if (err) {
		vunmap((void *)addr);
		return NULL;
	}

	return (void __iomem *)(offset + addr);
}

void __iomem *__ioremap(phys_addr_t phys_addr, size_t size, pgprot_t prot)
{
	return __ioremap_caller(phys_addr, size, prot,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(__ioremap);

void iounmap(volatile void __iomem *io_addr)
{
	unsigned long addr = (unsigned long)io_addr & PAGE_MASK;

	/*
	 * We could get an address outside vmalloc range in case
	 * of ioremap_cache() reusing a RAM mapping.
	 */
	if (is_vmalloc_addr((void *)addr))
		vunmap((void *)addr);
}
EXPORT_SYMBOL(iounmap);

void __iomem *ioremap_cache(phys_addr_t phys_addr, size_t size)
{
	/* For normal memory we already have a cacheable mapping. */
	if (pfn_is_map_memory(__phys_to_pfn(phys_addr)))
		return (void __iomem *)__phys_to_virt(phys_addr);

	return __ioremap_caller(phys_addr, size, __pgprot(PROT_NORMAL),
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

/*
 * Must be called after early_fixmap_init
 */
void __init early_ioremap_init(void)
{
	early_ioremap_setup();
}
