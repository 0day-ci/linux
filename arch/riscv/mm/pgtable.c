// SPDX-License-Identifier: GPL-2.0

#include <asm/pgalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/pgtable.h>

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP

#ifndef __PAGETABLE_PMD_FOLDED
int pmd_set_huge(pmd_t *pmd, phys_addr_t phys, pgprot_t prot)
{
	pmd_t new_pmd = pfn_pmd(__phys_to_pfn(phys), prot);

	set_pmd(pmd, new_pmd);
	return 1;
}

int pmd_clear_huge(pmd_t *pmd)
{
	if (!pmd_leaf(READ_ONCE(*pmd)))
		return 0;
	pmd_clear(pmd);
	return 1;
}
#endif

int pmd_free_pte_page(pmd_t *pmd, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)pmd_page_vaddr(*pmd);
	pmd_clear(pmd);

	flush_tlb_kernel_range(addr, addr + PMD_SIZE);
	pte_free_kernel(NULL, pte);
	return 1;
}

#endif /* CONFIG_HAVE_ARCH_HUGE_VMAP */
