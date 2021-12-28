/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM page_pin_owner

#if !defined(_TRACE_PAGE_PIN_OWNER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PAGE_PIN_OWNER_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>

TRACE_EVENT(page_pin_owner_put,

	TP_PROTO(struct page *page),

	TP_ARGS(page),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(unsigned long, flags)
		__field(int, count)
		__field(int, mapcount)
		__field(void *, mapping)
		__field(int, mt)
		),

	TP_fast_assign(
		__entry->pfn = page_to_pfn(page);
		__entry->flags = page->flags;
		__entry->count = page_ref_count(page);
		__entry->mapcount = page_mapcount(page);
		__entry->mapping = page->mapping;
		__entry->mt = get_pageblock_migratetype(page);
		),

	TP_printk("pfn=0x%lx flags=%s count=%d mapcount=%d mapping=%p mt=%d",
			__entry->pfn,
			show_page_flags(__entry->flags & ((1UL << NR_PAGEFLAGS) - 1)),
			__entry->count,
			__entry->mapcount, __entry->mapping, __entry->mt)
);

TRACE_EVENT(report_page_pinners,

	TP_PROTO(struct page *page, const char *reason, int err),

	TP_ARGS(page, reason, err),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(unsigned long, flags)
		__field(int, count)
		__field(int, mapcount)
		__field(void *, mapping)
		__field(int, mt)
		__field(const char *, reason)
		__field(int, err)
		),

	TP_fast_assign(
		__entry->pfn = page_to_pfn(page);
		__entry->flags = page->flags;
		__entry->count = page_ref_count(page);
		__entry->mapcount = page_mapcount(page);
		__entry->mapping = page->mapping;
		__entry->mt = get_pageblock_migratetype(page);
		__entry->reason = reason;
		__entry->err = err;
		),

	TP_printk("pfn=0x%lx flags=%s count=%d mapcount=%d mapping=%p mt=%d reason=%s err=%d",
			__entry->pfn,
			show_page_flags(__entry->flags & ((1UL << NR_PAGEFLAGS) - 1)),
			__entry->count, __entry->mapcount, __entry->mapping,
			__entry->mt, __entry->reason, __entry->err)
);

#endif /* _TRACE_PAGE_PIN_OWNER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
