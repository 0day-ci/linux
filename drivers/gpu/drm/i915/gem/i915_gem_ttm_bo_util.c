// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/**
 * DOC: Usage and intentions.
 *
 * This file contains functionality that we might want to move into
 * ttm_bo_util.c if there is a common interest.
 * Currently a kmap_local only memcpy with support for page-based iomem regions,
 * and fast memcpy from write-combined memory.
 */

#include <linux/dma-buf-map.h>
#include <linux/highmem.h>
#include <linux/io-mapping.h>
#include <linux/scatterlist.h>

#include "i915_memcpy.h"

#include "gem/i915_gem_ttm_bo_util.h"

static void i915_ttm_kmap_iter_tt_kmap_local(struct i915_ttm_kmap_iter *iter,
					     struct dma_buf_map *dmap,
					     pgoff_t i)
{
	struct i915_ttm_kmap_iter_tt *iter_tt =
		container_of(iter, typeof(*iter_tt), base);

	dma_buf_map_set_vaddr(dmap, kmap_local_page(iter_tt->tt->pages[i]));
}

static void i915_ttm_kmap_iter_iomap_kmap_local(struct i915_ttm_kmap_iter *iter,
						struct dma_buf_map *dmap,
						pgoff_t i)
{
	struct i915_ttm_kmap_iter_iomap *iter_io =
		container_of(iter, typeof(*iter_io), base);
	void __iomem *addr;

retry:
	while (i >= iter_io->cache.end) {
		iter_io->cache.sg = iter_io->cache.sg ?
			sg_next(iter_io->cache.sg) : iter_io->st->sgl;
		iter_io->cache.i = iter_io->cache.end;
		iter_io->cache.end += sg_dma_len(iter_io->cache.sg) >>
			PAGE_SHIFT;
		iter_io->cache.offs = sg_dma_address(iter_io->cache.sg) -
			iter_io->start;
	}

	if (i < iter_io->cache.i) {
		iter_io->cache.end = 0;
		iter_io->cache.sg = NULL;
		goto retry;
	}

	addr = io_mapping_map_local_wc(iter_io->iomap, iter_io->cache.offs +
				       (((resource_size_t)i - iter_io->cache.i)
					<< PAGE_SHIFT));
	dma_buf_map_set_vaddr_iomem(dmap, addr);
}

struct i915_ttm_kmap_iter_ops i915_ttm_kmap_iter_tt_ops = {
	.kmap_local = i915_ttm_kmap_iter_tt_kmap_local
};

struct i915_ttm_kmap_iter_ops i915_ttm_kmap_iter_io_ops = {
	.kmap_local =  i915_ttm_kmap_iter_iomap_kmap_local
};

static void kunmap_local_dma_buf_map(struct dma_buf_map *map)
{
	if (map->is_iomem)
		io_mapping_unmap_local(map->vaddr_iomem);
	else
		kunmap_local(map->vaddr);
}

/**
 * i915_ttm_move_memcpy - Helper to perform a memcpy ttm move operation.
 * @bo: The struct ttm_buffer_object.
 * @new_mem: The struct ttm_resource we're moving to (copy destination).
 * @new_kmap: A struct i915_ttm_kmap_iter representing the destination resource.
 * @old_kmap: A struct i915_ttm_kmap_iter representing the source resource.
 */
void i915_ttm_move_memcpy(struct ttm_buffer_object *bo,
			  struct ttm_resource *new_mem,
			  struct i915_ttm_kmap_iter *new_kmap,
			  struct i915_ttm_kmap_iter *old_kmap)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource *old_mem = &bo->mem;
	struct ttm_resource old_copy = *old_mem;
	struct ttm_resource_manager *old_man = ttm_manager_type(bdev, old_mem->mem_type);
	struct dma_buf_map old_map, new_map;
	pgoff_t i;

	/* For the page-based allocator we need sgtable iterators as well.*/

	/* Single TTM move. NOP */
	if (old_man->use_tt && man->use_tt)
		goto done;

	/* Don't move nonexistent data. Clear destination instead. */
	if (old_man->use_tt && !man->use_tt &&
	    (ttm == NULL || !ttm_tt_is_populated(ttm))) {
		if (ttm && !(ttm->page_flags & TTM_PAGE_FLAG_ZERO_ALLOC))
			goto done;

		for (i = 0; i < new_mem->num_pages; ++i) {
			new_kmap->ops->kmap_local(new_kmap, &new_map, i);
			memset_io(new_map.vaddr_iomem, 0, PAGE_SIZE);
			kunmap_local_dma_buf_map(&new_map);
		}
		goto done;
	}

	for (i = 0; i < new_mem->num_pages; ++i) {
		new_kmap->ops->kmap_local(new_kmap, &new_map, i);
		old_kmap->ops->kmap_local(old_kmap, &old_map, i);
		if (!old_map.is_iomem ||
		    !i915_memcpy_from_wc(new_map.vaddr, old_map.vaddr, PAGE_SIZE)) {
			if (!old_map.is_iomem) {
				dma_buf_map_memcpy_to(&new_map, old_map.vaddr,
						      PAGE_SIZE);
			} else if (!new_map.is_iomem) {
				memcpy_fromio(new_map.vaddr, old_map.vaddr_iomem,
					      PAGE_SIZE);
			} else {
				pgoff_t j;
				u32 __iomem *src = old_map.vaddr_iomem;
				u32 __iomem *dst = new_map.vaddr_iomem;

				for (j = 0; j < (PAGE_SIZE >> 2); ++j)
					iowrite32(ioread32(src++), dst++);
			}
		}
		kunmap_local_dma_buf_map(&old_map);
		kunmap_local_dma_buf_map(&new_map);
	}

done:
	old_copy = *old_mem;

	ttm_bo_assign_mem(bo, new_mem);

	if (!man->use_tt)
		ttm_bo_tt_destroy(bo);

	ttm_resource_free(bo, &old_copy);
}
