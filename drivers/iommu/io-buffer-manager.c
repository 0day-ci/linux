// SPDX-License-Identifier: GPL-2.0-only
/*
 * Manager which allocates and tracks bounce buffers and their IOVAs. Does
 * not actually manage the IOMMU mapping nor do the bounce copies.
 *
 * The manager caches recently used bounce buffers. The cache is initialized
 * with a fixed number of slots, which allows for cache operations to be
 * performed efficiently. Slots are assigned pre-allocated IOVAs. The number
 * of slots is configurable, but is limited to 1/2 of the total IOVA range.
 *
 * If the cache fills up, or for very large allocations, the manager falls
 * back to single-use bounce buffers.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include "io-buffer-manager.h"

#include <linux/slab.h>

#define EVICT_PERIOD_MSEC 5000

struct io_buffer_node {
	struct rb_node node;
	struct io_bounce_buffer_info info;
	void *orig_buffer;
	int prot;
};

static void io_buffer_manager_free_pages(struct page **pages, int count)
{
	while (count--)
		__free_page(pages[count]);
	kfree(pages);
}

static struct page **io_buffer_manager_alloc_pages(int count, unsigned int nid)
{
	struct page **pages;
	unsigned int i;

	pages = kmalloc_array(count, sizeof(*pages), GFP_ATOMIC);
	if (!pages)
		return NULL;

	// The IOMMU can map highmem pages, but try to allocate non-highmem
	// pages first to make accessing the buffer cheaper.
	for (i = 0; i < count; i++) {
		pages[i] = alloc_pages_node(
			nid, GFP_ATOMIC | __GFP_NORETRY | __GFP_NOWARN, 0);
		if (!pages[i]) {
			pages[i] = alloc_pages_node(
				nid, GFP_ATOMIC | __GFP_HIGHMEM, 0);
			if (!pages[i]) {
				io_buffer_manager_free_pages(pages, i);
				return NULL;
			}
		}
	}

	return pages;
}

static dma_addr_t io_buffer_slot_to_iova(struct io_buffer_slot *slot,
					 struct io_buffer_pool *pool)
{
	return pool->iova_base + pool->buffer_size * (slot - pool->slots);
}

static struct io_buffer_slot **
io_buffer_pool_get_cache(struct io_buffer_pool *pool, int prot)
{
	prot &= (IOMMU_READ | IOMMU_WRITE);
	if (prot == IOMMU_READ)
		return &pool->cached_slots[IO_BUFFER_SLOT_TYPE_RO];
	else if (prot == IOMMU_WRITE)
		return &pool->cached_slots[IO_BUFFER_SLOT_TYPE_WO];
	BUG_ON(prot == 0);
	return &pool->cached_slots[IO_BUFFER_SLOT_TYPE_RW];
}

/**
 * io_buffer_manager_relese_slots - release unused buffer slots
 * @to_free: head of list of slots to free
 * @head: outparam of head of list of slots that were freed
 * @tail_link: outparam for next ptr of tail of list of freed slots
 *
 * Frees slots that are evicted from cache. May leak slots if an
 * error occurs while freeing slot resources.
 */
static void io_buffer_manager_relese_slots(struct io_buffer_manager *manager,
					   struct io_buffer_pool *pool,
					   struct io_buffer_slot *to_free,
					   struct io_buffer_slot **head,
					   struct io_buffer_slot ***tail_link)
{
	struct io_buffer_slot *tmp, **prev_link;

	*head = to_free;
	prev_link = head;

	while ((tmp = *prev_link)) {
		dma_addr_t iova = io_buffer_slot_to_iova(tmp, pool);

		if (io_bounce_buffers_release_buffer_cb(manager, iova,
							pool->buffer_size)) {
			io_buffer_manager_free_pages(tmp->bounce_buffer,
						     pool->buffer_size >>
							     PAGE_SHIFT);
		} else {
			// If freeing fails, the iova is in an unknown state.
			// Remove it from the list of slots being freed.
			pr_warn("Bounce buffer release failed; leaking slot\n");
			*prev_link = tmp->next;
		}
		prev_link = &tmp->next;
	}

