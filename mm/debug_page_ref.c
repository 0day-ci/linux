// SPDX-License-Identifier: GPL-2.0
#include <linux/mm_types.h>
#include <linux/tracepoint.h>

#define CREATE_TRACE_POINTS
#include <trace/events/page_ref.h>

void __page_ref_init(struct page *page)
{
	trace_page_ref_init(page);
}
EXPORT_SYMBOL(__page_ref_init);
EXPORT_TRACEPOINT_SYMBOL(page_ref_init);

void __page_ref_mod_and_return(struct page *page, int v, int ret)
{
	trace_page_ref_mod_and_return(page, v, ret);
}
EXPORT_SYMBOL(__page_ref_mod_and_return);
EXPORT_TRACEPOINT_SYMBOL(page_ref_mod_and_return);

void __page_ref_add_unless(struct page *page, int v, int u, int ret)
{
	trace_page_ref_add_unless(page, v, u, ret);
}
EXPORT_SYMBOL(__page_ref_add_unless);
EXPORT_TRACEPOINT_SYMBOL(page_ref_add_unless);

void __page_ref_freeze(struct page *page, int v, int ret)
{
	trace_page_ref_freeze(page, v, ret);
}
EXPORT_SYMBOL(__page_ref_freeze);
EXPORT_TRACEPOINT_SYMBOL(page_ref_freeze);

void __page_ref_unfreeze(struct page *page, int v)
{
	trace_page_ref_unfreeze(page, v);
}
EXPORT_SYMBOL(__page_ref_unfreeze);
EXPORT_TRACEPOINT_SYMBOL(page_ref_unfreeze);
