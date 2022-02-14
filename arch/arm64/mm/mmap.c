// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/mmap.c
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/types.h>

#include <asm/page.h>
#include <asm/mman.h>

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This might go
 * away in the future.
 */
int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	/*
	 * Check whether addr is covered by a memory region without the
	 * MEMBLOCK_NOMAP attribute, and whether that region covers the
	 * entire range. In theory, this could lead to false negatives
	 * if the range is covered by distinct but adjacent memory regions
	 * that only differ in other attributes. However, few of such
	 * attributes have been defined, and it is debatable whether it
	 * follows that /dev/mem read() calls should be able traverse
	 * such boundaries.
	 */
	return memblock_is_region_memory(addr, size) &&
	       memblock_is_map_memory(addr);
}

/*
 * Do not allow /dev/mem mappings beyond the supported physical range.
 */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return !(((pfn << PAGE_SHIFT) + size) & ~PHYS_MASK);
}

static inline pgprot_t __vm_get_page_prot(unsigned long vm_flags)
{
	switch (vm_flags & (VM_READ | VM_WRITE | VM_EXEC | VM_SHARED)) {
	case VM_NONE:
		return PAGE_NONE;
	case VM_READ:
	case VM_WRITE:
	case VM_WRITE | VM_READ:
		return PAGE_READONLY;
	case VM_EXEC:
		return PAGE_EXECONLY;
	case VM_EXEC | VM_READ:
	case VM_EXEC | VM_WRITE:
	case VM_EXEC | VM_WRITE | VM_READ:
		return PAGE_READONLY_EXEC;
	case VM_SHARED:
		return PAGE_NONE;
	case VM_SHARED | VM_READ:
		return PAGE_READONLY;
	case VM_SHARED | VM_WRITE:
	case VM_SHARED | VM_WRITE | VM_READ:
		return PAGE_SHARED;
	case VM_SHARED | VM_EXEC:
		return PAGE_EXECONLY;
	case VM_SHARED | VM_EXEC | VM_READ:
		return PAGE_READONLY_EXEC;
	case VM_SHARED | VM_EXEC | VM_WRITE:
	case VM_SHARED | VM_EXEC | VM_WRITE | VM_READ:
		return PAGE_SHARED_EXEC;
	default:
		BUILD_BUG();
	}
}

static pgprot_t arm64_arch_filter_pgprot(pgprot_t prot)
{
	if (cpus_have_const_cap(ARM64_HAS_EPAN))
		return prot;

	if (pgprot_val(prot) != pgprot_val(PAGE_EXECONLY))
		return prot;

	return PAGE_READONLY_EXEC;
}

static pgprot_t arm64_arch_vm_get_page_prot(unsigned long vm_flags)
{
	pteval_t prot = 0;

	if (vm_flags & VM_ARM64_BTI)
		prot |= PTE_GP;

	/*
	 * There are two conditions required for returning a Normal Tagged
	 * memory type: (1) the user requested it via PROT_MTE passed to
	 * mmap() or mprotect() and (2) the corresponding vma supports MTE. We
	 * register (1) as VM_MTE in the vma->vm_flags and (2) as
	 * VM_MTE_ALLOWED. Note that the latter can only be set during the
	 * mmap() call since mprotect() does not accept MAP_* flags.
	 * Checking for VM_MTE only is sufficient since arch_validate_flags()
	 * does not permit (VM_MTE & !VM_MTE_ALLOWED).
	 */
	if (vm_flags & VM_MTE)
		prot |= PTE_ATTRINDX(MT_NORMAL_TAGGED);

	return __pgprot(prot);
}

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	pgprot_t ret = __pgprot(pgprot_val(__vm_get_page_prot(vm_flags)) |
			pgprot_val(arm64_arch_vm_get_page_prot(vm_flags)));

	return arm64_arch_filter_pgprot(ret);
}
EXPORT_SYMBOL(vm_get_page_prot);
