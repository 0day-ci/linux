/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_KASAN_REPORT_TAGS_H
#define __MM_KASAN_REPORT_TAGS_H

#include "kasan.h"
#include "../slab.h"

#ifdef CONFIG_KASAN_TAGS_IDENTIFY
const char *kasan_get_bug_type(struct kasan_access_info *info)
{
	struct kasan_alloc_meta *alloc_meta;
	struct kmem_cache *cache;
	struct page *page;
	const void *addr;
	void *object;
	u8 tag;
	int i;

	tag = get_tag(info->access_addr);
	addr = kasan_reset_tag(info->access_addr);
	page = kasan_addr_to_page(addr);
	if (page && PageSlab(page)) {
		cache = page->slab_cache;
		object = nearest_obj(cache, page, (void *)addr);
		alloc_meta = kasan_get_alloc_meta(cache, object);

		if (alloc_meta) {
			for (i = 0; i < KASAN_NR_FREE_STACKS; i++) {
				if (alloc_meta->free_pointer_tag[i] == tag)
					return "use-after-free";
			}
		}
		return "out-of-bounds";
	}

	/*
	 * If access_size is a negative number, then it has reason to be
	 * defined as out-of-bounds bug type.
	 *
	 * Casting negative numbers to size_t would indeed turn up as
	 * a large size_t and its value will be larger than ULONG_MAX/2,
	 * so that this can qualify as out-of-bounds.
	 */
	if (info->access_addr + info->access_size < info->access_addr)
		return "out-of-bounds";

	return "invalid-access";
}
#else
const char *kasan_get_bug_type(struct kasan_access_info *info)
{
	return "invalid-access";
}
#endif

#endif
