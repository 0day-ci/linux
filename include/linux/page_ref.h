/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_REF_H
#define _LINUX_PAGE_REF_H

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/tracepoint-defs.h>

DECLARE_TRACEPOINT(page_ref_init);
DECLARE_TRACEPOINT(page_ref_mod_and_return);
DECLARE_TRACEPOINT(page_ref_mod_unless);
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
extern void __page_ref_mod_and_return(struct page *page, int v, int ret);
extern void __page_ref_mod_unless(struct page *page, int v, int u);
extern void __page_ref_freeze(struct page *page, int v, int ret);
extern void __page_ref_unfreeze(struct page *page, int v);

#else

#define page_ref_tracepoint_active(t) false

static inline void __page_ref_init(struct page *page)
{
}
static inline void __page_ref_mod_and_return(struct page *page, int v, int ret)
{
}
static inline void __page_ref_mod_unless(struct page *page, int v, int u)
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

static inline int page_count(const struct page *page)
{
	return atomic_read(&compound_head(page)->_refcount);
}

/*
 * Setup the page->_refcount to 1 before being freed into the page allocator.
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

static inline int page_ref_add_return(struct page *page, int nr)
{
	int ret;

	VM_BUG_ON(nr <= 0);
	ret = atomic_add_return(nr, &page->_refcount);
	VM_BUG_ON_PAGE(ret <= 0, page);

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, nr, ret);
	return ret;
}

static inline void page_ref_add(struct page *page, int nr)
{
	page_ref_add_return(page, nr);
}

static inline int page_ref_sub_return(struct page *page, int nr)
{
	int ret;

	VM_BUG_ON(nr <= 0);
	ret = atomic_sub_return(nr, &page->_refcount);
	VM_BUG_ON_PAGE(ret < 0, page);

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, -nr, ret);
	return ret;
}

static inline void page_ref_sub(struct page *page, int nr)
{
	page_ref_sub_return(page, nr);
}

static inline int page_ref_sub_and_test(struct page *page, int nr)
{
	return page_ref_sub_return(page, nr) == 0;
}

static inline int page_ref_inc_return(struct page *page)
{
	int ret = atomic_inc_return(&page->_refcount);

	VM_BUG_ON_PAGE(ret <= 0, page);

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, 1, ret);
	return ret;
}

static inline void page_ref_inc(struct page *page)
{

	page_ref_inc_return(page);
}

static inline int page_ref_dec_return(struct page *page)
{
	int ret = atomic_dec_return(&page->_refcount);

	VM_BUG_ON_PAGE(ret < 0, page);

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, -1, ret);
	return ret;
}

static inline void page_ref_dec(struct page *page)
{
	page_ref_dec_return(page);
}

static inline int page_ref_dec_and_test(struct page *page)
{
	return page_ref_dec_return(page) == 0;
}

static inline int page_ref_add_unless(struct page *page, int nr, int u)
{
	int ret;

	VM_BUG_ON(nr <= 0 || u < 0);
	ret = atomic_fetch_add_unless(&page->_refcount, nr, u);
	VM_BUG_ON_PAGE(ret < 0, page);

	if (page_ref_tracepoint_active(page_ref_mod_unless))
		__page_ref_mod_unless(page, nr, ret);
	return ret != u;
}

static inline int page_ref_freeze(struct page *page, int count)
{
	int ret;

	VM_BUG_ON(count <= 0);
	ret = likely(atomic_cmpxchg(&page->_refcount, count, 0) == count);

	if (page_ref_tracepoint_active(page_ref_freeze))
		__page_ref_freeze(page, count, ret);
	return ret;
}

static inline void page_ref_unfreeze(struct page *page, int count)
{
	VM_BUG_ON_PAGE(page_count(page) != 0, page);
	VM_BUG_ON(count == 0);

	atomic_set_release(&page->_refcount, count);
	if (page_ref_tracepoint_active(page_ref_unfreeze))
		__page_ref_unfreeze(page, count);
}

#endif
