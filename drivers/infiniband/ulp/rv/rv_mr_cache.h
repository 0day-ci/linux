/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#ifndef __RV_MR_CACHE_H__
#define __RV_MR_CACHE_H__

#include <linux/types.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/mmu_notifier.h>
#include <linux/interval_tree_generic.h>

#define MAX_RB_SIZE 256 /* This is MB */
#define RV_RB_MAX_ACTIVE_WQ_ENTRIES 5

/*
 * The MR cache holds registered MRs and tracks reference counts for each.
 * Entries with a refcount==0 may remain in the cache and on an lru_list.
 * If the MMU notifier indicates pages would like to be freed, the entry
 * will be removed from the cache if it's refcount==0.  Otherwise there
 * are IOs in flight (app should not free memory for buffers with IOs in flight)
 * and the MMU notifier is not allowed to free the pages.
 * If a new cache entry is needed (cache miss), entries will be evicted, oldest
 * to newest based on the lru_list, until there is space for the new entry.
 *
 * max_size - limit allowed for total_size in bytes, immutable
 * ops_arg - owner context for all ops calls, immutable
 * mn - MMU notifier
 * lock - protects the RB-tree, lru_list, del_list, total_size, and stats
 * root - an RB-tree with an interval based lookup
 * total_size - current bytes in the cache
 * ops - owner callbacks for major cache events
 * mm - for MMU notifier
 * lru_list - ordered list, most recently used to least recently used
 * del_work, del_list, wq - handle deletes on a work queue
 *
 * Statistics:
 *	max_cache_size - max bytes in the cache
 *	count - Current number of MRs in the cache
 *	max_count - Maximum of count
 *	inuse - Current number of MRs with refcount > 0
 *	max_inuse - Maximum of inuse
 *	inuse_bytes - number of bytes with refcount > 0
 *	max_inuse_bytes - of inuse_bytes
 *	max_refcount - Maximum of refcount for any MR
 *	hit - Cache hit
 *	miss - Cache miss and added
 *	full - Cache miss and can't add since full
 *	evict - Removed due to lack of cache space
 *	remove - Refcount==0 & remove by mmu notifier event
 */
struct rv_mr_cache {
	u64 max_size;
	void *ops_arg;
	struct mmu_notifier mn;
	spinlock_t lock; /* See above */
	struct rb_root_cached root;
	u64 total_size;
	const struct rv_mr_cache_ops *ops;
	struct mm_struct *mm;
	struct list_head lru_list;
	struct work_struct del_work;
	struct list_head del_list;
	struct workqueue_struct *wq;

	struct {
		u64 max_cache_size;
		u32 count;
		u32 max_count;
		u32 inuse;
		u32 max_inuse;
		u64 inuse_bytes;
		u64 max_inuse_bytes;
		u32 max_refcount;
		u64 hit;
		u64 miss;
		u64 full;
		u64 evict;
		u64 remove;
	} stats;
};

/*
 * basic info about an MR
 * ib_pd - converted from user version
 * fd - converted from user provided cmd_fd
 */
struct mr_info {
	struct ib_mr *ib_mr;
	struct ib_pd *ib_pd;
	struct fd fd;
};

/* an MR entry in the MR cache RB-tree */
struct rv_mr_cached {
	struct mr_info mr;
	u64 addr;
	u64 len;
	u32 access;
	u64 __last;
	atomic_t refcount;
	struct rb_node node;
	struct list_head list;
};

/* callbacks for each major cache event */
struct rv_mr_cache_ops {
	bool (*filter)(struct rv_mr_cached *mrc, u64 addr, u64 len, u32 acc);
	void (*get)(struct rv_mr_cache *cache,
		    void *ops_arg, struct rv_mr_cached *mrc);
	int (*put)(struct rv_mr_cache *cache,
		   void *ops_arg, struct rv_mr_cached *mrc);
	int (*invalidate)(struct rv_mr_cache *cache,
			  void *ops_arg, struct rv_mr_cached *mrc);
	int (*evict)(struct rv_mr_cache *cache,
		     void *ops_arg, struct rv_mr_cached *mrc,
		     void *evict_arg, bool *stop);
};

void rv_mr_cache_update_stats_max(struct rv_mr_cache *cache,
				  int refcount);

int rv_mr_cache_insert(struct rv_mr_cache *cache, struct rv_mr_cached *mrc);

void rv_mr_cache_evict(struct rv_mr_cache *cache, void *evict_arg);

struct rv_mr_cached *rv_mr_cache_search_get(struct rv_mr_cache *cache,
					    u64 addr, u64 len, u32 acc,
					    bool update_hit);
struct rv_mr_cached *rv_mr_cache_search_put(struct rv_mr_cache *cache,
					    u64 addr, u64 len, u32 acc);
void rv_mr_cache_put(struct rv_mr_cache *cache, struct rv_mr_cached *mrc);

int rv_mr_cache_init(int rv_inx, struct rv_mr_cache *cache,
		     const struct rv_mr_cache_ops *ops, void *priv,
		     struct mm_struct *mm, u32 cache_size);
void rv_mr_cache_deinit(int rv_inx, struct rv_mr_cache *cache);

/*
 * evict operation argument
 * cleared - count evicted so far in bytes
 * target - target count to evict in bytes
 */
struct evict_data {
	u64 cleared;
	u64 target;
};

#endif /* __RV_MR_CACHE_H__ */
