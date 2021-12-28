// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/page_pin_owner.h>
#include <linux/migrate.h>

#define CREATE_TRACE_POINTS
#include <trace/events/page_pin_owner.h>

static bool page_pin_owner_enabled;
DEFINE_STATIC_KEY_FALSE(page_pin_owner_inited);
EXPORT_SYMBOL(page_pin_owner_inited);

static int __init early_page_pin_owner_param(char *buf)
{
	return kstrtobool(buf, &page_pin_owner_enabled);
}
early_param("page_pin_owner", early_page_pin_owner_param);

static bool need_page_pin_owner(void)
{
	return page_pin_owner_enabled;
}

static void init_page_pin_owner(void)
{
	if (!page_pin_owner_enabled)
		return;

	static_branch_enable(&page_pin_owner_inited);
}

struct page_ext_operations page_pin_owner_ops = {
	.need = need_page_pin_owner,
	.init = init_page_pin_owner,
};

void __reset_page_pin_owner(struct page *page, unsigned int order)
{
	struct page_ext *page_ext;
	int i;

	page_ext = lookup_page_ext(page);
	if (unlikely(!page_ext))
		return;

	for (i = 0; i < (1 << order); i++) {
		if (!test_bit(PAGE_EXT_PIN_OWNER, &page_ext->flags))
			break;

		clear_bit(PAGE_EXT_PIN_OWNER, &page_ext->flags);
		page_ext = page_ext_next(page_ext);
	}
}

void __report_page_pinners(struct page *page, int reason, int err)
{
	struct page_ext *page_ext;

	page_ext = lookup_page_ext(page);
	if (unlikely(!page_ext))
		return;

	test_and_set_bit(PAGE_EXT_PIN_OWNER, &page_ext->flags);
	trace_report_page_pinners(page, migrate_reason_names[reason], err);
}

void __page_pin_owner_put(struct page *page)
{
	struct page_ext *page_ext = lookup_page_ext(page);

	if (unlikely(!page_ext))
		return;

	if (!test_bit(PAGE_EXT_PIN_OWNER, &page_ext->flags))
		return;

	trace_page_pin_owner_put(page);
}
EXPORT_SYMBOL(__page_pin_owner_put);

static int __init page_pin_owner_init(void)
{
	if (!static_branch_unlikely(&page_pin_owner_inited)) {
		pr_info("page_pin_owner is disabled\n");
		return 0;
	}

	pr_info("page_pin_owner is enabled\n");
	return 0;
}
late_initcall(page_pin_owner_init)
