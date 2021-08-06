// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dynamic IOMMU mapped bounce buffers.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/dma-iommu.h>
#include <linux/dma-map-ops.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "io-buffer-manager.h"
#include "io-bounce-buffers.h"

struct io_bounce_buffers {
	struct iommu_domain *domain;
	struct iova_domain *iovad;
	unsigned int nid;
	struct io_buffer_manager manager;
};

bool io_bounce_buffers_release_buffer_cb(struct io_buffer_manager *manager,
					 dma_addr_t iova, size_t size)
{
	struct io_bounce_buffers *buffers =
		container_of(manager, struct io_bounce_buffers, manager);
	return iommu_unmap(buffers->domain, iova, size) >= size;
}

struct io_bounce_buffers *io_bounce_buffers_init(struct device *dev,
						 struct iommu_domain *domain,
						 struct iova_domain *iovad)
{
	int ret;
	struct io_bounce_buffers *buffers;

	buffers = kzalloc(sizeof(*buffers), GFP_KERNEL);
	if (!buffers)
		return ERR_PTR(-ENOMEM);

	ret = io_buffer_manager_init(&buffers->manager);
	if (ret) {
		kfree(buffers);
		return ERR_PTR(ret);
	}

	buffers->domain = domain;
	buffers->iovad = iovad;
	buffers->nid = dev_to_node(dev);

	return buffers;
}

void io_bounce_buffers_destroy(struct io_bounce_buffers *buffers)
{
	kfree(buffers);
}

static bool should_sync_buffer(enum dma_data_direction dir, bool sync_for_cpu)
{
	return dir == DMA_BIDIRECTIONAL ||
	       (dir == DMA_FROM_DEVICE && sync_for_cpu) ||
	       (dir == DMA_TO_DEVICE && !sync_for_cpu);
}

static void io_bounce_buffers_do_sync(struct io_bounce_buffers *buffers,
				      struct page **bounce_buffer,
				      size_t bounce_offset, struct page *orig,
				      size_t orig_offset, size_t size,
				      enum dma_data_direction dir, int prot,
				      bool sync_for_cpu)
{
	bool needs_bounce_sync = should_sync_buffer(dir, sync_for_cpu);
	char *orig_lowmem_ptr;
	bool dma_is_coherent = prot & IOMMU_CACHE;

	if (dma_is_coherent && !needs_bounce_sync)
		return;

	orig_lowmem_ptr = PageHighMem(orig) ? NULL : page_to_virt(orig);

	while (size) {
		size_t copy_len, bounce_page_offset;
		struct page *bounce_page;

		bounce_page = bounce_buffer[bounce_offset / PAGE_SIZE];
		bounce_page_offset = bounce_offset % PAGE_SIZE;

		copy_len = size;
		if (copy_len + bounce_page_offset > PAGE_SIZE)
			copy_len = PAGE_SIZE - bounce_page_offset;

		if (!dma_is_coherent && sync_for_cpu) {
			phys_addr_t paddr = page_to_phys(bounce_page);

			arch_sync_dma_for_cpu(paddr + bounce_page_offset,
					      copy_len, dir);
		}

		if (needs_bounce_sync) {
			char *bounce_page_ptr = kmap_local_page(bounce_page);
			char *bounce_ptr = bounce_page_ptr + bounce_page_offset;

			if (!orig_lowmem_ptr) {
				size_t remaining = copy_len;
				size_t offset = orig_offset % PAGE_SIZE;
				size_t orig_page_idx = orig_offset / PAGE_SIZE;

				while (remaining) {
					char *orig_ptr;
					size_t sz = min(remaining,
							PAGE_SIZE - offset);

					orig_ptr = kmap_local_page(
						nth_page(orig, orig_page_idx));
					if (sync_for_cpu) {
						memcpy(orig_ptr + offset,
						       bounce_ptr, sz);
					} else {
						memcpy(bounce_ptr,
						       orig_ptr + offset, sz);
					}
					kunmap_local(orig_ptr);

					remaining -= sz;
					orig_page_idx += 1;
					bounce_ptr += sz;
					offset = 0;
				}
			} else if (sync_for_cpu) {
				memcpy(orig_lowmem_ptr + orig_offset,
				       bounce_ptr, copy_len);
			} else {
				memcpy(bounce_ptr,
				       orig_lowmem_ptr + orig_offset, copy_len);
			}

			kunmap_local(bounce_page_ptr);
		}

		if (!dma_is_coherent && !sync_for_cpu) {
			phys_addr_t paddr = page_to_phys(bounce_page);

			arch_sync_dma_for_device(paddr + bounce_page_offset,
						 copy_len, dir);
		}

		bounce_offset += copy_len;
		orig_offset += copy_len;
		size -= copy_len;
	}
}

