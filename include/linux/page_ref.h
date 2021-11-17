/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_REF_H
#define _LINUX_PAGE_REF_H

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/tracepoint-defs.h>

DECLARE_TRACEPOINT(page_ref_init);
DECLARE_TRACEPOINT(page_ref_mod);
DECLARE_TRACEPOINT(page_ref_mod_and_test);
DECLARE_TRACEPOINT(page_ref_mod_and_return);
DECLARE_TRACEPOINT(page_ref_add_unless);
DECLARE_TRACEPOINT(page_ref_freeze);
DECLARE_TRACEPOINT(page_ref_unfreeze);

#ifdef CONFIG_DEBUG_PAGE_REF

/*
 * Ideally we would want to use the trace_<tracepoint>_enabled() helper
 * functions. But due to include header file issues, that is not
 * feasible. Instead we have to open code the static key functions.
 *
 * See trace_##name##_enabled(void) in include/linux/tracepoint.h
 */
#define page_ref_tracepoint_active(t) tracepoint_enabled(t)

extern void __page_ref_init(struct page *page);
extern void __page_ref_mod(struct page *page, int v);
extern void __page_ref_mod_and_test(struct page *page, int v, int ret);
extern void __page_ref_mod_and_return(struct page *page, int v, int ret);
extern void __page_ref_add_unless(struct page *page, int v, int u, int ret);
extern void __page_ref_freeze(struct page *page, int v, int ret);
extern void __page_ref_unfreeze(struct page *page, int v);

#else

#define page_ref_tracepoint_active(t) false

static inline void __page_ref_init(struct page *page)
{
}
static inline void __page_ref_mod(struct page *page, int v)
{
}
static inline void __page_ref_mod_and_test(struct page *page, int v, int ret)
{
}
static inline void __page_ref_mod_and_return(struct page *page, int v, int ret)
{
}
static inline void __page_ref_add_unless(struct page *page, int v, int u, int ret)
{
}
static inline void __page_ref_freeze(struct page *page, int v, int ret)
{
}
static inline void __page_ref_unfreeze(struct page *page, int v)
{
}

#endif

static inline int page_ref_count(const struct page *page)
{
	return atomic_read(&page->_refcount);
}

/**
 * folio_ref_count - The reference count on this folio.
 * @folio: The folio.
 *
 * The refcount is usually incremented by calls to folio_get() and
 * decremented by calls to folio_put().  Some typical users of the
 * folio refcount:
 *
 * - Each reference from a page table
 * - The page cache
 * - Filesystem private data
 * - The LRU list
 * - Pipes
 * - Direct IO which references this page in the process address space
 *
 * Return: The number of references to this folio.
 */
static inline int folio_ref_count(const struct folio *folio)
{
	return page_ref_count(&folio->page);
}

static inline int page_count(const struct page *page)
{
	return folio_ref_count(page_folio(page));
}

/*
 * Setup the page refcount to one before being freed into the page allocator.
 * The memory might not be initialized and therefore there cannot be any
 * assumptions about the current value of page->_refcount. This call should be
 * done during boot when memory is being initialized, during memory hotplug
 * when new memory is added, or when a previous reserved memory is unreserved
 * this is the first time kernel take control of the given memory.
 */
static inline void page_ref_init(struct page *page)
{
	atomic_set(&page->_refcount, 1);
	if (page_ref_tracepoint_active(page_ref_init))
		__page_ref_init(page);
}

static inline void folio_ref_init(struct folio *folio)
{
	page_ref_init(&folio->page);
}

