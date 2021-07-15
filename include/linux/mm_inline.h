/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/huge_mm.h>
#include <linux/swap.h>
#include <linux/userfaultfd_k.h>
#include <linux/swapops.h>

/**
 * page_is_file_lru - should the page be on a file LRU or anon LRU?
 * @page: the page to test
 *
 * Returns 1 if @page is a regular filesystem backed page cache page or a lazily
 * freed anonymous page (e.g. via MADV_FREE).  Returns 0 if @page is a normal
 * anonymous page, a tmpfs page or otherwise ram or swap backed page.  Used by
 * functions that manipulate the LRU lists, to sort a page onto the right LRU
 * list.
 *
 * We would like to get this info without a page flag, but the state
 * needs to survive until the page is last deleted from the LRU, which
 * could be as far down as __page_cache_release.
 */
static inline int page_is_file_lru(struct page *page)
{
	return !PageSwapBacked(page);
}

static __always_inline void update_lru_size(struct lruvec *lruvec,
				enum lru_list lru, enum zone_type zid,
				int nr_pages)
{
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);

	__mod_lruvec_state(lruvec, NR_LRU_BASE + lru, nr_pages);
	__mod_zone_page_state(&pgdat->node_zones[zid],
				NR_ZONE_LRU_BASE + lru, nr_pages);
#ifdef CONFIG_MEMCG
	mem_cgroup_update_lru_size(lruvec, lru, zid, nr_pages);
#endif
}

/**
 * __clear_page_lru_flags - clear page lru flags before releasing a page
 * @page: the page that was on lru and now has a zero reference
 */
static __always_inline void __clear_page_lru_flags(struct page *page)
{
	VM_BUG_ON_PAGE(!PageLRU(page), page);

	__ClearPageLRU(page);

	/* this shouldn't happen, so leave the flags to bad_page() */
	if (PageActive(page) && PageUnevictable(page))
		return;

	__ClearPageActive(page);
	__ClearPageUnevictable(page);
}

/**
 * page_lru - which LRU list should a page be on?
 * @page: the page to test
 *
 * Returns the LRU list a page should be on, as an index
 * into the array of LRU lists.
 */
static __always_inline enum lru_list page_lru(struct page *page)
{
	enum lru_list lru;

	VM_BUG_ON_PAGE(PageActive(page) && PageUnevictable(page), page);

	if (PageUnevictable(page))
		return LRU_UNEVICTABLE;

	lru = page_is_file_lru(page) ? LRU_INACTIVE_FILE : LRU_INACTIVE_ANON;
	if (PageActive(page))
		lru += LRU_ACTIVE;

	return lru;
}

static __always_inline void add_page_to_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	enum lru_list lru = page_lru(page);

	update_lru_size(lruvec, lru, page_zonenum(page), thp_nr_pages(page));
	list_add(&page->lru, &lruvec->lists[lru]);
}

static __always_inline void add_page_to_lru_list_tail(struct page *page,
				struct lruvec *lruvec)
{
	enum lru_list lru = page_lru(page);

	update_lru_size(lruvec, lru, page_zonenum(page), thp_nr_pages(page));
	list_add_tail(&page->lru, &lruvec->lists[lru]);
}

static __always_inline void del_page_from_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	list_del(&page->lru);
	update_lru_size(lruvec, page_lru(page), page_zonenum(page),
			-thp_nr_pages(page));
}

/*
 * If this pte is wr-protected by uffd-wp in any form, arm the special pte to
 * replace a none pte.  NOTE!  This should only be called when *pte is already
 * cleared so we will never accidentally replace something valuable.  Meanwhile
 * none pte also means we are not demoting the pte so tlb flushed is not needed.
 * E.g., when pte cleared the caller should have taken care of the tlb flush.
 *
 * Must be called with pgtable lock held.
 */
static inline void
pte_install_uffd_wp_if_needed(struct vm_area_struct *vma, unsigned long addr,
			      pte_t *pte, pte_t pteval)
{
#ifdef CONFIG_USERFAULTFD
	bool arm_uffd_pte = false;

	/* The current status of the pte should be "cleared" before calling */
	WARN_ON_ONCE(!pte_none(*pte));

	if (vma_is_anonymous(vma))
		return;

	/* A uffd-wp wr-protected normal pte */
	if (unlikely(pte_present(pteval) && pte_uffd_wp(pteval)))
		arm_uffd_pte = true;

	/*
	 * A uffd-wp wr-protected swap pte.  Note: this should even work for
	 * pte_swp_uffd_wp_special() too.
	 */
	if (unlikely(is_swap_pte(pteval) && pte_swp_uffd_wp(pteval)))
		arm_uffd_pte = true;

	if (unlikely(arm_uffd_pte))
		set_pte_at(vma->vm_mm, addr, pte,
			   pte_swp_mkuffd_wp_special(vma));
#endif
}

#endif