static void __io_bounce_buffers_sync_single(struct io_bounce_buffers *buffers,
					    dma_addr_t dma_handle, size_t size,
					    struct io_bounce_buffer_info *info,
					    struct page *orig_buffer, int prot,
					    enum dma_data_direction dir,
					    bool sync_for_cpu)
{
	size_t offset = dma_handle - info->iova;

	io_bounce_buffers_do_sync(buffers, info->bounce_buffer, offset,
				  orig_buffer, offset, size, dir, prot,
				  sync_for_cpu);
}

bool io_bounce_buffers_sync_single(struct io_bounce_buffers *buffers,
				   dma_addr_t dma_handle, size_t size,
				   enum dma_data_direction dir,
				   bool sync_for_cpu)
{
	struct io_bounce_buffer_info info;
	void *orig_buffer;
	int prot;

	if (!io_buffer_manager_find_buffer(&buffers->manager, dma_handle, &info,
					   &orig_buffer, &prot))
		return false;

	__io_bounce_buffers_sync_single(buffers, dma_handle, size, &info,
					orig_buffer, prot, dir, sync_for_cpu);
	return true;
}

static void __io_bounce_buffers_sync_sg(struct io_bounce_buffers *buffers,
					struct scatterlist *sgl, int nents,
					struct page **bounce_buffer,
					enum dma_data_direction dir, int prot,
					bool sync_for_cpu)
{
	size_t bounce_offset = 0;
	struct scatterlist *iter;
	int i;

	for_each_sg(sgl, iter, nents, i) {
		io_bounce_buffers_do_sync(buffers, bounce_buffer, bounce_offset,
					  sg_page(iter), iter->offset,
					  iter->length, dir, prot,
					  sync_for_cpu);
		bounce_offset += iter->length;
	}
}

bool io_bounce_buffers_sync_sg(struct io_bounce_buffers *buffers,
			       struct scatterlist *sgl, int nents,
			       enum dma_data_direction dir, bool sync_for_cpu)
{
	struct io_bounce_buffer_info info;
	void *orig_buffer;
	int prot;

	if (!io_buffer_manager_find_buffer(&buffers->manager,
					   sg_dma_address(sgl), &info,
					   &orig_buffer, &prot))
		return false;

	// In the non bounce buffer case, iommu_dma_map_sg syncs before setting
	// up the new mapping's dma address. This check handles false positives
	// in find_buffer caused by sgl being reused for a non bounce buffer
	// case after being used with a bounce buffer.
	if (orig_buffer != sgl)
		return false;

	__io_bounce_buffers_sync_sg(buffers, sgl, nents, info.bounce_buffer,
				    dir, prot, sync_for_cpu);

	return true;
}

struct unmap_sync_args {
	struct io_bounce_buffers *buffers;
	unsigned long attrs;
	enum dma_data_direction dir;
	dma_addr_t handle;
	size_t size;
	int nents;
};

