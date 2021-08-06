/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef _LINUX_IO_BUFFER_MANAGER_H
#define _LINUX_IO_BUFFER_MANAGER_H

#include <linux/dma-iommu.h>
#include <linux/iova.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct io_buffer_slot {
	void *orig_buffer;
	struct page **bounce_buffer;
	struct io_buffer_slot *next;
	int prot;
	bool old_cache_entry;
};

enum io_buffer_slot_type {
	IO_BUFFER_SLOT_TYPE_RO = 0,
	IO_BUFFER_SLOT_TYPE_WO,
	IO_BUFFER_SLOT_TYPE_RW,
	IO_BUFFER_SLOT_TYPE_COUNT,
};

struct io_buffer_pool {
	struct io_buffer_slot *cached_slots[IO_BUFFER_SLOT_TYPE_COUNT];
	struct io_buffer_slot *empty_slots;
	unsigned int untouched_slot_idx;
	spinlock_t lock;
	dma_addr_t iova_base;
	size_t buffer_size;
	struct io_buffer_slot *slots;
};

#define NUM_POOLS 8

struct io_buffer_manager {
	struct workqueue_struct *evict_wq;
	struct delayed_work evict_work;
	unsigned int num_slots;
	spinlock_t fallback_lock;
	struct rb_root fallback_buffers;
	struct io_buffer_pool pools[NUM_POOLS];
	dma_addr_t iova;
	size_t iova_size;
};

struct io_bounce_buffer_info {
	struct page **bounce_buffer;
	dma_addr_t iova;
	size_t size;
};

bool io_buffer_manager_alloc_buffer(struct io_buffer_manager *manager,
				    struct device *dev, void *orig_buffer,
				    size_t size, int prot, bool use_fallback,
				    unsigned int nid,
				    struct io_bounce_buffer_info *info,
				    bool *new_buffer);

bool io_buffer_manager_find_buffer(struct io_buffer_manager *manager,
				   dma_addr_t handle, bool may_use_fallback,
				   struct io_bounce_buffer_info *info,
				   void **orig_buffer, int *prot);

typedef void (*prerelease_cb)(struct io_bounce_buffer_info *info, int prot,
			      void *orig_buffer, void *ctx);

bool io_buffer_manager_release_buffer(struct io_buffer_manager *manager,
				      struct iommu_domain *domain,
				      dma_addr_t handle, bool inited,
				      bool may_use_fallback, prerelease_cb cb,
				      void *ctx);

int io_buffer_manager_init(struct io_buffer_manager *manager,
			   struct device *dev, struct iova_domain *iovad,
			   unsigned int num_slots);

bool io_buffer_manager_reinit_check(struct io_buffer_manager *manager,
				    struct device *dev,
				    struct iova_domain *iovad, dma_addr_t base,
				    dma_addr_t limit);

void io_buffer_manager_destroy(struct io_buffer_manager *manager,
			       struct iommu_domain *domain);

bool io_bounce_buffers_release_buffer_cb(struct io_buffer_manager *manager,
					 dma_addr_t iova, size_t size);

#endif /* _LINUX_IO_BUFFER_MANAGER_H */