	*tail_link = prev_link;
}

static void __io_buffer_manager_evict(struct io_buffer_manager *manager,
				      bool pool_teardown)
{
	struct io_buffer_pool *pool;
	struct io_buffer_slot **prev_link, *to_free;
	unsigned long flags;
	int i, j;
	bool requeue = false;

	for (i = 0; i < NUM_POOLS; i++) {
		pool = &manager->pools[i];

		spin_lock_irqsave(&pool->lock, flags);
		for (j = 0; j < IO_BUFFER_SLOT_TYPE_COUNT; j++) {
			prev_link = &pool->cached_slots[j];

			if (pool_teardown) {
				to_free = *prev_link;
			} else {
				while ((to_free = *prev_link)) {
					if (to_free->old_cache_entry) {
						*prev_link = NULL;
						break;
					}
					requeue = true;
					to_free->old_cache_entry = true;
					prev_link = &to_free->next;
				}
			}
			if (!to_free)
				continue;

			spin_unlock_irqrestore(&pool->lock, flags);

			io_buffer_manager_relese_slots(manager, pool, to_free,
						       &to_free, &prev_link);

			spin_lock_irqsave(&pool->lock, flags);
			if (to_free) {
				*prev_link = pool->empty_slots;
				pool->empty_slots = to_free;
			}
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	if (requeue)
		queue_delayed_work(manager->evict_wq, &manager->evict_work,
				   msecs_to_jiffies(EVICT_PERIOD_MSEC));
}

struct io_buffer_node *find_fallback_node(struct rb_root *root, dma_addr_t iova)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct io_buffer_node *cur =
			container_of(node, struct io_buffer_node, node);

		if (iova < cur->info.iova)
			node = node->rb_left;
		else if (iova >= cur->info.iova + cur->info.size)
			node = node->rb_right;
		else
			return cur;
	}
	return NULL;
}

bool insert_fallback_node(struct rb_root *root, struct io_buffer_node *node)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	dma_addr_t node_end = node->info.iova + node->info.size;

	while (*new) {
		struct io_buffer_node *cur =
			container_of(*new, struct io_buffer_node, node);
		dma_addr_t cur_end = cur->info.iova + cur->info.size;

		parent = *new;
		if (node_end <= cur->info.iova)
			new = &((*new)->rb_left);
		else if (node->info.iova >= cur_end)
			new = &((*new)->rb_right);
		else {
			pr_crit("IOVA collision new=[%llx,%llx) old=[%llx,%llx)\n",
				node->info.iova, node_end, cur->info.iova,
				cur_end);
			return false;
		}
	}

	rb_link_node(&node->node, parent, new);
	rb_insert_color(&node->node, root);
	return true;
}

static void io_buffer_manager_evict(struct work_struct *work)
{
	struct io_buffer_manager *manager = container_of(
		to_delayed_work(work), struct io_buffer_manager, evict_work);
	__io_buffer_manager_evict(manager, false);
}

static void fill_buffer_info(struct io_buffer_slot *slot,
			     struct io_buffer_pool *pool,
			     struct io_bounce_buffer_info *info)
{
	info->bounce_buffer = slot->bounce_buffer;
	info->iova = io_buffer_slot_to_iova(slot, pool);
	info->size = pool->buffer_size;
}

static bool io_buffer_pool_has_empty_slot(struct io_buffer_pool *pool,
					  int num_slots)
{
	if (pool->empty_slots)
		return true;

	if (!pool->slots) {
		pool->slots = kmalloc_array(num_slots, sizeof(*pool->slots),
					    GFP_ATOMIC);
		if (!pool->slots)
			return false;
	}

	if (pool->untouched_slot_idx < num_slots) {
		struct io_buffer_slot *slot =
			&pool->slots[pool->untouched_slot_idx++];
		memset(slot, 0, sizeof(*slot));
		pool->empty_slots = slot;
	}

	return !!pool->empty_slots;
}