static void
io_bounce_buffers_unmap_page_sync(struct io_bounce_buffer_info *info, int prot,
				  void *orig_buffer, void *ctx)
{
	struct unmap_sync_args *args = ctx;

	if (args->attrs & DMA_ATTR_SKIP_CPU_SYNC)
		return;

	__io_bounce_buffers_sync_single(args->buffers, args->handle, args->size,
					info, orig_buffer, prot, args->dir,
					true);
}

bool io_bounce_buffers_unmap_page(struct io_bounce_buffers *buffers,
				  dma_addr_t handle, size_t size,
				  enum dma_data_direction dir,
				  unsigned long attrs)
{
	struct unmap_sync_args args = { .buffers = buffers,
					.attrs = attrs,
					.dir = dir,
					.handle = handle,
					.size = size };

	return io_buffer_manager_release_buffer(
		&buffers->manager, buffers->domain, handle, true,
		io_bounce_buffers_unmap_page_sync, &args);
}

static void io_bounce_buffers_unmap_sg_sync(struct io_bounce_buffer_info *info,
					    int prot, void *orig_buffer,
					    void *ctx)
{
	struct unmap_sync_args *args = ctx;

	if (args->attrs & DMA_ATTR_SKIP_CPU_SYNC)
		return;

	__io_bounce_buffers_sync_sg(args->buffers, orig_buffer, args->nents,
				    info->bounce_buffer, args->dir, prot, true);
}

bool io_bounce_buffers_unmap_sg(struct io_bounce_buffers *buffers,
				struct scatterlist *sgl, int nents,
				enum dma_data_direction dir,
				unsigned long attrs)
{
	struct unmap_sync_args args = {
		.buffers = buffers, .attrs = attrs, .dir = dir, .nents = nents
	};

	return io_buffer_manager_release_buffer(
		&buffers->manager, buffers->domain, sg_dma_address(sgl), true,
		io_bounce_buffers_unmap_sg_sync, &args);
}

static void io_bounce_buffers_clear_padding(struct io_bounce_buffer_info *info,
					    size_t pad_hd_end,
					    size_t pad_tl_start)
{
	size_t idx, pad_hd_idx, pad_tl_idx, count;

	count = info->size / PAGE_SIZE;
	pad_hd_idx = pad_hd_end / PAGE_SIZE;
	pad_tl_idx = pad_tl_start / PAGE_SIZE;

	if (!IS_ALIGNED(pad_hd_end, PAGE_SIZE)) {
		struct page *page = info->bounce_buffer[pad_hd_idx];
		size_t len = offset_in_page(pad_hd_end);

		memset_page(page, 0, 0, len);
		arch_sync_dma_for_device(page_to_phys(page), 0, len);
	}

	if (!IS_ALIGNED(pad_tl_start, PAGE_SIZE)) {
		size_t off = offset_in_page(pad_tl_start);
		size_t len = PAGE_SIZE - off;
		struct page *page = info->bounce_buffer[pad_tl_idx];

		memset_page(page, off, 0, len);
		arch_sync_dma_for_device(page_to_phys(page) + off, 0, len);

		pad_tl_idx++;
	}

	idx = pad_hd_idx ? 0 : pad_tl_idx;
	while (idx < count) {
		struct page *page = info->bounce_buffer[idx++];

		clear_highpage(page);
		arch_sync_dma_for_device(page_to_phys(page), 0, PAGE_SIZE);
		if (idx == pad_hd_idx)
			idx = pad_tl_idx;
	}
}

