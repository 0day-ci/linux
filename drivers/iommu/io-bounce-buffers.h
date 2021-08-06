/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef _LINUX_IO_BOUNCE_BUFFERS_H
#define _LINUX_IO_BOUNCE_BUFFERS_H

#include <linux/dma-iommu.h>
#include <linux/iova.h>

struct io_bounce_buffers;

struct io_bounce_buffers *io_bounce_buffers_init(struct device *dev,
						 struct iommu_domain *domain,
						 struct iova_domain *iovad);
bool io_bounce_buffer_reinit_check(struct io_bounce_buffers *buffers,
				   struct device *dev, dma_addr_t base,
				   dma_addr_t limit);
void io_bounce_buffers_destroy(struct io_bounce_buffers *buffers);

bool io_bounce_buffers_sync_single(struct io_bounce_buffers *buffers,
				   dma_addr_t dma_handle, size_t size,
				   enum dma_data_direction dir,
				   bool sync_for_cpu);
bool io_bounce_buffers_sync_sg(struct io_bounce_buffers *buffers,
			       struct scatterlist *sgl, int nents,
			       enum dma_data_direction dir, bool sync_for_cpu);

bool io_bounce_buffers_map_page(struct io_bounce_buffers *buffers,
				struct device *dev, struct page *page,
				unsigned long offset, size_t size, int prot,
				enum dma_data_direction dir,
				unsigned long attrs, dma_addr_t *handle);
bool io_bounce_buffers_map_sg(struct io_bounce_buffers *buffers,
			      struct device *dev, struct scatterlist *sgl,
			      int nents, int prot, enum dma_data_direction dir,
			      unsigned long attrs, int *out_nents);

bool io_bounce_buffers_unmap_page(struct io_bounce_buffers *buffers,
				  dma_addr_t handle, size_t size,
				  enum dma_data_direction dir,
				  unsigned long attrs);
bool io_bounce_buffers_unmap_sg(struct io_bounce_buffers *buffers,
				struct scatterlist *sgl, int nents,
				enum dma_data_direction dir,
				unsigned long attrs);

#endif /* _LINUX_IO_BOUNCE_BUFFERS_H */
