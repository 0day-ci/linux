// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, ByteDance. All rights reserved.
 *
 * 	Author: Qi Zheng <zhengqi.arch@bytedance.com>
 */

#include <linux/pte_ref.h>
#include <linux/mm.h>

/*
 * pte_get_unless_zero - Increment refcount for the PTE page table
 *			 unless it is zero.
 * @pmd: a pointer to the pmd entry corresponding to the PTE page table.
 */
bool pte_get_unless_zero(pmd_t *pmd)
{
	return true;
}

/*
 * pte_try_get - Try to increment refcount for the PTE page table.
 * @pmd: a pointer to the pmd entry corresponding to the PTE page table.
 *
 * Return true if the increment succeeded. Otherwise return false.
 *
 * Before Operating the PTE page table, we need to hold a refcount
 * to protect against the concurrent release of the PTE page table.
 * But we will fail in the following case:
 * 	- The content mapped in @pmd is not a PTE page
 * 	- The refcount of the PTE page table is zero, it will be freed
 */
enum pte_tryget_type pte_try_get(pmd_t *pmd)
{
	if (unlikely(pmd_none(*pmd)))
		return TRYGET_FAILED_NONE;
	if (unlikely(is_huge_pmd(*pmd)))
		return TRYGET_FAILED_HUGE_PMD;

	return TRYGET_SUCCESSED;
}

/*
 * pte_put_vmf - Decrement refcount for the PTE page table.
 * @vmf: fault information
 *
 * The mmap_lock may be unlocked in advance in some cases
 * in handle_pte_fault(), then the pmd entry will no longer
 * be stable. For example, the corresponds of the PTE page may
 * be replaced(e.g. mremap), so we should ensure the pte_put()
 * is performed in the critical section of the mmap_lock.
 */
void pte_put_vmf(struct vm_fault *vmf)
{
}