static bool io_bounce_buffers_map_buffer(struct io_bounce_buffers *buffers,
					 struct io_bounce_buffer_info *info,
					 int prot, bool skiped_sync,
					 size_t offset, size_t orig_size)
{
	unsigned int count = info->size >> PAGE_SHIFT;
	struct sg_table sgt;
	size_t mapped;

	if (offset || offset + orig_size < info->size || skiped_sync) {
		// Ensure that nothing is leaked to untrusted devices when
		// mapping the buffer by clearing any part of the bounce buffer
		// that wasn't already cleared by syncing.
		size_t pad_hd_end, pad_tl_start;

		if (skiped_sync) {
			pad_hd_end = pad_tl_start = 0;
		} else {
			pad_hd_end = offset;
			pad_tl_start = offset + orig_size;
		}
		io_bounce_buffers_clear_padding(info, pad_hd_end, pad_tl_start);
	}

	if (sg_alloc_table_from_pages(&sgt, info->bounce_buffer, count, 0,
				      info->size, GFP_ATOMIC))
		return false;

	mapped = iommu_map_sg_atomic(buffers->domain, info->iova, sgt.sgl,
				     sgt.orig_nents, prot);

	sg_free_table(&sgt);
	return mapped >= info->size;
}

bool io_bounce_buffers_map_page(struct io_bounce_buffers *buffers,
				struct device *dev, struct page *page,
				unsigned long offset, size_t size, int prot,
				enum dma_data_direction dir,
				unsigned long attrs, dma_addr_t *handle)
{
	bool skip_cpu_sync = attrs & DMA_ATTR_SKIP_CPU_SYNC;
	struct io_bounce_buffer_info info;
	bool force_bounce = iova_offset(buffers->iovad, offset | size);

	if (!force_bounce)
		return false;

	*handle = DMA_MAPPING_ERROR;
	if (!io_buffer_manager_alloc_buffer(&buffers->manager, dev, page,
					    offset + size, prot, buffers->nid,
					    &info))
		return true;

	if (!skip_cpu_sync)
		io_bounce_buffers_do_sync(buffers, info.bounce_buffer, offset,
					  page, offset, size, dir, prot, false);

	if (!io_bounce_buffers_map_buffer(buffers, &info, prot, skip_cpu_sync,
					  offset, size)) {
		io_buffer_manager_release_buffer(&buffers->manager,
						 buffers->domain, info.iova,
						 false, NULL, NULL);
		return true;
	}

	*handle = info.iova + offset;
	return true;
}

bool io_bounce_buffers_map_sg(struct io_bounce_buffers *buffers,
			      struct device *dev, struct scatterlist *sgl,
			      int nents, int prot, enum dma_data_direction dir,
			      unsigned long attrs, int *out_nents)
{
	struct io_bounce_buffer_info info;
	struct scatterlist *iter;
	size_t size = 0;
	bool skip_cpu_sync = attrs & DMA_ATTR_SKIP_CPU_SYNC;
	dma_addr_t seg_iova;
	int i;
	bool force_bounce = false;

	for_each_sg(sgl, iter, nents, i) {
		size += iter->length;
		force_bounce |= iova_offset(buffers->iovad,
					    iter->offset | iter->length);
	}

	if (!force_bounce)
		return false;

	*out_nents = 0;
	if (!io_buffer_manager_alloc_buffer(&buffers->manager, dev, sgl, size,
					    prot, buffers->nid, &info))
		return true;

	if (!skip_cpu_sync)
		__io_bounce_buffers_sync_sg(buffers, sgl, nents,
					    info.bounce_buffer, dir, prot,
					    false);

	if (!io_bounce_buffers_map_buffer(buffers, &info, prot, skip_cpu_sync,
					  0, size)) {
		io_buffer_manager_release_buffer(&buffers->manager,
						 buffers->domain, info.iova,
						 false, NULL, NULL);
		return true;
	}

	i = 0;
	seg_iova = info.iova;
	while (size > 0) {
		size_t seg_size = min_t(size_t, size,
					dma_get_max_seg_size(dev));

		sg_dma_len(sgl) = seg_size;
		sg_dma_address(sgl) = seg_iova;

		sgl = sg_next(sgl);
		size -= seg_size;
		seg_iova += seg_size;
		i++;
	}

	if (sgl) {
		sg_dma_address(sgl) = DMA_MAPPING_ERROR;
		sg_dma_len(sgl) = 0;
	}

	*out_nents = i;
	return true;
}
