/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Extracted from include/drm/ttm/ttm_pool.h
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2021 Linaro Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König, John Stultz
 */

#ifndef _DRM_PAGE_POOL_H_
#define _DRM_PAGE_POOL_H_

#include <linux/mmzone.h>
#include <linux/llist.h>
#include <linux/spinlock.h>

/**
 * drm_page_pool - Page Pool for a certain memory type
 *
 * @order: the allocation order our pages have
 * @pages: the list of pages in the pool
 * @shrinker_list: our place on the global shrinker list
 * @lock: protection of the page list
 * @page_count: number of pages currently in the pool
 * @free: Function pointer to free the page
 */
struct drm_page_pool {
	unsigned int order;
	struct list_head pages;
	struct list_head shrinker_list;
	spinlock_t lock;

	unsigned long page_count;
	void (*free)(struct drm_page_pool *pool, struct page *p);
};

void drm_page_pool_set_max(unsigned long max);
unsigned long drm_page_pool_get_max(void);
unsigned long drm_page_pool_get_total(void);
unsigned int drm_page_pool_shrink(void);
unsigned long drm_page_pool_get_size(struct drm_page_pool *pool);
void drm_page_pool_add(struct drm_page_pool *pool, struct page *p);
struct page *drm_page_pool_remove(struct drm_page_pool *pool);
void dma_page_pool_lock_shrinker(void);
void dma_page_pool_unlock_shrinker(void);
void drm_page_pool_init(struct drm_page_pool *pool, unsigned int order,
			void (*free_page)(struct drm_page_pool *pool, struct page *p));
void drm_page_pool_fini(struct drm_page_pool *pool);

#endif
