// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Huawei Ltd.
 * Author: Bixuan Cui <cuibixuan@huawei.com>
 */
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/jump_label.h>

static void __iomem *__ioremap(phys_addr_t addr, size_t size, pgprot_t prot)
{
	unsigned long offset, vaddr;
	struct vm_struct *area;
	phys_addr_t last_addr;

	last_addr = addr + size - 1;
	if (!size || last_addr < addr)
		return NULL;

	offset = addr & ~PAGE_MASK;
	addr &= PAGE_MASK;
	size = PAGE_ALIGN(size + offset);
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;

	vaddr = (unsigned long) area->addr;
	if (ioremap_page_range(vaddr, vaddr + size, addr, prot)) {
		free_vm_area(area);
		return NULL;
	}
	return (void __iomem *) ((unsigned long) area->addr + offset);
}

void __iomem *ioremap_prot(phys_addr_t addr, size_t size, unsigned long prot)
{
	return __ioremap(addr, size, __pgprot(prot));
}
EXPORT_SYMBOL(ioremap_prot);

void __iomem *ioremap(phys_addr_t addr, size_t size)
{
	return __ioremap(addr, size, PAGE_KERNEL);
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_wc(phys_addr_t addr, size_t size)
{
	return __ioremap(addr, size, pgprot_writecombine(PAGE_KERNEL));
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *ioremap_wt(phys_addr_t addr, size_t size)
{
	return __ioremap(addr, size, pgprot_writethrough(PAGE_KERNEL));
}
EXPORT_SYMBOL(ioremap_wt);

void iounmap(volatile void __iomem *addr)
{
	vunmap((__force void *) ((unsigned long) addr & PAGE_MASK));
}
EXPORT_SYMBOL(iounmap);
