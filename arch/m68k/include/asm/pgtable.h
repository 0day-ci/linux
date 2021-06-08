/* SPDX-License-Identifier: GPL-2.0 */
#ifdef __uClinux__
#include <asm/pgtable_no.h>
#else
#include <asm/pgtable_mm.h>
#endif


#if defined(CONFIG_COLDFIRE)
#define pmd_pgtable(pmd) pfn_to_virt(pmd_val(pmd) >> PAGE_SHIFT)
#elif defined(CONFIG_SUN3)
#define pmd_pgtable(pmd) pmd_page(pmd)
#else
#define pmd_pgtable(pmd) ((pgtable_t)pmd_page_vaddr(pmd))
#endif