static inline int page_ref_add_return(struct page *page, int nr)
{
	int old_val = atomic_fetch_add(nr, &page->_refcount);
	int new_val = old_val + nr;

	VM_BUG_ON_PAGE((unsigned int)new_val < (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, nr, new_val);
	return new_val;
}

static inline void page_ref_add(struct page *page, int nr)
{
	int old_val = atomic_fetch_add(nr, &page->_refcount);
	int new_val = old_val + nr;

	VM_BUG_ON_PAGE((unsigned int)new_val < (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, nr);
}

static inline void folio_ref_add(struct folio *folio, int nr)
{
	page_ref_add(&folio->page, nr);
}

static inline void page_ref_sub(struct page *page, int nr)
{
	int old_val = atomic_fetch_sub(nr, &page->_refcount);
	int new_val = old_val - nr;

	VM_BUG_ON_PAGE((unsigned int)new_val > (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, -nr);
}

static inline void folio_ref_sub(struct folio *folio, int nr)
{
	page_ref_sub(&folio->page, nr);
}

static inline int page_ref_sub_return(struct page *page, int nr)
{
	int old_val = atomic_fetch_sub(nr, &page->_refcount);
	int new_val = old_val - nr;

	VM_BUG_ON_PAGE((unsigned int)new_val > (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, -nr, new_val);
	return new_val;
}

static inline int folio_ref_sub_return(struct folio *folio, int nr)
{
	return page_ref_sub_return(&folio->page, nr);
}

static inline void page_ref_inc(struct page *page)
{
	int old_val = atomic_fetch_inc(&page->_refcount);
	int new_val = old_val + 1;

	VM_BUG_ON_PAGE((unsigned int)new_val < (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, 1);
}

static inline void folio_ref_inc(struct folio *folio)
{
	page_ref_inc(&folio->page);
}

static inline void page_ref_dec(struct page *page)
{
	int old_val = atomic_fetch_dec(&page->_refcount);
	int new_val = old_val - 1;

	VM_BUG_ON_PAGE((unsigned int)new_val > (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, -1);
}

static inline void folio_ref_dec(struct folio *folio)
{
	page_ref_dec(&folio->page);
}

static inline int page_ref_sub_and_test(struct page *page, int nr)
{
	int old_val = atomic_fetch_sub(nr, &page->_refcount);
	int new_val = old_val - nr;
	int ret = new_val == 0;

	VM_BUG_ON_PAGE((unsigned int)new_val > (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod_and_test))
		__page_ref_mod_and_test(page, -nr, ret);
	return ret;
}

static inline int folio_ref_sub_and_test(struct folio *folio, int nr)
{
	return page_ref_sub_and_test(&folio->page, nr);
}

static inline int page_ref_inc_return(struct page *page)
{
	int old_val = atomic_fetch_inc(&page->_refcount);
	int new_val = old_val + 1;

	VM_BUG_ON_PAGE((unsigned int)new_val < (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, 1, new_val);
	return new_val;
}

static inline int folio_ref_inc_return(struct folio *folio)
{
	return page_ref_inc_return(&folio->page);
}

static inline int page_ref_dec_and_test(struct page *page)
{
	int old_val = atomic_fetch_dec(&page->_refcount);
	int new_val = old_val - 1;
	int ret = new_val == 0;

	VM_BUG_ON_PAGE((unsigned int)new_val > (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod_and_test))
		__page_ref_mod_and_test(page, -1, ret);
	return ret;
}

static inline int folio_ref_dec_and_test(struct folio *folio)
{
	return page_ref_dec_and_test(&folio->page);
}

static inline int page_ref_dec_return(struct page *page)
{
	int old_val = atomic_fetch_dec(&page->_refcount);
	int new_val = old_val - 1;

	VM_BUG_ON_PAGE((unsigned int)new_val > (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, -1, new_val);
	return new_val;
}

static inline int folio_ref_dec_return(struct folio *folio)
{
	return page_ref_dec_return(&folio->page);
}

static inline bool page_ref_add_unless(struct page *page, int nr, int u)
{
	int old_val = atomic_fetch_add_unless(&page->_refcount, nr, u);
	int new_val = old_val + nr;
	int ret = old_val != u;

	VM_BUG_ON_PAGE(ret && (unsigned int)new_val < (unsigned int)old_val, page);
	if (page_ref_tracepoint_active(page_ref_add_unless))
		__page_ref_add_unless(page, nr, u, ret);
	return ret;
}

static inline bool folio_ref_add_unless(struct folio *folio, int nr, int u)
{
	return page_ref_add_unless(&folio->page, nr, u);
}

/**
 * folio_try_get - Attempt to increase the refcount on a folio.
 * @folio: The folio.
 *
 * If you do not already have a reference to a folio, you can attempt to
 * get one using this function.  It may fail if, for example, the folio
 * has been freed since you found a pointer to it, or it is frozen for
 * the purposes of splitting or migration.
 *
 * Return: True if the reference count was successfully incremented.
 */
static inline bool folio_try_get(struct folio *folio)
{
	return folio_ref_add_unless(folio, 1, 0);
}

static inline bool folio_ref_try_add_rcu(struct folio *folio, int count)
{
#ifdef CONFIG_TINY_RCU
	/*
	 * The caller guarantees the folio will not be freed from interrupt
	 * context, so (on !SMP) we only need preemption to be disabled
	 * and TINY_RCU does that for us.
	 */
# ifdef CONFIG_PREEMPT_COUNT
	VM_BUG_ON(!in_atomic() && !irqs_disabled());
# endif
	VM_BUG_ON_FOLIO(folio_ref_count(folio) == 0, folio);
	folio_ref_add(folio, count);
#else
	if (unlikely(!folio_ref_add_unless(folio, count, 0))) {
		/* Either the folio has been freed, or will be freed. */
		return false;
	}
#endif
	return true;
}

/**
 * folio_try_get_rcu - Attempt to increase the refcount on a folio.
 * @folio: The folio.
 *
 * This is a version of folio_try_get() optimised for non-SMP kernels.
 * If you are still holding the rcu_read_lock() after looking up the
 * page and know that the page cannot have its refcount decreased to
 * zero in interrupt context, you can use this instead of folio_try_get().
 *
 * Example users include get_user_pages_fast() (as pages are not unmapped
 * from interrupt context) and the page cache lookups (as pages are not
 * truncated from interrupt context).  We also know that pages are not
 * frozen in interrupt context for the purposes of splitting or migration.
 *
 * You can also use this function if you're holding a lock that prevents
 * pages being frozen & removed; eg the i_pages lock for the page cache
 * or the mmap_sem or page table lock for page tables.  In this case,
 * it will always succeed, and you could have used a plain folio_get(),
 * but it's sometimes more convenient to have a common function called
 * from both locked and RCU-protected contexts.
 *
 * Return: True if the reference count was successfully incremented.
 */
static inline bool folio_try_get_rcu(struct folio *folio)
{
	return folio_ref_try_add_rcu(folio, 1);
}

static inline int page_ref_freeze(struct page *page, int count)
{
	int ret = likely(atomic_cmpxchg(&page->_refcount, count, 0) == count);

	if (page_ref_tracepoint_active(page_ref_freeze))
		__page_ref_freeze(page, count, ret);
	return ret;
}

static inline int folio_ref_freeze(struct folio *folio, int count)
{
	return page_ref_freeze(&folio->page, count);
}

static inline void page_ref_unfreeze(struct page *page, int count)
{
	VM_BUG_ON_PAGE(page_count(page) != 0, page);
	VM_BUG_ON(count == 0);

	atomic_set_release(&page->_refcount, count);
	if (page_ref_tracepoint_active(page_ref_unfreeze))
		__page_ref_unfreeze(page, count);
}

static inline void folio_ref_unfreeze(struct folio *folio, int count)
{
	page_ref_unfreeze(&folio->page, count);
}
#endif
