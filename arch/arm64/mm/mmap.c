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

unsigned long arch_get_mmap_end(unsigned long addr)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		return TASK_SIZE_32;
#endif /* CONFIG_COMPAT */
#ifndef CONFIG_ARM64_FORCE_52BIT
	if (addr > DEFAULT_MAP_WINDOW_64)
		return TASK_SIZE_64;
#endif /* CONFIG_ARM64_FORCE_52BIT */
	return DEFAULT_MAP_WINDOW_64;
}
unsigned long arch_get_mmap_base(unsigned long addr)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		return current->mm->mmap_compat_base;
#endif /* CONFIG_COMPAT */
	return current->mm->mmap_base;
}
unsigned long arch_get_mmap_base_topdown(unsigned long addr)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		return current->mm->mmap_compat_base;
#endif /* CONFIG_COMPAT */
#ifndef CONFIG_ARM64_FORCE_52BIT
	if (addr > DEFAULT_MAP_WINDOW_64)
		return current->mm->mmap_base + TASK_SIZE - DEFAULT_MAP_WINDOW_64;
#endif /* CONFIG_ARM64_FORCE_52BIT */
	return current->mm->mmap_base;
}
