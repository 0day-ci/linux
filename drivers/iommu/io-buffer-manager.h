/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef _LINUX_IO_BUFFER_MANAGER_H
#define _LINUX_IO_BUFFER_MANAGER_H

#include <linux/dma-iommu.h>
#include <linux/iova.h>
#include <linux/spinlock.h>

struct io_buffer_manager {
	spinlock_t fallback_lock;
	struct rb_root fallback_buffers;
};

struct io_bounce_buffer_info {
	struct page **bounce_buffer;
	dma_addr_t iova;
	size_t size;
};

bool io_buffer_manager_alloc_buffer(struct io_buffer_manager *manager,
				    struct device *dev, void *orig_buffer,
				    size_t size, int prot, unsigned int nid,
				    struct io_bounce_buffer_info *info);

bool io_buffer_manager_find_buffer(struct io_buffer_manager *manager,
				   dma_addr_t handle,
				   struct io_bounce_buffer_info *info,
				   void **orig_buffer, int *prot);

bool io_buffer_manager_release_buffer(struct io_buffer_manager *manager,
				      struct iommu_domain *domain,
				      dma_addr_t handle, bool inited);

int io_buffer_manager_init(struct io_buffer_manager *manager);

bool io_bounce_buffers_release_buffer_cb(struct io_buffer_manager *manager,
					 dma_addr_t iova, size_t size);

#endif /* _LINUX_IO_BUFFER_MANAGER_H */
