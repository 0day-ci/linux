// SPDX-License-Identifier: GPL-2.0
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include "prmtv-common.h"

/*
 * Get an online page for a pfn if it's in the LRU list.  Otherwise, returns
 * NULL.
 *
 * The body of this function is stolen from the 'page_idle_get_page()'.  We
 * steal rather than reuse it because the code is quite simple.
 */
struct page *damon_get_page(unsigned long pfn)
{
	struct page *page = pfn_to_online_page(pfn);

	if (!page || !PageLRU(page) || !get_page_unless_zero(page))
		return NULL;

	if (unlikely(!PageLRU(page))) {
		put_page(page);
		page = NULL;
	}
	return page;
}

void damon_ptep_mkold(pte_t *pte, struct mm_struct *mm, unsigned long addr)
{
	bool referenced = false;
	struct page *page = damon_get_page(pte_pfn(*pte));

	if (!page)
		return;

	if (pte_young(*pte)) {
		referenced = true;
		*pte = pte_mkold(*pte);
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (mmu_notifier_clear_young(mm, addr, addr + PAGE_SIZE))
		referenced = true;
#endif /* CONFIG_MMU_NOTIFIER */

	if (referenced)
		set_page_young(page);

	set_page_idle(page);
	put_page(page);
}

static inline bool pte_is_pinned(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	struct page *page;

	if (!pte_write(pte))
		return false;
	if (!is_cow_mapping(vma->vm_flags))
		return false;
	if (likely(!test_bit(MMF_HAS_PINNED, &vma->vm_mm->flags)))
		return false;
	page = vm_normal_page(vma, addr, pte);
	if (!page)
		return false;
	return page_maybe_dma_pinned(page);
}

static inline void clear_soft_dirty_pmd(struct vm_area_struct *vma,
		unsigned long addr, pmd_t *pmdp)
{
	pmd_t old, pmd = *pmdp;

	if (pmd_present(pmd)) {
		/* See comment in change_huge_pmd() */
		old = pmdp_invalidate(vma, addr, pmdp);
		if (pmd_dirty(old))
			pmd = pmd_mkdirty(pmd);
		if (pmd_young(old))
			pmd = pmd_mkyoung(pmd);

		pmd = pmd_wrprotect(pmd);
		pmd = pmd_clear_soft_dirty(pmd);

		set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
	} else if (is_migration_entry(pmd_to_swp_entry(pmd))) {
		pmd = pmd_swp_clear_soft_dirty(pmd);
		set_pmd_at(vma->vm_mm, addr, pmdp, pmd);
	}
}

static inline void clear_soft_dirty(struct vm_area_struct *vma,
		unsigned long addr, pte_t *pte)
{
	/*
	 * The soft-dirty tracker uses #PF-s to catch writes
	 * to pages, so write-protect the pte as well. See the
	 * Documentation/admin-guide/mm/soft-dirty.rst for full description
	 * of how soft-dirty works.
	 */
	pte_t ptent = *pte;

	if (pte_present(ptent)) {
		pte_t old_pte;

		if (pte_is_pinned(vma, addr, ptent))
			return;
		old_pte = ptep_modify_prot_start(vma, addr, pte);
		ptent = pte_wrprotect(old_pte);
		ptent = pte_clear_soft_dirty(ptent);
		ptep_modify_prot_commit(vma, addr, pte, old_pte, ptent);
	} else if (is_swap_pte(ptent)) {
		ptent = pte_swp_clear_soft_dirty(ptent);
		set_pte_at(vma->vm_mm, addr, pte, ptent);
	}
}

void damon_pmd_clean_soft_dirty(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd)
{
	vma->vm_flags &= ~VM_SOFTDIRTY;
	vma_set_page_prot(vma);

	if (pmd_soft_dirty(*pmd))
		clear_soft_dirty_pmd(vma, addr, pmd);

}

void damon_ptep_clean_soft_dirty(struct vm_area_struct *vma, unsigned long addr,
		pte_t *pte)
{
	vma->vm_flags &= ~VM_SOFTDIRTY;
	vma_set_page_prot(vma);

	if (pte_soft_dirty(*pte))
		clear_soft_dirty(vma, addr, pte);
}

void damon_pmdp_mkold(pmd_t *pmd, struct mm_struct *mm, unsigned long addr)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	bool referenced = false;
	struct page *page = damon_get_page(pmd_pfn(*pmd));

	if (!page)
		return;

	if (pmd_young(*pmd)) {
		referenced = true;
		*pmd = pmd_mkold(*pmd);
	}

#ifdef CONFIG_MMU_NOTIFIER
	if (mmu_notifier_clear_young(mm, addr,
				addr + ((1UL) << HPAGE_PMD_SHIFT)))
		referenced = true;
#endif /* CONFIG_MMU_NOTIFIER */

	if (referenced)
		set_page_young(page);

	set_page_idle(page);
	put_page(page);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
}

#define DAMON_MAX_SUBSCORE	(100)
#define DAMON_MAX_AGE_IN_LOG	(32)

int damon_pageout_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s)
{
	unsigned int max_nr_accesses;
	int freq_subscore;
	unsigned int age_in_sec;
	int age_in_log, age_subscore;
	unsigned int freq_weight = s->quota.weight_nr_accesses;
	unsigned int age_weight = s->quota.weight_age;
	int hotness;

	max_nr_accesses = c->aggr_interval / c->sample_interval;
	freq_subscore = r->nr_accesses * DAMON_MAX_SUBSCORE / max_nr_accesses;

	age_in_sec = (unsigned long)r->age * c->aggr_interval / 1000000;
	for (age_in_log = 0; age_in_log < DAMON_MAX_AGE_IN_LOG && age_in_sec;
			age_in_log++, age_in_sec >>= 1)
		;

	/* If frequency is 0, higher age means it's colder */
	if (freq_subscore == 0)
		age_in_log *= -1;

	/*
	 * Now age_in_log is in [-DAMON_MAX_AGE_IN_LOG, DAMON_MAX_AGE_IN_LOG].
	 * Scale it to be in [0, 100] and set it as age subscore.
	 */
	age_in_log += DAMON_MAX_AGE_IN_LOG;
	age_subscore = age_in_log * DAMON_MAX_SUBSCORE /
		DAMON_MAX_AGE_IN_LOG / 2;

	hotness = (freq_weight * freq_subscore + age_weight * age_subscore);
	if (freq_weight + age_weight)
		hotness /= freq_weight + age_weight;
	/*
	 * Transform it to fit in [0, DAMOS_MAX_SCORE]
	 */
	hotness = hotness * DAMOS_MAX_SCORE / DAMON_MAX_SUBSCORE;

	/* Return coldness of the region */
	return DAMOS_MAX_SCORE - hotness;
}
