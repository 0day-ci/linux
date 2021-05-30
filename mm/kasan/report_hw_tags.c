// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains hardware tag-based KASAN specific error reporting code.
 *
 * Copyright (c) 2020 Google, Inc.
 * Author: Andrey Konovalov <andreyknvl@google.com>
 */

#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"
#include "../slab.h"

const char *kasan_get_bug_type(struct kasan_access_info *info)
{
#ifdef CONFIG_KASAN_HW_TAGS_IDENTIFY
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

#endif
	return "invalid-access";
}

void *kasan_find_first_bad_addr(void *addr, size_t size)
{
	return kasan_reset_tag(addr);
}

void kasan_metadata_fetch_row(char *buffer, void *row)
{
	int i;

	for (i = 0; i < META_BYTES_PER_ROW; i++)
		buffer[i] = hw_get_mem_tag(row + i * KASAN_GRANULE_SIZE);
}

void kasan_print_tags(u8 addr_tag, const void *addr)
{
	u8 memory_tag = hw_get_mem_tag((void *)addr);

	pr_err("Pointer tag: [%02x], memory tag: [%02x]\n",
		addr_tag, memory_tag);
}