static bool io_buffer_manager_alloc_slot(struct io_buffer_manager *manager,
					 void *orig_buffer, size_t size,
					 int prot, unsigned int nid,
					 struct io_bounce_buffer_info *info,
					 bool *new_buffer)
{
	struct io_buffer_slot *slot = NULL, **prev_link, *cur;
	struct io_buffer_pool *pool = NULL;
	unsigned long flags;
	dma_addr_t iova;
	int pool_idx;

	if (!manager->num_slots)
		return false;

	// Compute the power-of-2 size buffer needed, and then the pool idx.
	pool_idx = roundup_pow_of_two(ALIGN(size, PAGE_SIZE));
	pool_idx = fls(pool_idx >> PAGE_SHIFT) - 1;
	if (pool_idx >= NUM_POOLS)
		return false;
	pool = manager->pools + pool_idx;

	spin_lock_irqsave(&pool->lock, flags);

	prev_link = io_buffer_pool_get_cache(pool, prot);
	while ((cur = *prev_link)) {
		if (cur->prot == prot) {
			slot = cur;
			*prev_link = cur->next;
			break;
		}
		prev_link = &cur->next;
	}

	*new_buffer = slot == NULL;
	if (*new_buffer) {
		if (!io_buffer_pool_has_empty_slot(pool, manager->num_slots)) {
			spin_unlock_irqrestore(&pool->lock, flags);
			return false;
		}

		slot = pool->empty_slots;
		pool->empty_slots = slot->next;
		spin_unlock_irqrestore(&pool->lock, flags);

		iova = io_buffer_slot_to_iova(slot, pool);

		slot->bounce_buffer = io_buffer_manager_alloc_pages(
			pool->buffer_size >> PAGE_SHIFT, nid);
		if (!slot->bounce_buffer) {
			spin_lock_irqsave(&pool->lock, flags);
			slot->next = pool->empty_slots;
			pool->empty_slots = slot;
			spin_unlock_irqrestore(&pool->lock, flags);
			return false;
		}
	} else {
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	slot->orig_buffer = orig_buffer;
	slot->prot = prot;

	fill_buffer_info(slot, pool, info);
	return true;
}

bool io_buffer_manager_alloc_buffer(struct io_buffer_manager *manager,
				    struct device *dev, void *orig_buffer,
				    size_t size, int prot, unsigned int nid,
				    struct io_bounce_buffer_info *info,
				    bool *new_buffer)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	struct io_buffer_node *node;
	unsigned long flags;

	if (io_buffer_manager_alloc_slot(manager, orig_buffer, size, prot,
					 nid, info, new_buffer))
		return true;

	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node)
		return false;

	size = PAGE_ALIGN(size);
	node->info.iova =
		__iommu_dma_alloc_iova(domain, size, dma_get_mask(dev), dev);
	if (!node->info.iova)
		goto free_node;

	node->info.bounce_buffer =
		io_buffer_manager_alloc_pages(size >> PAGE_SHIFT, nid);
	if (!node->info.bounce_buffer)
		goto free_iova;

	spin_lock_irqsave(&manager->fallback_lock, flags);
	if (!insert_fallback_node(&manager->fallback_buffers, node))
		goto fallback_lock_unlock;
	spin_unlock_irqrestore(&manager->fallback_lock, flags);

	node->orig_buffer = orig_buffer;
	node->prot = prot;
	node->info.size = size;

	*info = node->info;
	*new_buffer = true;

	return true;

fallback_lock_unlock:
	spin_unlock_irqrestore(&manager->fallback_lock, flags);
free_iova:
	__iommu_dma_free_iova(domain->iova_cookie, node->info.iova, size, NULL);
free_node:
	kfree(node);
	return false;
}

static bool __io_buffer_manager_find_slot(struct io_buffer_manager *manager,
					  dma_addr_t handle,
					  struct io_buffer_pool **pool,
					  struct io_buffer_slot **slot)
{
	size_t i;
	dma_addr_t iova_end = manager->iova + manager->iova_size;

	if (!manager->num_slots || handle < manager->iova || handle >= iova_end)
		return false;

	// Pools are ordered from largest to smallest, and each pool is twice
	// as large as the next pool. Find how far from the end of the overall
	// allocation the handle is in terms of the size of the iova range
	// assigned to the smallest pool (1-indexed), and then compute the idx.
	i = ALIGN(iova_end - handle, PAGE_SIZE) >> PAGE_SHIFT;
	i = DIV_ROUND_UP(i, manager->num_slots);
	i = fls(i) - 1;

