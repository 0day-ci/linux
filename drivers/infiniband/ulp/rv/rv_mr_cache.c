// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/debugfs.h>
#include <linux/interval_tree_generic.h>

#include <rdma/ib_user_sa.h>

#include "rv.h"
#include "trace.h"

static unsigned int mr_cache_size = MAX_RB_SIZE;

module_param(mr_cache_size, uint, 0444);
MODULE_PARM_DESC(mr_cache_size, "Size of mr cache (in MB)");

static void handle_remove(struct work_struct *work);
static void do_remove(struct rv_mr_cache *cache, struct list_head *del_list);
static u32 rv_cache_evict(struct rv_mr_cache *cache, u64 mbytes);
static int mmu_notifier_range_start(struct mmu_notifier *,
				    const struct mmu_notifier_range *);
static struct rv_mr_cached *rv_mr_cache_search(struct rv_mr_cache *cache,
					       u64 addr, u64 len, u32 acc);
static void rv_update_mrc_stats_add(struct rv_mr_cache *cache,
				    struct rv_mr_cached *mrc);
static void rv_update_mrc_stats_remove(struct rv_mr_cache *cache,
				       struct rv_mr_cached *mrc);

static const struct mmu_notifier_ops mn_opts = {
	.invalidate_range_start = mmu_notifier_range_start,
};

static u64 mrc_start(struct rv_mr_cached *mrc)
{
	return mrc->addr;
}

static u64 mrc_last(struct rv_mr_cached *mrc)
{
	return mrc->addr + mrc->len - 1;
}

INTERVAL_TREE_DEFINE(struct rv_mr_cached, node, u64, __last,
		     mrc_start, mrc_last, static, rv_int_rb);

/*
 * MMU notifier callback
 *
 * If the address range overlaps an MR which is in use (refcount>0)
 * we refuse to remove it.  Otherwise we remove it from the MR cache
 * by getting it off the LRU list and RB-tree and schedule the
 * MR deregistration.
 */
static int mmu_notifier_range_start(struct mmu_notifier *mn,
				    const struct mmu_notifier_range *range)
{
	struct rv_mr_cache *cache = container_of(mn, struct rv_mr_cache, mn);
	struct rb_root_cached *root = &cache->root;
	struct rv_mr_cached *mrc, *ptr = NULL;
	unsigned long flags;
	bool added = false;

	spin_lock_irqsave(&cache->lock, flags);
	for (mrc = rv_int_rb_iter_first(root, range->start, range->end - 1);
	     mrc; mrc = ptr) {
		ptr = rv_int_rb_iter_next(mrc, range->start, range->end - 1);
		if (cache->ops->invalidate(cache, cache->ops_arg, mrc)) {
			trace_rv_mr_cache_notifier(mrc->addr, mrc->len,
						   mrc->access);
			rv_int_rb_remove(mrc, root);
			list_move(&mrc->list, &cache->del_list);
			cache->stats.remove++;
			rv_update_mrc_stats_remove(cache, mrc);
			added = true;
		}
	}
	spin_unlock_irqrestore(&cache->lock, flags);

	if (added)
		queue_work(cache->wq, &cache->del_work);
	return 0;
}

/* MR deregistration is done on a per rv_user work queue.  */
int rv_mr_cache_init(int rv_inx, struct rv_mr_cache *cache,
		     const struct rv_mr_cache_ops *ops, void *priv,
		     struct mm_struct *mm, u32 cache_size)
{
	char wq_name[25];
	int ret = 0;

	sprintf(wq_name, "rv-%d\n", rv_inx);
	cache->wq = alloc_workqueue(wq_name,
				    WQ_SYSFS | WQ_HIGHPRI | WQ_CPU_INTENSIVE
					| WQ_MEM_RECLAIM,
				    RV_RB_MAX_ACTIVE_WQ_ENTRIES);
	if (!cache->wq)
		return -ENOMEM;

	trace_rv_mr_cache_wq_alloc(wq_name);
	cache->root = RB_ROOT_CACHED;
	cache->ops = ops;
	cache->ops_arg = priv;

	INIT_HLIST_NODE(&cache->mn.hlist);
	spin_lock_init(&cache->lock);

	cache->mn.ops = &mn_opts;
	cache->mm = mm;

	INIT_WORK(&cache->del_work, handle_remove);
	INIT_LIST_HEAD(&cache->del_list);
	INIT_LIST_HEAD(&cache->lru_list);

	if (cache_size)
		cache->max_size = (u64)cache_size * 1024 * 1024;
	else
		cache->max_size = (u64)mr_cache_size * 1024 * 1024;

	if (mm) {
		ret = mmu_notifier_register(&cache->mn, cache->mm);
		if (ret)
			goto bail_wq;
	}

	return ret;

bail_wq:
	destroy_workqueue(cache->wq);
	cache->wq = NULL;
	return ret;
}

