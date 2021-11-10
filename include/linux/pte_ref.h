// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, ByteDance. All rights reserved.
 *
 * 	Author: Qi Zheng <zhengqi.arch@bytedance.com>
 */
#ifndef _LINUX_PTE_REF_H
#define _LINUX_PTE_REF_H

#include <linux/pgtable.h>

enum pte_tryget_type {
	TRYGET_SUCCESSED,
	TRYGET_FAILED_ZERO,
	TRYGET_FAILED_NONE,
	TRYGET_FAILED_HUGE_PMD,
};

bool pte_get_unless_zero(pmd_t *pmd);
enum pte_tryget_type pte_try_get(pmd_t *pmd);
void pte_put_vmf(struct vm_fault *vmf);

static inline void pte_ref_init(pgtable_t pte, pmd_t *pmd, int count)
{
}

static inline pmd_t *pte_to_pmd(pte_t *pte)
{
	return NULL;
}

static inline void pte_update_pmd(pmd_t old_pmd, pmd_t *new_pmd)
{
}

static inline void pte_get_many(pmd_t *pmd, unsigned int nr)
{
}

/*
 * pte_get - Increment refcount for the PTE page table.
 * @pmd: a pointer to the pmd entry corresponding to the PTE page table.
 *
 * Similar to the mechanism of page refcount, the user of PTE page table
 * should hold a refcount to it before accessing.
 */
static inline void pte_get(pmd_t *pmd)
{
	pte_get_many(pmd, 1);
}

static inline pte_t *pte_tryget_map(pmd_t *pmd, unsigned long address)
{
	if (pte_try_get(pmd))
		return NULL;

	return pte_offset_map(pmd, address);
}

static inline pte_t *pte_tryget_map_lock(struct mm_struct *mm, pmd_t *pmd,
					 unsigned long address, spinlock_t **ptlp)
{
	if (pte_try_get(pmd))
		return NULL;

	return pte_offset_map_lock(mm, pmd, address, ptlp);
}

static inline void pte_put_many(struct mm_struct *mm, pmd_t *pmd,
				unsigned long addr, unsigned int nr)
{
}

/*
 * pte_put - Decrement refcount for the PTE page table.
 * @mm: the mm_struct of the target address space.
 * @pmd: a pointer to the pmd entry corresponding to the PTE page table.
 * @addr: the start address of the tlb range to be flushed.
 *
 * The PTE page table page will be freed when the last refcount is dropped.
 */
static inline void pte_put(struct mm_struct *mm, pmd_t *pmd, unsigned long addr)
{
	pte_put_many(mm, pmd, addr, 1);
}

#endif
