/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

/*
 * This files contains functionality that we might want to move into
 * ttm_bo_util.c if there is a common interest.
 */
#ifndef _I915_GEM_TTM_BO_UTIL_H_
#define _I915_GEM_TTM_BO_UTIL_H_

#include <drm/ttm/ttm_bo_driver.h>
struct dma_buf_map;
struct io_mapping;
struct sg_table;
struct scatterlist;

struct ttm_tt;
struct i915_ttm_kmap_iter;

/**
 * struct i915_ttm_kmap_iter_ops - Ops structure for a struct
 * i915_ttm_kmap_iter.
 */
struct i915_ttm_kmap_iter_ops {
	/**
	 * kmap_local - Map a PAGE_SIZE part of the resource using
	 * kmap_local semantics.
	 * @res_kmap: Pointer to the struct i915_ttm_kmap_iter representing
	 * the resource.
	 * @dmap: The struct dma_buf_map holding the virtual address after
	 * the operation.
	 * @i: The location within the resource to map. PAGE_SIZE granularity.
	 */
	void (*kmap_local)(struct i915_ttm_kmap_iter *res_kmap,
			   struct dma_buf_map *dmap, pgoff_t i);
};

/**
 * struct i915_ttm_kmap_iter - Iterator for kmap_local type operations on a
 * resource.
 * @ops: Pointer to the operations struct.
 *
 * This struct is intended to be embedded in a resource-specific specialization
 * implementing operations for the resource.
 *
 * Nothing stops us from extending the operations to vmap, vmap_pfn etc,
 * replacing some or parts of the ttm_bo_util. cpu-map functionality.
 */
struct i915_ttm_kmap_iter {
	const struct i915_ttm_kmap_iter_ops *ops;
};

/**
 * struct i915_ttm_kmap_iter_tt - Specialization for a tt (page) backed struct
 * ttm_resource.
 * @base: Embedded struct i915_ttm_kmap_iter providing the usage interface
 * @tt: Cached struct ttm_tt.
 */
struct i915_ttm_kmap_iter_tt {
	struct i915_ttm_kmap_iter base;
	struct ttm_tt *tt;
};

/**
 * struct i915_ttm_kmap_iter_iomap - Specialization for a struct io_mapping +
 * struct sg_table backed struct ttm_resource.
 * @base: Embedded struct i915_ttm_kmap_iter providing the usage interface.
 * @iomap: struct io_mapping representing the underlying linear io_memory.
 * @st: sg_table into @iomap, representing the memory of the struct ttm_resource.
 * @start: Offset that needs to be subtracted from @st to make
 * sg_dma_address(st->sgl) - @start == 0 for @iomap start.
 * @cache: Scatterlist traversal cache for fast lookups.
 * @cache.sg: Pointer to the currently cached scatterlist segment.
 * @cache.i: First index of @sg. PAGE_SIZE granularity.
 * @cache.end: Last index + 1 of @sg. PAGE_SIZE granularity.
 * @cache.offs: First offset into @iomap of @sg. PAGE_SIZE granularity.
 */
struct i915_ttm_kmap_iter_iomap {
	struct i915_ttm_kmap_iter base;
	struct io_mapping *iomap;
	struct sg_table *st;
	resource_size_t start;
	struct {
		struct scatterlist *sg;
		pgoff_t i;
		pgoff_t end;
		pgoff_t offs;
	} cache;
};

extern struct i915_ttm_kmap_iter_ops i915_ttm_kmap_iter_tt_ops;
extern struct i915_ttm_kmap_iter_ops i915_ttm_kmap_iter_io_ops;

/**
 * i915_ttm_kmap_iter_iomap_init - Initialize a struct i915_ttm_kmap_iter_iomap
 * @iter_io: The struct i915_ttm_kmap_iter_iomap to initialize.
 * @iomap: The struct io_mapping representing the underlying linear io_memory.
 * @st: sg_table into @iomap, representing the memory of the struct
 * ttm_resource.
 * @start: Offset that needs to be subtracted from @st to make
 * sg_dma_address(st->sgl) - @start == 0 for @iomap start.
 *
 * Return: Pointer to the embedded struct i915_ttm_kmap_iter.
 */
static inline struct i915_ttm_kmap_iter *
i915_ttm_kmap_iter_iomap_init(struct i915_ttm_kmap_iter_iomap *iter_io,
			      struct io_mapping *iomap,
			      struct sg_table *st,
			      resource_size_t start)
{
	iter_io->base.ops = &i915_ttm_kmap_iter_io_ops;
	iter_io->iomap = iomap;
	iter_io->st = st;
	iter_io->start = start;
	memset(&iter_io->cache, 0, sizeof(iter_io->cache));
	return &iter_io->base;
}

/**
 * ttm_kmap_iter_tt_init - Initialize a struct i915_ttm_kmap_iter_tt
 * @iter_tt: The struct i915_ttm_kmap_iter_tt to initialize.
 * @tt: Struct ttm_tt holding page pointers of the struct ttm_resource.
 *
 * Return: Pointer to the embedded struct i915_ttm_kmap_iter.
 */
static inline struct i915_ttm_kmap_iter *
i915_ttm_kmap_iter_tt_init(struct i915_ttm_kmap_iter_tt *iter_tt,
			   struct ttm_tt *tt)
{
	iter_tt->base.ops = &i915_ttm_kmap_iter_tt_ops;
	iter_tt->tt = tt;
	return &iter_tt->base;
}

void i915_ttm_move_memcpy(struct ttm_buffer_object *bo,
			  struct ttm_resource *new_mem,
			  struct i915_ttm_kmap_iter *new_iter,
			  struct i915_ttm_kmap_iter *old_iter);
#endif