	*pool = manager->pools + i;
	*slot = (*pool)->slots +
		(handle - (*pool)->iova_base) / (*pool)->buffer_size;

	return true;
}

bool io_buffer_manager_find_buffer(struct io_buffer_manager *manager,
				   dma_addr_t handle,
				   struct io_bounce_buffer_info *info,
				   void **orig_buffer, int *prot)
{
	struct io_buffer_pool *pool;
	struct io_buffer_slot *slot;
	struct io_buffer_node *node;
	unsigned long flags;

	if (__io_buffer_manager_find_slot(manager, handle, &pool, &slot)) {
		fill_buffer_info(slot, pool, info);
		*orig_buffer = slot->orig_buffer;
		*prot = slot->prot;
		return true;
	}

	spin_lock_irqsave(&manager->fallback_lock, flags);
	node = find_fallback_node(&manager->fallback_buffers, handle);
	spin_unlock_irqrestore(&manager->fallback_lock, flags);

	if (!node)
		return false;

	*info = node->info;
	*orig_buffer = node->orig_buffer;
	*prot = node->prot;
	return true;
}

bool io_buffer_manager_release_buffer(struct io_buffer_manager *manager,
				      struct iommu_domain *domain,
				      dma_addr_t handle, bool inited,
				      prerelease_cb cb, void *ctx)
{
	struct io_buffer_slot *slot, **cache;
	struct io_buffer_pool *pool;
	struct io_buffer_node *node;
	unsigned long flags;
	bool free_buffer;

	if (__io_buffer_manager_find_slot(manager, handle, &pool, &slot)) {
		if (cb) {
			struct io_bounce_buffer_info info;

			fill_buffer_info(slot, pool, &info);
			cb(&info, slot->prot, slot->orig_buffer, ctx);
		}

		spin_lock_irqsave(&pool->lock, flags);

		if (likely(inited)) {
			cache = io_buffer_pool_get_cache(pool, slot->prot);
			if (*cache == NULL)
				queue_delayed_work(
					manager->evict_wq, &manager->evict_work,
					msecs_to_jiffies(EVICT_PERIOD_MSEC));

			slot->orig_buffer = NULL;
			slot->next = *cache;
			*cache = slot;
			slot->old_cache_entry = false;
		} else {
			io_buffer_manager_free_pages(slot->bounce_buffer,
						     pool->buffer_size >>
							     PAGE_SHIFT);
			slot->next = pool->empty_slots;
			pool->empty_slots = slot;
		}

		spin_unlock_irqrestore(&pool->lock, flags);
		return true;
	}

	spin_lock_irqsave(&manager->fallback_lock, flags);
	node = find_fallback_node(&manager->fallback_buffers, handle);
	if (node)
		rb_erase(&node->node, &manager->fallback_buffers);
	spin_unlock_irqrestore(&manager->fallback_lock, flags);

	if (!node)
		return false;

	if (cb)
		cb(&node->info, node->prot, node->orig_buffer, ctx);

	if (inited)
		free_buffer = io_bounce_buffers_release_buffer_cb(
			manager, node->info.iova, node->info.size);
	else
		free_buffer = true;

	if (free_buffer) {
		io_buffer_manager_free_pages(node->info.bounce_buffer,
					     node->info.size >> PAGE_SHIFT);
		__iommu_dma_free_iova(domain->iova_cookie, node->info.iova,
				      node->info.size, NULL);
	} else {
		pr_warn("Bounce buffer release failed; leaking buffer\n");
	}

	kfree(node);
	return true;
}

void io_buffer_manager_destroy(struct io_buffer_manager *manager,
			       struct iommu_domain *domain)
{
	int i;

	if (!manager->num_slots)
		return;

	cancel_delayed_work_sync(&manager->evict_work);
	destroy_workqueue(manager->evict_wq);
	__io_buffer_manager_evict(manager, true);
	__iommu_dma_free_iova(domain->iova_cookie, manager->iova,
			      manager->iova_size, NULL);

