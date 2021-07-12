/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/huge_mm.h>
#include <linux/swap.h>

/**
 * folio_is_file_lru - should the folio be on a file LRU or anon LRU?
 * @folio: the folio to test
 *
 * Returns 1 if @folio is a regular filesystem backed page cache folio
 * or a lazily freed anonymous folio (e.g. via MADV_FREE).  Returns 0 if
 * @folio is a normal anonymous folio, a tmpfs folio or otherwise ram or
 * swap backed folio.  Used by functions that manipulate the LRU lists,
 * to sort a folio onto the right LRU list.
 *
 * We would like to get this info without a page flag, but the state
 * needs to survive until the folio is last deleted from the LRU, which
 * could be as far down as __page_cache_release.
 */
static inline int folio_is_file_lru(struct folio *folio)
{
	return !folio_swapbacked(folio);
}

static inline int page_is_file_lru(struct page *page)
{
	return folio_is_file_lru(page_folio(page));
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
static __always_inline void __folio_clear_lru_flags(struct folio *folio)
{
	VM_BUG_ON_FOLIO(!folio_lru(folio), folio);

	__folio_clear_lru_flag(folio);

	/* this shouldn't happen, so leave the flags to bad_page() */
	if (folio_active(folio) && folio_unevictable(folio))
		return;

	__folio_clear_active_flag(folio);
	__folio_clear_unevictable_flag(folio);
}

static __always_inline void __clear_page_lru_flags(struct page *page)
{
	__folio_clear_lru_flags(page_folio(page));
}

/**
 * folio_lru_list - which LRU list should a folio be on?
 * @folio: the folio to test
 *
 * Returns the LRU list a folio should be on, as an index
 * into the array of LRU lists.
 */
static __always_inline enum lru_list folio_lru_list(struct folio *folio)
{
	enum lru_list lru;

	VM_BUG_ON_FOLIO(folio_active(folio) && folio_unevictable(folio), folio);

	if (folio_unevictable(folio))
		return LRU_UNEVICTABLE;

	lru = folio_is_file_lru(folio) ? LRU_INACTIVE_FILE : LRU_INACTIVE_ANON;
	if (folio_active(folio))
		lru += LRU_ACTIVE;

	return lru;
}

static __always_inline enum lru_list page_lru(struct page *page)
{
	return folio_lru_list(page_folio(page));
}

static __always_inline void add_page_to_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	enum lru_list lru = page_lru(page);

	update_lru_size(lruvec, lru, page_zonenum(page), compound_nr(page));
	list_add(&page->lru, &lruvec->lists[lru]);
}

static __always_inline void folio_add_to_lru_list(struct folio *folio,
				struct lruvec *lruvec)
{
	add_page_to_lru_list(&folio->page, lruvec);
}

static __always_inline void add_page_to_lru_list_tail(struct page *page,
				struct lruvec *lruvec)
{
	enum lru_list lru = page_lru(page);

	update_lru_size(lruvec, lru, page_zonenum(page), compound_nr(page));
	list_add_tail(&page->lru, &lruvec->lists[lru]);
}

static __always_inline void folio_add_to_lru_list_tail(struct folio *folio,
				struct lruvec *lruvec)
{
	add_page_to_lru_list_tail(&folio->page, lruvec);
}

static __always_inline void del_page_from_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	list_del(&page->lru);
	update_lru_size(lruvec, page_lru(page), page_zonenum(page),
			-compound_nr(page));
}

static __always_inline void folio_del_from_lru_list(struct folio *folio,
				struct lruvec *lruvec)
{
	del_page_from_lru_list(&folio->page, lruvec);
}
#endif
