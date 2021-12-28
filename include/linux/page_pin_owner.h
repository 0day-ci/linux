/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_PIN_OWNER_H
#define __LINUX_PAGE_PIN_OWNER_H

#include <linux/jump_label.h>

#ifdef CONFIG_PAGE_PIN_OWNER
extern struct static_key_false page_pin_owner_inited;
extern struct page_ext_operations page_pin_owner_ops;

void __report_page_pinners(struct page *page, int reason, int err);
void __page_pin_owner_put(struct page *page);
void __reset_page_pin_owner(struct page *page, unsigned int order);

static inline void reset_page_pin_owner(struct page *page, unsigned int order)
{
	if (static_branch_unlikely(&page_pin_owner_inited))
		__reset_page_pin_owner(page, order);
}

static inline void report_page_pinners(struct page *page, int reason, int err)
{
	if (!static_branch_unlikely(&page_pin_owner_inited))
		return;

	__report_page_pinners(page, reason, err);
}

static inline void page_pin_owner_put(struct page *page)
{
	if (!static_branch_unlikely(&page_pin_owner_inited))
		return;

	__page_pin_owner_put(page);
}

#else
static inline void reset_page_pin_owner(struct page *page, unsigned int order)
{
}
static inline void report_page_pinners(struct page *page, int reason, int err)
{
}
static inline void page_pin_owner_put(struct page *page)
{
}
#endif /* CONFIG_PAGE_PIN_OWNER */
#endif /*__LINUX_PAGE_PIN_OWNER_H */