/* All remaining entries in the cache are deregistered */
void rv_mr_cache_deinit(int rv_inx, struct rv_mr_cache *cache)
{
	struct rv_mr_cached *mrc;
	struct rb_node *node;
	unsigned long flags;
	struct list_head del_list;

	if (cache->mm)
		mmu_notifier_unregister(&cache->mn, cache->mm);

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&cache->lock, flags);
	while ((node = rb_first_cached(&cache->root))) {
		mrc = rb_entry(node, struct rv_mr_cached, node);
		trace_rv_mr_cache_deinit(mrc->addr, mrc->len, mrc->access,
					 atomic_read(&mrc->refcount));
		rb_erase_cached(node, &cache->root);
		list_move(&mrc->list, &del_list);
		cache->stats.remove++;
		rv_update_mrc_stats_remove(cache, mrc);
	}
	WARN_ON(cache->total_size);

	spin_unlock_irqrestore(&cache->lock, flags);

	do_remove(cache, &del_list);

	if (cache->wq) {
		char wq_name[25];

		sprintf(wq_name, "rv-%d\n", rv_inx);
		trace_rv_mr_cache_wq_destroy(wq_name);
		flush_workqueue(cache->wq);
		destroy_workqueue(cache->wq);
	}
	cache->wq = NULL;
	cache->mm = NULL;
}

/* called with cache->lock */
void rv_mr_cache_update_stats_max(struct rv_mr_cache *cache, int refcount)
{
	if ((u32)refcount > cache->stats.max_refcount)
		cache->stats.max_refcount = (u32)refcount;
	if (cache->stats.inuse > cache->stats.max_inuse)
		cache->stats.max_inuse = cache->stats.inuse;
	if (cache->stats.inuse_bytes > cache->stats.max_inuse_bytes)
		cache->stats.max_inuse_bytes = cache->stats.inuse_bytes;
	if (cache->stats.count > cache->stats.max_count)
		cache->stats.max_count = cache->stats.count;
	if (cache->total_size > cache->stats.max_cache_size)
		cache->stats.max_cache_size = cache->total_size;
}

/* gets a reference to mrc on behalf of caller */
int rv_mr_cache_insert(struct rv_mr_cache *cache,
		       struct rv_mr_cached *mrc)
{
	struct rv_mr_cached *existing;
	unsigned long flags;
	u64 new_len, evict_len;
	int ret = 0;

again:
	trace_rv_mr_cache_insert(mrc->addr, mrc->len, mrc->access);

	spin_lock_irqsave(&cache->lock, flags);
	existing = rv_mr_cache_search(cache, mrc->addr, mrc->len, mrc->access);
	if (existing) {
		ret = -EINVAL;
		goto unlock;
	}
	new_len = cache->total_size + mrc->len;
	if (new_len > cache->max_size) {
		spin_unlock_irqrestore(&cache->lock, flags);

		trace_rv_mr_cache_cache_full(cache->max_size, cache->total_size,
					     mrc->len);

		evict_len = new_len - cache->max_size;
		if (rv_cache_evict(cache, evict_len) >= evict_len)
			goto again;
		spin_lock_irqsave(&cache->lock, flags);
		cache->stats.full++;
		spin_unlock_irqrestore(&cache->lock, flags);
		return -ENOMEM;
	}

	rv_int_rb_insert(mrc, &cache->root);
	INIT_LIST_HEAD(&mrc->list);

	cache->ops->get(cache, cache->ops_arg, mrc);
	cache->stats.miss++;
	rv_update_mrc_stats_add(cache, mrc);
unlock:
	spin_unlock_irqrestore(&cache->lock, flags);
	return ret;
}

/* Caller must hold cache->lock */
static struct rv_mr_cached *rv_mr_cache_search(struct rv_mr_cache *cache,
					       u64 addr, u64 len, u32 acc)
{
	struct rv_mr_cached *mrc = NULL;

	trace_rv_mr_cache_search_enter(addr, len, acc);

	if (!cache->ops->filter) {
		mrc = rv_int_rb_iter_first(&cache->root, addr,
					   (addr + len) - 1);
		if (mrc)
			trace_rv_mr_cache_search_mrc(mrc->addr, mrc->len,
						     mrc->access);
	} else {
		for (mrc = rv_int_rb_iter_first(&cache->root, addr,
						(addr + len) - 1);
		     mrc;
		     mrc = rv_int_rb_iter_next(mrc, addr, (addr + len) - 1)) {
			trace_rv_mr_cache_search_mrc(mrc->addr, mrc->len,
						     mrc->access);
			if (cache->ops->filter(mrc, addr, len, acc))
				return mrc;
		}
	}
	return mrc;
}

/* look for a cache hit.  If get a hit, make sure removed from LRU list */
struct rv_mr_cached *rv_mr_cache_search_get(struct rv_mr_cache *cache,
					    u64 addr, u64 len, u32 acc,
					    bool update_hit)
{
	unsigned long flags;
	struct rv_mr_cached *mrc;

	spin_lock_irqsave(&cache->lock, flags);
	mrc =  rv_mr_cache_search(cache, addr, len, acc);
	if (mrc) {
		cache->ops->get(cache, cache->ops_arg, mrc);
		if (update_hit)
			cache->stats.hit++;
		list_del_init(&mrc->list);
	}
	spin_unlock_irqrestore(&cache->lock, flags);

	return mrc;
}