	for (i = 0; i < NUM_POOLS; i++)
		kfree(manager->pools[i].slots);
}

bool io_buffer_manager_reinit_check(struct io_buffer_manager *manager,
				    struct device *dev,
				    struct iova_domain *iovad, dma_addr_t base,
				    dma_addr_t limit)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	u64 dma_limit = __iommu_dma_limit(domain, dev, dma_get_mask(dev));
	dma_addr_t start_iova = iovad->start_pfn << iovad->granule;

	if (!manager->num_slots)
		return true;

	if (base > manager->iova ||
	    limit < manager->iova + manager->iova_size) {
		pr_warn("Bounce buffer pool out of range\n");
		return false;
	}

	if (~dma_limit & (manager->iova + manager->iova_size - 1)) {
		pr_warn("Bounce buffer pool larger than dma limit\n");
		return false;
	}

	if (manager->iova_size > (dma_limit - start_iova) / 2)
		pr_info("Bounce buffer pool using >1/2 of iova range\n");

	return true;
}

int io_buffer_manager_init(struct io_buffer_manager *manager,
			   struct device *dev, struct iova_domain *iovad,
			   unsigned int num_slots)
{
	struct iommu_domain *domain = iommu_get_dma_domain(dev);
	int i;
	unsigned int old_num_slots = num_slots;
	size_t reserved_iova_pages, pages_per_slot, max_reserved_iova_pages;
	dma_addr_t iova_base;
	u64 dma_limit, start_iova;

	manager->fallback_buffers = RB_ROOT;
	spin_lock_init(&manager->fallback_lock);

	if (num_slots == 0)
		return 0;

	INIT_DELAYED_WORK(&manager->evict_work, io_buffer_manager_evict);
	manager->evict_wq = create_singlethread_workqueue("io-buffer-buffers");
	if (!manager->evict_wq)
		return -ENOMEM;

	// Make sure there are iovas left over for non-pooled buffers. The iova
	// allocation can be quite large, so also handle allocation falures due
	// to reserved iova regions.
	dma_limit = __iommu_dma_limit(domain, dev, dma_get_mask(dev));
	start_iova = iovad->start_pfn << iovad->granule;
	max_reserved_iova_pages = ((dma_limit - start_iova) / 2) >> PAGE_SHIFT;
	pages_per_slot = (1 << NUM_POOLS) - 1;
	do {
		reserved_iova_pages = pages_per_slot * num_slots;
		if (reserved_iova_pages > max_reserved_iova_pages) {
			num_slots = max_reserved_iova_pages / pages_per_slot;
			reserved_iova_pages = pages_per_slot * num_slots;
		}

		manager->iova_size = reserved_iova_pages << PAGE_SHIFT;
		manager->iova = __iommu_dma_alloc_iova(
			domain, manager->iova_size, dma_get_mask(dev), dev);
		max_reserved_iova_pages /= 2;
	} while (!manager->iova && max_reserved_iova_pages >= pages_per_slot);

	if (!manager->iova) {
		destroy_workqueue(manager->evict_wq);
		return -ENOSPC;
	} else if (num_slots < old_num_slots) {
		pr_info("Insufficient space for %u slots, limited to %u\n",
			old_num_slots, num_slots);
	}
	manager->num_slots = num_slots;

	// To ensure that no slot has a segment which crosses a segment
	// boundary, align each slot's iova to the slot's size.
	// __iommu_dma_alloc_iova aligns to roundup_power_of_two(size), which
	// is larger than the largest buffer size. Assigning iova_base from
	// largest to smallest ensures each iova_base is aligned to the
	// previous pool's larger size.
	iova_base = manager->iova;
	for (i = NUM_POOLS - 1; i >= 0; i--) {
		struct io_buffer_pool *pool = manager->pools + i;

		spin_lock_init(&pool->lock);
		pool->empty_slots = NULL;
		pool->untouched_slot_idx = 0;
		pool->buffer_size = PAGE_SIZE << i;
		pool->iova_base = iova_base;

		iova_base += num_slots * pool->buffer_size;
	}

	return 0;
}
