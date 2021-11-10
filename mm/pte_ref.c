// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, ByteDance. All rights reserved.
 *
 * 	Author: Qi Zheng <zhengqi.arch@bytedance.com>
 */

#include <linux/pte_ref.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_FREE_USER_PTE
/*
 * pte_get_unless_zero - Increment refcount for the PTE page table
 *			 unless it is zero.
 * @pmd: a pointer to the pmd entry corresponding to the PTE page table.
 */
bool pte_get_unless_zero(pmd_t *pmd)
{
	pgtable_t pte = pmd_pgtable(*pmd);

	VM_BUG_ON(!PageTable(pte));
	return atomic_inc_not_zero(&pte->pte_refcount);
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
	int retval = TRYGET_SUCCESSED;
	pmd_t pmdval;

	rcu_read_lock();
	pmdval = READ_ONCE(*pmd);
	if (unlikely(pmd_none(pmdval)))
		retval = TRYGET_FAILED_NONE;
	else if (unlikely(is_huge_pmd(pmdval)))
		retval = TRYGET_FAILED_HUGE_PMD;
	else if (!pte_get_unless_zero(&pmdval))
		retval = TRYGET_FAILED_ZERO;
	rcu_read_unlock();

	return retval;
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
	if (!(vmf->flags & FAULT_FLAG_PTE_GET))
		return;
	vmf->flags &= ~FAULT_FLAG_PTE_GET;

	pte_put(vmf->vma->vm_mm, vmf->pmd, vmf->address);
}
#else
bool pte_get_unless_zero(pmd_t *pmd)
{
	return true;
}

enum pte_tryget_type pte_try_get(pmd_t *pmd)
{
	if (unlikely(pmd_none(*pmd)))
		return TRYGET_FAILED_NONE;

	if (unlikely(is_huge_pmd(*pmd)))
		return TRYGET_FAILED_HUGE_PMD;

	return TRYGET_SUCCESSED;
}

void pte_put_vmf(struct vm_fault *vmf)
{
}
#endif /* CONFIG_FREE_USER_PTE */

#ifdef CONFIG_DEBUG_VM
static void pte_free_debug(pmd_t pmd)
{
	pte_t *ptep = (pte_t *)pmd_page_vaddr(pmd);
	int i = 0;

	for (i = 0; i < PTRS_PER_PTE; i++)
		BUG_ON(!pte_none(*ptep++));
}
#else
static inline void pte_free_debug(pmd_t pmd)
{
}
#endif

static void pte_free_rcu(struct rcu_head *rcu)
{
	struct page *page = container_of(rcu, struct page, rcu_head);

	pgtable_pte_page_dtor(page);
	__free_page(page);
}

void free_user_pte_table(struct mmu_gather *tlb, struct mm_struct *mm,
			 pmd_t *pmd, unsigned long addr)
{
	struct vm_area_struct vma = TLB_FLUSH_VMA(mm, 0);
	spinlock_t *ptl;
	pmd_t pmdval;

	ptl = pmd_lock(mm, pmd);
	pmdval = pmdp_huge_get_and_clear(mm, addr, pmd);
	if (!tlb)
		flush_tlb_range(&vma, addr, addr + PMD_SIZE);
	else
		pte_free_tlb(tlb, pmd_pgtable(pmdval), addr);
	spin_unlock(ptl);

	pte_free_debug(pmdval);
	mm_dec_nr_ptes(mm);
	if (!tlb)
		call_rcu(&pmd_pgtable(pmdval)->rcu_head, pte_free_rcu);
}