/*
 * release a cache reference by address.
 * This is called from user ioctl, so we must make sure they don't
 * dereg twice yielding a negative refcount.
 * The released entry goes on our LRU list to prioritize evictions.
 */
struct rv_mr_cached *rv_mr_cache_search_put(struct rv_mr_cache *cache,
					    u64 addr, u64 len, u32 acc)
{
	unsigned long flags;
	struct rv_mr_cached *mrc;

	spin_lock_irqsave(&cache->lock, flags);
	mrc =  rv_mr_cache_search(cache, addr, len, acc);
	if (mrc) {
		if (!atomic_read(&mrc->refcount)) {
			mrc = NULL;
			goto unlock;
		}
		if (!cache->ops->put(cache, cache->ops_arg, mrc))
			list_add(&mrc->list, &cache->lru_list);
	}
unlock:
	spin_unlock_irqrestore(&cache->lock, flags);

	return mrc;
}

/* Simple release, the entry goes on our LRU list to prioritize evictions. */
void rv_mr_cache_put(struct rv_mr_cache *cache, struct rv_mr_cached *mrc)
{
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	if (!cache->ops->put(cache, cache->ops_arg, mrc))
		list_add(&mrc->list, &cache->lru_list);
	spin_unlock_irqrestore(&cache->lock, flags);
}

/*
 * evict entries from the cache, least recently used first.
 * We evict until we reach the goal or LRU_list is empty. Evicted
 * entries are removed from the cache and deregistered.
 */
void rv_mr_cache_evict(struct rv_mr_cache *cache, void *evict_arg)
{
	struct rv_mr_cached *mrc, *temp;
	struct list_head del_list;
	unsigned long flags;
	bool stop = false;

	INIT_LIST_HEAD(&del_list);

	spin_lock_irqsave(&cache->lock, flags);
	list_for_each_entry_safe_reverse(mrc, temp, &cache->lru_list, list) {
		if (cache->ops->evict(cache, cache->ops_arg, mrc, evict_arg,
				      &stop)) {
			trace_rv_mr_cache_evict_evict(mrc->addr, mrc->len,
						      mrc->access);
			rv_int_rb_remove(mrc, &cache->root);
			list_move(&mrc->list, &del_list);
			cache->stats.evict++;
			rv_update_mrc_stats_remove(cache, mrc);
		} else {
			trace_rv_mr_cache_evict_keep(mrc->addr, mrc->len,
						     mrc->access);
		}
		if (stop)
			break;
	}
	spin_unlock_irqrestore(&cache->lock, flags);

	do_remove(cache, &del_list);
}

/*
 * Call the remove function for the given cache and the list.  This
 * is expected to be called with a delete list extracted from cache.
 * The caller does NOT need the cache->lock.
 */
static void do_remove(struct rv_mr_cache *cache, struct list_head *del_list)
{
	struct rv_mr_cached *mrc;

	while (!list_empty(del_list)) {
		mrc = list_first_entry(del_list, struct rv_mr_cached, list);
		list_del(&mrc->list);
		rv_drv_api_dereg_mem(&mrc->mr);
		kfree(mrc);
	}
}

/*
 * Work queue function to remove all nodes that have been queued up to
 * be removed.	The key feature is that mm->mmap_lock is not being held
 * and the remove callback can sleep while taking it, if needed.
 */
static void handle_remove(struct work_struct *work)
{
	struct rv_mr_cache *cache = container_of(work, struct rv_mr_cache,
						 del_work);
	struct list_head del_list;
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	list_replace_init(&cache->del_list, &del_list);
	spin_unlock_irqrestore(&cache->lock, flags);

	do_remove(cache, &del_list);
}

static u32 rv_cache_evict(struct rv_mr_cache *cache, u64 mbytes)
{
	struct evict_data evict_data;

	evict_data.cleared = 0;
	evict_data.target = mbytes;
	trace_rv_mr_cache_cache_evict(evict_data.cleared, evict_data.target,
				      cache->total_size);
	rv_mr_cache_evict(cache, &evict_data);
	trace_rv_mr_cache_cache_evict(evict_data.cleared, evict_data.target,
				      cache->total_size);
	return evict_data.cleared;
}

static void rv_update_mrc_stats_add(struct rv_mr_cache *cache,
				    struct rv_mr_cached *mrc)
{
	cache->total_size += mrc->len;
	cache->stats.count++;
	rv_mr_cache_update_stats_max(cache, atomic_read(&mrc->refcount));
}

static void rv_update_mrc_stats_remove(struct rv_mr_cache *cache,
				       struct rv_mr_cached *mrc)
{
	cache->total_size -= mrc->len;
	cache->stats.count--;
}
