// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/sched.h>
#include <linux/mman.h>
#include <linux/shm.h>

#define COLOUR_ALIGN(addr,pgoff)		\
	((((addr)+SHMLBA-1)&~(SHMLBA-1)) +	\
	 (((pgoff)<<PAGE_SHIFT) & (SHMLBA-1)))

/*
 * We need to ensure that shared mappings are correctly aligned to
 * avoid aliasing issues with VIPT caches.  We need to ensure that
 * a specific page of an object is always mapped at a multiple of
 * SHMLBA bytes.
 *
 * We unconditionally provide this function for all cases, however
 * in the VIVT case, we optimise out the alignment rules.
 */
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		       unsigned long len, unsigned long pgoff,
		       unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int do_align = 0;
	struct vm_unmapped_area_info info;
	int aliasing = 0;
	if(IS_ENABLED(CONFIG_CPU_CACHE_ALIASING))
		aliasing = 1;

	/*
	 * We only need to do colour alignment if either the I or D
	 * caches alias.
	 */
	if (aliasing)
		do_align = filp || (flags & MAP_SHARED);

	/*
	 * We enforce the MAP_FIXED case.
	 */
	if (flags & MAP_FIXED) {
		if (aliasing && flags & MAP_SHARED &&
		    (addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1))
			return -EINVAL;
		return addr;
	}

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		if (do_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_mask = do_align ? (PAGE_MASK & (SHMLBA - 1)) : 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	return vm_unmapped_area(&info);
}

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	switch (vm_flags & (VM_READ | VM_WRITE | VM_EXEC | VM_SHARED)) {
	case VM_NONE:
		return (PAGE_NONE | _PAGE_CACHE_SHRD);
	case VM_READ:
		return (PAGE_READ | _PAGE_CACHE_SHRD);
	case VM_WRITE:
	case VM_WRITE | VM_READ:
		return (PAGE_COPY | _PAGE_CACHE_SHRD);
	case VM_EXEC:
		return (PAGE_EXEC | _PAGE_CACHE_SHRD);
	case VM_EXEC | VM_READ:
		return (PAGE_READ | _PAGE_E | _PAGE_CACHE_SHRD);
	case VM_EXEC | VM_WRITE:
	case VM_EXEC | VM_WRITE | VM_READ:
		return (PAGE_COPY | _PAGE_E | _PAGE_CACHE_SHRD);
	case VM_SHARED:
		return (PAGE_NONE | _PAGE_CACHE_SHRD);
	case VM_SHARED | VM_READ:
		return (PAGE_READ | _PAGE_CACHE_SHRD);
	case VM_SHARED | VM_WRITE:
	case VM_SHARED | VM_WRITE | VM_READ:
		return (PAGE_RDWR | _PAGE_CACHE_SHRD);
	case VM_SHARED | VM_EXEC:
		return (PAGE_EXEC | _PAGE_CACHE_SHRD);
	case VM_SHARED | VM_EXEC | VM_READ:
		return (PAGE_READ | _PAGE_E | _PAGE_CACHE_SHRD);
	case VM_SHARED | VM_EXEC | VM_WRITE:
	case VM_SHARED | VM_EXEC | VM_WRITE | VM_READ:
		return (PAGE_RDWR | _PAGE_E | _PAGE_CACHE_SHRD);
	default:
		BUILD_BUG();
	}
}
EXPORT_SYMBOL(vm_get_page_prot);
