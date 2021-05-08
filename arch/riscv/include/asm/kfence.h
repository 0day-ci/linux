/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_KFENCE_H
#define _ASM_RISCV_KFENCE_H

#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/kfence.h>
#include <asm/pgtable.h>

static inline bool arch_kfence_init_pool(void)
{
	int i;
	unsigned long addr;
	pte_t *pte;
	pmd_t *pmd;

	for (addr = (unsigned long)__kfence_pool; is_kfence_address((void *)addr);
	     addr += PAGE_SIZE) {
		pte = virt_to_kpte(addr);
		pmd = pmd_off_k(addr);

		if (!pmd_leaf(*pmd) && pte_present(*pte))
			continue;

		pte = kmalloc(PAGE_SIZE, GFP_ATOMIC);
		for (i = 0; i < PTRS_PER_PTE; i++)
			set_pte(pte + i, pfn_pte(PFN_DOWN(__pa((addr & PMD_MASK) + i * PAGE_SIZE)), PAGE_KERNEL));

		set_pmd(pmd, pfn_pmd(PFN_DOWN(__pa(pte)), PAGE_TABLE));
		flush_tlb_kernel_range(addr, addr + PMD_SIZE);
	}

	return true;
}

static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	pte_t *pte = virt_to_kpte(addr);

	if (protect)
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
	else
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));

	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);

	return true;
}

#endif /* _ASM_RISCV_KFENCE_H */
