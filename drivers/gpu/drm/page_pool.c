// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Sharable page pool implementation
 *
 * Extracted from drivers/gpu/drm/ttm/ttm_pool.c
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2021 Linaro Ltd.
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

#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/shrinker.h>
#include <drm/page_pool.h>

static unsigned long page_pool_size; /* max size of the pool */

MODULE_PARM_DESC(page_pool_size, "Number of pages in the drm page pool");
module_param(page_pool_size, ulong, 0644);

static atomic_long_t nr_managed_pages;

static struct mutex shrinker_lock;
static struct list_head shrinker_list;
static struct shrinker mm_shrinker;

/**
 * drm_page_pool_set_max - Sets maximum size of all pools
 *
 * Sets the maximum number of pages allows in all pools.
 * This can only be set once, and the first caller wins.
 */
void drm_page_pool_set_max(unsigned long max)
{
	if (!page_pool_size)
		page_pool_size = max;
}

/**
 * drm_page_pool_get_max - Maximum size of all pools
 *
 * Return the maximum number of pages allows in all pools
 */
unsigned long drm_page_pool_get_max(void)
{
	return page_pool_size;
}

/**
 * drm_page_pool_get_total - Current size of all pools
 *
 * Return the number of pages in all managed pools
 */
unsigned long drm_page_pool_get_total(void)
{
	return atomic_long_read(&nr_managed_pages);
}

/**
 * drm_page_pool_get_size - Get the number of pages in the pool
 *
 * @pool: Pool to calculate the size of
 *
 * Return the number of pages in the specified pool
 */
unsigned long drm_page_pool_get_size(struct drm_page_pool *pool)
{
	unsigned long size;

	spin_lock(&pool->lock);
	size = pool->page_count;
	spin_unlock(&pool->lock);
	return size;
}

/**
 * drm_page_pool_add - Add a page to a pool
 *
 * @pool: Pool to add page to
 * @page: Page to be added to the pool
 *
 * Gives specified page into a specific pool
 */
void drm_page_pool_add(struct drm_page_pool *pool, struct page *p)
{
	unsigned int i, num_pages = 1 << pool->order;

	/* Make sure we won't grow larger then the max pool size */
	if (page_pool_size &&
	       ((drm_page_pool_get_total()) + num_pages > page_pool_size)) {
		pool->free(pool, p);
		return;
	}

	/* Be sure to zero pages before adding them to the pool */
	for (i = 0; i < num_pages; ++i) {
		if (PageHighMem(p))
			clear_highpage(p + i);
		else
			clear_page(page_address(p + i));
	}

	spin_lock(&pool->lock);
	list_add(&p->lru, &pool->pages);
	pool->page_count += 1 << pool->order;
	spin_unlock(&pool->lock);
	atomic_long_add(1 << pool->order, &nr_managed_pages);

}

/**
 * drm_page_pool_remove - Remove page from pool
 *
 * @pool: Pool to pull the page from
 *
 * Take pages from a specific pool, return NULL when nothing available
 */
struct page *drm_page_pool_remove(struct drm_page_pool *pool)
{
	struct page *p;

	spin_lock(&pool->lock);
	p = list_first_entry_or_null(&pool->pages, typeof(*p), lru);
	if (p) {
		atomic_long_sub(1 << pool->order, &nr_managed_pages);
		pool->page_count -= 1 << pool->order;
		list_del(&p->lru);
	}
	spin_unlock(&pool->lock);

	return p;
}

/**
 * drm_page_pool_init - Initialize a pool
 *
 * @pool: the pool to initialize
 * @order: page allocation order
 * @free_page: function pointer to free the pool's pages
 *
 * Initialize and add a pool type to the global shrinker list
 */
void drm_page_pool_init(struct drm_page_pool *pool, unsigned int order,
			void (*free_page)(struct drm_page_pool *pool, struct page *p))
{
	pool->order = order;
	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->pages);
	pool->free = free_page;
	pool->page_count = 0;

	mutex_lock(&shrinker_lock);
	list_add_tail(&pool->shrinker_list, &shrinker_list);
	mutex_unlock(&shrinker_lock);
}

/**
 * drm_page_pool_fini - Cleanup a pool
 *
 * @pool: the pool to clean up
 *
 * Remove a pool_type from the global shrinker list and free all pages
 */
void drm_page_pool_fini(struct drm_page_pool *pool)
{
	struct page *p;

	mutex_lock(&shrinker_lock);
	list_del(&pool->shrinker_list);
	mutex_unlock(&shrinker_lock);

	while ((p = drm_page_pool_remove(pool)))
		pool->free(pool, p);
}

/**
 * drm_page_pool_shrink - Shrink the drm page pool
 *
 * Free pages using the global shrinker list. Returns
 * the number of pages free
 */
unsigned int drm_page_pool_shrink(void)
{
	struct drm_page_pool *pool;
	unsigned int num_freed;
	struct page *p;

	mutex_lock(&shrinker_lock);
	pool = list_first_entry(&shrinker_list, typeof(*pool), shrinker_list);

	p = drm_page_pool_remove(pool);
	if (p) {
		pool->free(pool, p);
		num_freed = 1 << pool->order;
	} else {
		num_freed = 0;
	}

	list_move_tail(&pool->shrinker_list, &shrinker_list);
	mutex_unlock(&shrinker_lock);

	return num_freed;
}

/* As long as pages are available make sure to release at least one */
static unsigned long drm_page_pool_shrinker_scan(struct shrinker *shrink,
						 struct shrink_control *sc)
{
	unsigned long num_freed = 0;

	do
		num_freed += drm_page_pool_shrink();
	while (!num_freed && atomic_long_read(&nr_managed_pages));

	return num_freed;
}

/* Return the number of pages available or SHRINK_EMPTY if we have none */
static unsigned long drm_page_pool_shrinker_count(struct shrinker *shrink,
						  struct shrink_control *sc)
{
	unsigned long num_pages = atomic_long_read(&nr_managed_pages);

	return num_pages ? num_pages : SHRINK_EMPTY;
}

/**
 * dma_page_pool_lock_shrinker - Take the shrinker lock
 *
 * Takes the shrinker lock, preventing the shrinker from making
 * changes to the pools
 */
void dma_page_pool_lock_shrinker(void)
{
	mutex_lock(&shrinker_lock);
}

/**
 * dma_page_pool_unlock_shrinker - Release the shrinker lock
 *
 * Releases the shrinker lock, allowing the shrinker to free
 * pages
 */
void dma_page_pool_unlock_shrinker(void)
{
	mutex_unlock(&shrinker_lock);
}

/**
 * drm_page_pool_shrinker_init - Initialize globals
 *
 * Initialize the global locks and lists for the shrinker.
 */
static int drm_page_pool_shrinker_init(void)
{
	mutex_init(&shrinker_lock);
	INIT_LIST_HEAD(&shrinker_list);

	mm_shrinker.count_objects = drm_page_pool_shrinker_count;
	mm_shrinker.scan_objects = drm_page_pool_shrinker_scan;
	mm_shrinker.seeks = 1;
	return register_shrinker(&mm_shrinker);
}

/**
 * drm_page_pool_shrinker_fini - Finalize globals
 *
 * Unregister the shrinker.
 */
static void drm_page_pool_shrinker_fini(void)
{
	unregister_shrinker(&mm_shrinker);
	WARN_ON(!list_empty(&shrinker_list));
}

module_init(drm_page_pool_shrinker_init);
module_exit(drm_page_pool_shrinker_fini);
MODULE_LICENSE("Dual MIT/GPL");
