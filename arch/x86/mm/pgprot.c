// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/mm.h>
#include <asm/pgtable.h>

static inline pgprot_t __vm_get_page_prot(unsigned long vm_flags)
{
	switch (vm_flags & (VM_READ | VM_WRITE | VM_EXEC | VM_SHARED)) {
	case VM_NONE:
		return PAGE_NONE;
	case VM_READ:
		return PAGE_READONLY;
	case VM_WRITE:
		return PAGE_COPY;
	case VM_WRITE | VM_READ:
		return PAGE_COPY;
	case VM_EXEC:
	case VM_EXEC | VM_READ:
		return PAGE_READONLY_EXEC;
	case VM_EXEC | VM_WRITE:
	case VM_EXEC | VM_WRITE | VM_READ:
		return PAGE_COPY_EXEC;
	case VM_SHARED:
		return PAGE_NONE;
	case VM_SHARED | VM_READ:
		return PAGE_READONLY;
	case VM_SHARED | VM_WRITE:
	case VM_SHARED | VM_WRITE | VM_READ:
		return PAGE_SHARED;
	case VM_SHARED | VM_EXEC:
	case VM_SHARED | VM_EXEC | VM_READ:
		return PAGE_READONLY_EXEC;
	case VM_SHARED | VM_EXEC | VM_WRITE:
	case VM_SHARED | VM_EXEC | VM_WRITE | VM_READ:
		return PAGE_SHARED_EXEC;
	default:
		BUILD_BUG();
		return PAGE_NONE;
	}
}


pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	unsigned long val = pgprot_val(__vm_get_page_prot(vm_flags));

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
	/*
	 * Take the 4 protection key bits out of the vma->vm_flags value and
	 * turn them in to the bits that we can put in to a pte.
	 *
	 * Only override these if Protection Keys are available (which is only
	 * on 64-bit).
	 */
	if (vm_flags & VM_PKEY_BIT0)
		val |= _PAGE_PKEY_BIT0;
	if (vm_flags & VM_PKEY_BIT1)
		val |= _PAGE_PKEY_BIT1;
	if (vm_flags & VM_PKEY_BIT2)
		val |= _PAGE_PKEY_BIT2;
	if (vm_flags & VM_PKEY_BIT3)
		val |= _PAGE_PKEY_BIT3;
#endif

	val = __sme_set(val);
	if (val & _PAGE_PRESENT)
		val &= __supported_pte_mask;
	return __pgprot(val);
}
EXPORT_SYMBOL(vm_get_page_prot);
