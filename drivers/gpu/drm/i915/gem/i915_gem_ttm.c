// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

#include "i915_drv.h"
#include "i915_sw_fence_work.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_object.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_ttm_pm.h"


#include "gt/intel_engine_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_migrate.h"

#define I915_TTM_PRIO_PURGE     0
#define I915_TTM_PRIO_NO_PAGES  1
#define I915_TTM_PRIO_HAS_PAGES 2

I915_SELFTEST_DECLARE(static bool fail_gpu_migration;)
I915_SELFTEST_DECLARE(static bool fail_work_allocation;)

#ifdef CONFIG_DRM_I915_SELFTEST
void i915_ttm_migrate_set_failure_modes(bool gpu_migration,
					bool work_allocation)
{
	fail_gpu_migration = gpu_migration;
	fail_work_allocation = work_allocation;
}
#endif

/*
 * Size of struct ttm_place vector in on-stack struct ttm_placement allocs
 */
#define I915_TTM_MAX_PLACEMENTS INTEL_REGION_UNKNOWN

/**
 * struct i915_ttm_tt - TTM page vector with additional private information
 * @ttm: The base TTM page vector.
 * @dev: The struct device used for dma mapping and unmapping.
 * @cached_rsgt: The cached scatter-gather table.
 *
 * Note that DMA may be going on right up to the point where the page-
 * vector is unpopulated in delayed destroy. Hence keep the
 * scatter-gather table mapped and cached up to that point. This is
 * different from the cached gem object io scatter-gather table which
 * doesn't have an associated dma mapping.
 */
struct i915_ttm_tt {
	struct ttm_tt ttm;
	struct device *dev;
	struct i915_refct_sgt cached_rsgt;
};

static const struct ttm_place sys_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = I915_PL_SYSTEM,
	.flags = 0,
};

static struct ttm_placement i915_sys_placement = {
	.num_placement = 1,
	.placement = &sys_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags,
};

/**
 * i915_ttm_sys_placement - Return the struct ttm_placement to be
 * used for an object in system memory.
 *
 * Rather than making the struct extern, use this
 * function.
 *
 * Return: A pointer to a static variable for sys placement.
 */
struct ttm_placement *i915_ttm_sys_placement(void)
{
	return &i915_sys_placement;
}

static int i915_ttm_err_to_gem(int err)
{
	/* Fastpath */
	if (likely(!err))
		return 0;

	switch (err) {
	case -EBUSY:
		/*
		 * TTM likes to convert -EDEADLK to -EBUSY, and wants us to
		 * restart the operation, since we don't record the contending
		 * lock. We use -EAGAIN to restart.
		 */
		return -EAGAIN;
	case -ENOSPC:
		/*
		 * Memory type / region is full, and we can't evict.
		 * Except possibly system, that returns -ENOMEM;
		 */
		return -ENXIO;
	default:
		break;
	}

	return err;
}

static bool gpu_binds_iomem(struct ttm_resource *mem)
{
	return mem->mem_type != TTM_PL_SYSTEM;
}

static bool cpu_maps_iomem(struct ttm_resource *mem)
{
	/* Once / if we support GGTT, this is also false for cached ttm_tts */
	return mem->mem_type != TTM_PL_SYSTEM;
}

static enum i915_cache_level
i915_ttm_cache_level(struct drm_i915_private *i915, struct ttm_resource *res,
		     struct ttm_tt *ttm)
{
	return ((HAS_LLC(i915) || HAS_SNOOP(i915)) && !gpu_binds_iomem(res) &&
		ttm->caching == ttm_cached) ? I915_CACHE_LLC :
		I915_CACHE_NONE;
}

static void i915_ttm_adjust_lru(struct drm_i915_gem_object *obj);

static enum ttm_caching
i915_ttm_select_tt_caching(const struct drm_i915_gem_object *obj)
{
	/*
	 * Objects only allowed in system get cached cpu-mappings.
	 * Other objects get WC mapping for now. Even if in system.
	 */
	if (obj->mm.region->type == INTEL_MEMORY_SYSTEM &&
	    obj->mm.n_placements <= 1)
		return ttm_cached;

	return ttm_write_combined;
}

static void
i915_ttm_place_from_region(const struct intel_memory_region *mr,
			   struct ttm_place *place,
			   unsigned int flags)
{
	memset(place, 0, sizeof(*place));
	place->mem_type = intel_region_to_ttm_type(mr);

	if (flags & I915_BO_ALLOC_CONTIGUOUS)
		place->flags = TTM_PL_FLAG_CONTIGUOUS;
}

static void
i915_ttm_placement_from_obj(const struct drm_i915_gem_object *obj,
			    struct ttm_place *requested,
			    struct ttm_place *busy,
			    struct ttm_placement *placement)
{
	unsigned int num_allowed = obj->mm.n_placements;
	unsigned int flags = obj->flags;
	unsigned int i;

	placement->num_placement = 1;
	i915_ttm_place_from_region(num_allowed ? obj->mm.placements[0] :
				   obj->mm.region, requested, flags);

	/* Cache this on object? */
	placement->num_busy_placement = num_allowed;
	for (i = 0; i < placement->num_busy_placement; ++i)
		i915_ttm_place_from_region(obj->mm.placements[i], busy + i, flags);

	if (num_allowed == 0) {
		*busy = *requested;
		placement->num_busy_placement = 1;
	}

	placement->placement = requested;
	placement->busy_placement = busy;
}

static void i915_ttm_tt_release(struct kref *ref)
{
	struct i915_ttm_tt *i915_tt =
		container_of(ref, typeof(*i915_tt), cached_rsgt.kref);
	struct sg_table *st = &i915_tt->cached_rsgt.table;

	GEM_WARN_ON(st->sgl);

	kfree(i915_tt);
}

static const struct i915_refct_sgt_ops tt_rsgt_ops = {
	.release = i915_ttm_tt_release
};

static struct ttm_tt *i915_ttm_tt_create(struct ttm_buffer_object *bo,
					 uint32_t page_flags)
{
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, bo->resource->mem_type);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct i915_ttm_tt *i915_tt;
	int ret;

	i915_tt = kzalloc(sizeof(*i915_tt), GFP_KERNEL);
	if (!i915_tt)
		return NULL;

	if (obj->flags & I915_BO_ALLOC_CPU_CLEAR &&
	    man->use_tt)
		page_flags |= TTM_TT_FLAG_ZERO_ALLOC;

	ret = ttm_tt_init(&i915_tt->ttm, bo, page_flags,
			  i915_ttm_select_tt_caching(obj));
	if (ret) {
		kfree(i915_tt);
		return NULL;
	}

	i915_refct_sgt_init_ops(&i915_tt->cached_rsgt, bo->base.size,
				&tt_rsgt_ops);
	i915_tt->dev = obj->base.dev->dev;

	return &i915_tt->ttm;
}

static void i915_ttm_tt_unpopulate(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	struct sg_table *st = &i915_tt->cached_rsgt.table;

	GEM_WARN_ON(kref_read(&i915_tt->cached_rsgt.kref) != 1);

	if (st->sgl) {
		dma_unmap_sgtable(i915_tt->dev, st, DMA_BIDIRECTIONAL, 0);
		sg_free_table(st);
	}
	ttm_pool_free(&bdev->pool, ttm);
}

static void i915_ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);

	GEM_WARN_ON(kref_read(&i915_tt->cached_rsgt.kref) != 1);

	ttm_tt_fini(ttm);
	i915_refct_sgt_put(&i915_tt->cached_rsgt);
}

static bool i915_ttm_eviction_valuable(struct ttm_buffer_object *bo,
				       const struct ttm_place *place)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	/* Will do for now. Our pinned objects are still on TTM's LRU lists */
	return i915_gem_object_evictable(obj);
}

static void i915_ttm_evict_flags(struct ttm_buffer_object *bo,
				 struct ttm_placement *placement)
{
	*placement = i915_sys_placement;
}

static int i915_ttm_move_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	int ret;

	ret = i915_gem_object_unbind(obj, I915_GEM_OBJECT_UNBIND_ACTIVE);
	if (ret)
		return ret;

	ret = __i915_gem_object_put_pages(obj);
	if (ret)
		return ret;

	return 0;
}

static void i915_ttm_free_cached_io_rsgt(struct drm_i915_gem_object *obj)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	if (!obj->ttm.cached_io_rsgt)
		return;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &obj->ttm.get_io_page.radix, &iter, 0)
		radix_tree_delete(&obj->ttm.get_io_page.radix, iter.index);
	rcu_read_unlock();

	i915_refct_sgt_put(obj->ttm.cached_io_rsgt);
	obj->ttm.cached_io_rsgt = NULL;
}

static void
i915_ttm_adjust_domains_after_move(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);

	if (cpu_maps_iomem(bo->resource) || bo->ttm->caching != ttm_cached) {
		obj->write_domain = I915_GEM_DOMAIN_WC;
		obj->read_domains = I915_GEM_DOMAIN_WC;
	} else {
		obj->write_domain = I915_GEM_DOMAIN_CPU;
		obj->read_domains = I915_GEM_DOMAIN_CPU;
	}
}

static void i915_ttm_adjust_gem_after_move(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	unsigned int cache_level;
	unsigned int i;

	/*
	 * If object was moved to an allowable region, update the object
	 * region to consider it migrated. Note that if it's currently not
	 * in an allowable region, it's evicted and we don't update the
	 * object region.
	 */
	if (intel_region_to_ttm_type(obj->mm.region) != bo->resource->mem_type) {
		for (i = 0; i < obj->mm.n_placements; ++i) {
			struct intel_memory_region *mr = obj->mm.placements[i];

			if (intel_region_to_ttm_type(mr) == bo->resource->mem_type &&
			    mr != obj->mm.region) {
				i915_gem_object_release_memory_region(obj);
				i915_gem_object_init_memory_region(obj, mr);
				break;
			}
		}
	}

	obj->mem_flags &= ~(I915_BO_FLAG_STRUCT_PAGE | I915_BO_FLAG_IOMEM);

	obj->mem_flags |= cpu_maps_iomem(bo->resource) ? I915_BO_FLAG_IOMEM :
		I915_BO_FLAG_STRUCT_PAGE;

	cache_level = i915_ttm_cache_level(to_i915(bo->base.dev), bo->resource,
					   bo->ttm);
	i915_gem_object_set_cache_coherency(obj, cache_level);
}

static void i915_ttm_purge(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	struct ttm_placement place = {};
	int ret;

	if (obj->mm.madv == __I915_MADV_PURGED)
		return;

	/* TTM's purge interface. Note that we might be reentering. */
	ret = ttm_bo_validate(bo, &place, &ctx);
	if (!ret) {
		obj->write_domain = 0;
		obj->read_domains = 0;
		i915_ttm_adjust_gem_after_move(obj);
		i915_ttm_free_cached_io_rsgt(obj);
		obj->mm.madv = __I915_MADV_PURGED;
	}
}

static void i915_ttm_swap_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	int ret = i915_ttm_move_notify(bo);

	GEM_WARN_ON(ret);
	GEM_WARN_ON(obj->ttm.cached_io_rsgt);
	if (!ret && obj->mm.madv != I915_MADV_WILLNEED)
		i915_ttm_purge(obj);
}

static void i915_ttm_delete_mem_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	if (likely(obj)) {
		__i915_gem_object_pages_fini(obj);
		i915_ttm_free_cached_io_rsgt(obj);
	}
}

static struct intel_memory_region *
i915_ttm_region(struct ttm_device *bdev, int ttm_mem_type)
{
	struct drm_i915_private *i915 = container_of(bdev, typeof(*i915), bdev);

	/* There's some room for optimization here... */
	GEM_BUG_ON(ttm_mem_type != I915_PL_SYSTEM &&
		   ttm_mem_type < I915_PL_LMEM0);
	if (ttm_mem_type == I915_PL_SYSTEM)
		return intel_memory_region_lookup(i915, INTEL_MEMORY_SYSTEM,
						  0);

	return intel_memory_region_lookup(i915, INTEL_MEMORY_LOCAL,
					  ttm_mem_type - I915_PL_LMEM0);
}

static struct i915_refct_sgt *i915_ttm_tt_get_st(struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	struct sg_table *st;
	int ret;

	if (i915_tt->cached_rsgt.table.sgl)
		return i915_refct_sgt_get(&i915_tt->cached_rsgt);

	st = &i915_tt->cached_rsgt.table;
	ret = sg_alloc_table_from_pages_segment(st,
			ttm->pages, ttm->num_pages,
			0, (unsigned long)ttm->num_pages << PAGE_SHIFT,
			i915_sg_segment_size(), GFP_KERNEL);
	if (ret) {
		st->sgl = NULL;
		return ERR_PTR(ret);
	}

	ret = dma_map_sgtable(i915_tt->dev, st, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		sg_free_table(st);
		return ERR_PTR(ret);
	}

	return i915_refct_sgt_get(&i915_tt->cached_rsgt);
}

static struct i915_refct_sgt *
i915_ttm_resource_get_st(struct drm_i915_gem_object *obj,
			 struct ttm_resource *res)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);

	if (!gpu_binds_iomem(res))
		return i915_ttm_tt_get_st(bo->ttm);

	/*
	 * If CPU mapping differs, we need to add the ttm_tt pages to
	 * the resulting st. Might make sense for GGTT.
	 */
	GEM_WARN_ON(!cpu_maps_iomem(res));
	if (bo->resource == res) {
		if (!obj->ttm.cached_io_rsgt) {
			struct i915_refct_sgt *rsgt;

			rsgt = intel_region_ttm_resource_to_rsgt(obj->mm.region,
								 res);
			if (IS_ERR(rsgt))
				return rsgt;

			obj->ttm.cached_io_rsgt = rsgt;
		}
		return i915_refct_sgt_get(obj->ttm.cached_io_rsgt);
	}

	return intel_region_ttm_resource_to_rsgt(obj->mm.region, res);
}

static struct dma_fence *i915_ttm_accel_move(struct ttm_buffer_object *bo,
					     bool clear,
					     struct ttm_resource *dst_mem,
					     struct ttm_tt *dst_ttm,
					     struct sg_table *dst_st)
{
	struct drm_i915_private *i915 = container_of(bo->bdev, typeof(*i915),
						     bdev);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct i915_request *rq;
	struct ttm_tt *src_ttm = bo->ttm;
	enum i915_cache_level src_level, dst_level;
	int ret;

	if (!i915->gt.migrate.context || intel_gt_is_wedged(&i915->gt))
		return ERR_PTR(-EINVAL);

	/* With fail_gpu_migration, we always perform a GPU clear. */
	if (I915_SELFTEST_ONLY(fail_gpu_migration))
		clear = true;

	dst_level = i915_ttm_cache_level(i915, dst_mem, dst_ttm);
	if (clear) {
		if (bo->type == ttm_bo_type_kernel &&
		    !I915_SELFTEST_ONLY(fail_gpu_migration))
			return ERR_PTR(-EINVAL);

		intel_engine_pm_get(i915->gt.migrate.context->engine);
		ret = intel_context_migrate_clear(i915->gt.migrate.context, NULL,
						  dst_st->sgl, dst_level,
						  gpu_binds_iomem(dst_mem),
						  0, &rq);
	} else {
		struct i915_refct_sgt *src_rsgt =
			i915_ttm_resource_get_st(obj, bo->resource);

		if (IS_ERR(src_rsgt))
			return ERR_CAST(src_rsgt);

		src_level = i915_ttm_cache_level(i915, bo->resource, src_ttm);
		intel_engine_pm_get(i915->gt.migrate.context->engine);
		ret = intel_context_migrate_copy(i915->gt.migrate.context,
						 NULL, src_rsgt->table.sgl,
						 src_level,
						 gpu_binds_iomem(bo->resource),
						 dst_st->sgl, dst_level,
						 gpu_binds_iomem(dst_mem),
						 &rq);

		i915_refct_sgt_put(src_rsgt);
	}

	intel_engine_pm_put(i915->gt.migrate.context->engine);

	if (ret && rq) {
		i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
		i915_request_put(rq);
	}

	return ret ? ERR_PTR(ret) : &rq->fence;
}

/**
 * struct i915_ttm_memcpy_work - memcpy work item under a dma-fence
 * @base: The struct dma_fence_work we subclass.
 * @_dst_iter: Storage space for the destination kmap iterator.
 * @_src_iter: Storage space for the source kmap iterator.
 * @dst_iter: Pointer to the destination kmap iterator.
 * @src_iter: Pointer to the source kmap iterator.
 * @clear: Whether to clear instead of copy.
 * @num_pages: Number of pages in the copy.
 * @src_rsgt: Refcounted scatter-gather list of source memory.
 * @dst_rsgt: Refcounted scatter-gather list of destination memory.
 */
struct i915_ttm_memcpy_work {
	struct dma_fence_work base;
	union {
		struct ttm_kmap_iter_tt tt;
		struct ttm_kmap_iter_iomap io;
	} _dst_iter,
	_src_iter;
	struct ttm_kmap_iter *dst_iter;
	struct ttm_kmap_iter *src_iter;
	unsigned long num_pages;
	bool clear;
	struct i915_refct_sgt *src_rsgt;
	struct i915_refct_sgt *dst_rsgt;
};

static void __memcpy_work(struct dma_fence_work *work)
{
	struct i915_ttm_memcpy_work *copy_work =
		container_of(work, typeof(*copy_work), base);

	if (I915_SELFTEST_ONLY(fail_gpu_migration))
		cmpxchg(&work->error, 0, -EINVAL);

	/* If there was an error in the gpu copy operation, run memcpy. */
	if (work->error)
		ttm_move_memcpy(copy_work->clear, copy_work->num_pages,
				copy_work->dst_iter, copy_work->src_iter);

	/*
	 * Can't signal before we unref the rsgts, because then
	 * ttms might be unpopulated before we unref these and we'll hit
	 * a GEM_WARN_ON() in i915_ttm_tt_unpopulate. Not a real problem,
	 * but good to keep the GEM_WARN_ON to check that we don't leak rsgts.
	 */
	i915_refct_sgt_put(copy_work->src_rsgt);
	i915_refct_sgt_put(copy_work->dst_rsgt);
}

static const struct dma_fence_work_ops i915_ttm_memcpy_ops = {
	.work = __memcpy_work,
};

static void i915_ttm_memcpy_work_init(struct i915_ttm_memcpy_work *copy_work,
				      struct ttm_buffer_object *bo, bool clear,
				      struct ttm_resource *dst_mem,
				      struct ttm_tt *dst_ttm,
				      struct i915_refct_sgt *dst_rsgt)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct intel_memory_region *dst_reg, *src_reg;

	dst_reg = i915_ttm_region(bo->bdev, dst_mem->mem_type);
	src_reg = i915_ttm_region(bo->bdev, bo->resource->mem_type);
	GEM_BUG_ON(!dst_reg || !src_reg);

	/*
	 * We could consider populating only parts of this structure
	 * (like avoiding the iterators) until it's actually
	 * determined that we need it. But initializing the iterators
	 * shouldn't be that costly really.
	 */

	copy_work->dst_iter = !cpu_maps_iomem(dst_mem) ?
		ttm_kmap_iter_tt_init(&copy_work->_dst_iter.tt, dst_ttm) :
		ttm_kmap_iter_iomap_init(&copy_work->_dst_iter.io, &dst_reg->iomap,
					 &dst_rsgt->table, dst_reg->region.start);

	copy_work->src_iter = !cpu_maps_iomem(bo->resource) ?
		ttm_kmap_iter_tt_init(&copy_work->_src_iter.tt, bo->ttm) :
		ttm_kmap_iter_iomap_init(&copy_work->_src_iter.io, &src_reg->iomap,
					 &obj->ttm.cached_io_rsgt->table,
					 src_reg->region.start);
	copy_work->clear = clear;
	copy_work->num_pages = bo->base.size >> PAGE_SHIFT;

	copy_work->dst_rsgt = i915_refct_sgt_get(dst_rsgt);
	copy_work->src_rsgt = clear ? NULL :
		i915_ttm_resource_get_st(obj, bo->resource);
}

/*
 * This is only used as a last fallback if the copy_work
 * memory allocation fails, prohibiting async moves.
 */
static void __i915_ttm_move_fallback(struct ttm_buffer_object *bo, bool clear,
				     struct ttm_resource *dst_mem,
				     struct ttm_tt *dst_ttm,
				     struct i915_refct_sgt *dst_rsgt,
				     bool allow_accel)
{
	int ret = -EINVAL;

	if (allow_accel) {
		struct dma_fence *fence;

		fence = i915_ttm_accel_move(bo, clear, dst_mem, dst_ttm,
					    &dst_rsgt->table);
		if (IS_ERR(fence)) {
			ret = PTR_ERR(fence);
		} else {
			ret = dma_fence_wait(fence, false);
			if (!ret)
				ret = fence->error;
			dma_fence_put(fence);
		}
	}

	if (ret || I915_SELFTEST_ONLY(fail_gpu_migration)) {
		struct i915_ttm_memcpy_work copy_work;

		i915_ttm_memcpy_work_init(&copy_work, bo, clear, dst_mem,
					  dst_ttm, dst_rsgt);

		/* Trigger a copy by setting an error value */
		copy_work.base.dma.error = -EINVAL;
		__memcpy_work(&copy_work.base);
	}
}

static int __i915_ttm_move(struct ttm_buffer_object *bo, bool clear,
			   struct ttm_resource *dst_mem, struct ttm_tt *dst_ttm,
			   struct i915_refct_sgt *dst_rsgt, bool allow_accel)
{
	struct i915_ttm_memcpy_work *copy_work;
	struct dma_fence *fence;
	int ret;

	if (!I915_SELFTEST_ONLY(fail_work_allocation))
		copy_work = kzalloc(sizeof(*copy_work), GFP_KERNEL);
	else
		copy_work = NULL;

	if (!copy_work) {
		/* Don't fail with -ENOMEM. Move sync instead. */
		__i915_ttm_move_fallback(bo, clear, dst_mem, dst_ttm, dst_rsgt,
					 allow_accel);
		return 0;
	}

	dma_fence_work_init(&copy_work->base, &i915_ttm_memcpy_ops);
	if (allow_accel) {
		fence = i915_ttm_accel_move(bo, clear, dst_mem, dst_ttm,
					    &dst_rsgt->table);
		if (IS_ERR(fence)) {
			i915_sw_fence_set_error_once(&copy_work->base.chain,
						     PTR_ERR(fence));
		} else {
			ret = dma_fence_work_chain(&copy_work->base, fence);
			dma_fence_put(fence);
			GEM_WARN_ON(ret < 0);
		}
	} else {
		i915_sw_fence_set_error_once(&copy_work->base.chain, -EINVAL);
	}

	/* Setup async memcpy */
	i915_ttm_memcpy_work_init(copy_work, bo, clear, dst_mem, dst_ttm,
				  dst_rsgt);
	fence = dma_fence_get(&copy_work->base.dma);
	dma_fence_work_commit_imm(&copy_work->base);

	/*
	 * We're synchronizing here for now. For async moves, return the
	 * fence.
	 */
	dma_fence_wait(fence, false);
	dma_fence_put(fence);

	return ret;
}

static int i915_ttm_move(struct ttm_buffer_object *bo, bool evict,
			 struct ttm_operation_ctx *ctx,
			 struct ttm_resource *dst_mem,
			 struct ttm_place *hop)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct ttm_resource_manager *dst_man =
		ttm_manager_type(bo->bdev, dst_mem->mem_type);
	struct ttm_tt *ttm = bo->ttm;
	struct i915_refct_sgt *dst_rsgt;
	bool clear;
	int ret;

	/* Sync for now. We could do the actual copy async. */
	ret = ttm_bo_wait_ctx(bo, ctx);
	if (ret)
		return ret;

	ret = i915_ttm_move_notify(bo);
	if (ret)
		return ret;

	if (obj->mm.madv != I915_MADV_WILLNEED) {
		i915_ttm_purge(obj);
		ttm_resource_free(bo, &dst_mem);
		return 0;
	}

	/* Populate ttm with pages if needed. Typically system memory. */
	if (ttm && (dst_man->use_tt || (ttm->page_flags & TTM_TT_FLAG_SWAPPED))) {
		ret = ttm_tt_populate(bo->bdev, ttm, ctx);
		if (ret)
			return ret;
	}

	dst_rsgt = i915_ttm_resource_get_st(obj, dst_mem);
	if (IS_ERR(dst_rsgt))
		return PTR_ERR(dst_rsgt);

	clear = !cpu_maps_iomem(bo->resource) && (!ttm || !ttm_tt_is_populated(ttm));
	if (!(clear && ttm && !(ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC)))
		__i915_ttm_move(bo, clear, dst_mem, bo->ttm, dst_rsgt, true);

	ttm_bo_move_sync_cleanup(bo, dst_mem);
	i915_ttm_adjust_domains_after_move(obj);
	i915_ttm_free_cached_io_rsgt(obj);

	if (gpu_binds_iomem(dst_mem) || cpu_maps_iomem(dst_mem)) {
		obj->ttm.cached_io_rsgt = dst_rsgt;
		obj->ttm.get_io_page.sg_pos = dst_rsgt->table.sgl;
		obj->ttm.get_io_page.sg_idx = 0;
	} else {
		i915_refct_sgt_put(dst_rsgt);
	}

	i915_ttm_adjust_gem_after_move(obj);
	return 0;
}

static int i915_ttm_io_mem_reserve(struct ttm_device *bdev, struct ttm_resource *mem)
{
	if (!cpu_maps_iomem(mem))
		return 0;

	mem->bus.caching = ttm_write_combined;
	mem->bus.is_iomem = true;

	return 0;
}

static unsigned long i915_ttm_io_mem_pfn(struct ttm_buffer_object *bo,
					 unsigned long page_offset)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	unsigned long base = obj->mm.region->iomap.base - obj->mm.region->region.start;
	struct scatterlist *sg;
	unsigned int ofs;

	GEM_WARN_ON(bo->ttm);

	sg = __i915_gem_object_get_sg(obj, &obj->ttm.get_io_page, page_offset, &ofs, true);

	return ((base + sg_dma_address(sg)) >> PAGE_SHIFT) + ofs;
}

static struct ttm_device_funcs i915_ttm_bo_driver = {
	.ttm_tt_create = i915_ttm_tt_create,
	.ttm_tt_unpopulate = i915_ttm_tt_unpopulate,
	.ttm_tt_destroy = i915_ttm_tt_destroy,
	.eviction_valuable = i915_ttm_eviction_valuable,
	.evict_flags = i915_ttm_evict_flags,
	.move = i915_ttm_move,
	.swap_notify = i915_ttm_swap_notify,
	.delete_mem_notify = i915_ttm_delete_mem_notify,
	.io_mem_reserve = i915_ttm_io_mem_reserve,
	.io_mem_pfn = i915_ttm_io_mem_pfn,
};

/**
 * i915_ttm_driver - Return a pointer to the TTM device funcs
 *
 * Return: Pointer to statically allocated TTM device funcs.
 */
struct ttm_device_funcs *i915_ttm_driver(void)
{
	return &i915_ttm_bo_driver;
}

static int __i915_ttm_get_pages(struct drm_i915_gem_object *obj,
				struct ttm_placement *placement)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	int real_num_busy;
	int ret;

	/* First try only the requested placement. No eviction. */
	real_num_busy = fetch_and_zero(&placement->num_busy_placement);
	ret = ttm_bo_validate(bo, placement, &ctx);
	if (ret) {
		ret = i915_ttm_err_to_gem(ret);
		/*
		 * Anything that wants to restart the operation gets to
		 * do that.
		 */
		if (ret == -EDEADLK || ret == -EINTR || ret == -ERESTARTSYS ||
		    ret == -EAGAIN)
			return ret;

		/*
		 * If the initial attempt fails, allow all accepted placements,
		 * evicting if necessary.
		 */
		placement->num_busy_placement = real_num_busy;
		ret = ttm_bo_validate(bo, placement, &ctx);
		if (ret)
			return i915_ttm_err_to_gem(ret);
	}

	i915_ttm_adjust_lru(obj);
	if (bo->ttm && !ttm_tt_is_populated(bo->ttm)) {
		ret = ttm_tt_populate(bo->bdev, bo->ttm, &ctx);
		if (ret)
			return ret;

		i915_ttm_adjust_domains_after_move(obj);
		i915_ttm_adjust_gem_after_move(obj);
	}

	if (!i915_gem_object_has_pages(obj)) {
		struct i915_refct_sgt *rsgt =
			i915_ttm_resource_get_st(obj, bo->resource);

		if (IS_ERR(rsgt))
			return PTR_ERR(rsgt);

		GEM_BUG_ON(obj->mm.rsgt);
		obj->mm.rsgt = rsgt;
		__i915_gem_object_set_pages(obj, &rsgt->table,
					    i915_sg_dma_sizes(rsgt->table.sgl));
	}

	return ret;
}

static int i915_ttm_get_pages(struct drm_i915_gem_object *obj)
{
	struct ttm_place requested, busy[I915_TTM_MAX_PLACEMENTS];
	struct ttm_placement placement;

	GEM_BUG_ON(obj->mm.n_placements > I915_TTM_MAX_PLACEMENTS);

	/* Move to the requested placement. */
	i915_ttm_placement_from_obj(obj, &requested, busy, &placement);

	return __i915_ttm_get_pages(obj, &placement);
}

/**
 * DOC: Migration vs eviction
 *
 * GEM migration may not be the same as TTM migration / eviction. If
 * the TTM core decides to evict an object it may be evicted to a
 * TTM memory type that is not in the object's allowable GEM regions, or
 * in fact theoretically to a TTM memory type that doesn't correspond to
 * a GEM memory region. In that case the object's GEM region is not
 * updated, and the data is migrated back to the GEM region at
 * get_pages time. TTM may however set up CPU ptes to the object even
 * when it is evicted.
 * Gem forced migration using the i915_ttm_migrate() op, is allowed even
 * to regions that are not in the object's list of allowable placements.
 */
static int i915_ttm_migrate(struct drm_i915_gem_object *obj,
			    struct intel_memory_region *mr)
{
	struct ttm_place requested;
	struct ttm_placement placement;
	int ret;

	i915_ttm_place_from_region(mr, &requested, obj->flags);
	placement.num_placement = 1;
	placement.num_busy_placement = 1;
	placement.placement = &requested;
	placement.busy_placement = &requested;

	ret = __i915_ttm_get_pages(obj, &placement);
	if (ret)
		return ret;

	/*
	 * Reinitialize the region bindings. This is primarily
	 * required for objects where the new region is not in
	 * its allowable placements.
	 */
	if (obj->mm.region != mr) {
		i915_gem_object_release_memory_region(obj);
		i915_gem_object_init_memory_region(obj, mr);
	}

	return 0;
}

static void i915_ttm_put_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *st)
{
	/*
	 * We're currently not called from a shrinker, so put_pages()
	 * typically means the object is about to destroyed, or called
	 * from move_notify(). So just avoid doing much for now.
	 * If the object is not destroyed next, The TTM eviction logic
	 * and shrinkers will move it out if needed.
	 */

	if (obj->mm.rsgt) {
		i915_refct_sgt_put(obj->mm.rsgt);
		obj->mm.rsgt = NULL;
	}

	i915_ttm_adjust_lru(obj);
}

static void i915_ttm_adjust_lru(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);

	/*
	 * Don't manipulate the TTM LRUs while in TTM bo destruction.
	 * We're called through i915_ttm_delete_mem_notify().
	 */
	if (!kref_read(&bo->kref))
		return;

	/*
	 * Put on the correct LRU list depending on the MADV status
	 */
	spin_lock(&bo->bdev->lru_lock);
	if (obj->mm.madv != I915_MADV_WILLNEED) {
		bo->priority = I915_TTM_PRIO_PURGE;
	} else if (!i915_gem_object_has_pages(obj)) {
		if (bo->priority < I915_TTM_PRIO_HAS_PAGES)
			bo->priority = I915_TTM_PRIO_HAS_PAGES;
	} else {
		if (bo->priority > I915_TTM_PRIO_NO_PAGES)
			bo->priority = I915_TTM_PRIO_NO_PAGES;
	}

	ttm_bo_move_to_lru_tail(bo, bo->resource, NULL);
	spin_unlock(&bo->bdev->lru_lock);
}

/*
 * TTM-backed gem object destruction requires some clarification.
 * Basically we have two possibilities here. We can either rely on the
 * i915 delayed destruction and put the TTM object when the object
 * is idle. This would be detected by TTM which would bypass the
 * TTM delayed destroy handling. The other approach is to put the TTM
 * object early and rely on the TTM destroyed handling, and then free
 * the leftover parts of the GEM object once TTM's destroyed list handling is
 * complete. For now, we rely on the latter for two reasons:
 * a) TTM can evict an object even when it's on the delayed destroy list,
 * which in theory allows for complete eviction.
 * b) There is work going on in TTM to allow freeing an object even when
 * it's not idle, and using the TTM destroyed list handling could help us
 * benefit from that.
 */
static void i915_ttm_delayed_free(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!obj->ttm.created);

	ttm_bo_put(i915_gem_to_ttm(obj));
}

static vm_fault_t vm_fault_ttm(struct vm_fault *vmf)
{
	struct vm_area_struct *area = vmf->vma;
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(area->vm_private_data);

	/* Sanity check that we allow writing into this object */
	if (unlikely(i915_gem_object_is_readonly(obj) &&
		     area->vm_flags & VM_WRITE))
		return VM_FAULT_SIGBUS;

	return ttm_bo_vm_fault(vmf);
}

static int
vm_access_ttm(struct vm_area_struct *area, unsigned long addr,
	      void *buf, int len, int write)
{
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(area->vm_private_data);

	if (i915_gem_object_is_readonly(obj) && write)
		return -EACCES;

	return ttm_bo_vm_access(area, addr, buf, len, write);
}

static void ttm_vm_open(struct vm_area_struct *vma)
{
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(vma->vm_private_data);

	GEM_BUG_ON(!obj);
	i915_gem_object_get(obj);
}

static void ttm_vm_close(struct vm_area_struct *vma)
{
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(vma->vm_private_data);

	GEM_BUG_ON(!obj);
	i915_gem_object_put(obj);
}

static const struct vm_operations_struct vm_ops_ttm = {
	.fault = vm_fault_ttm,
	.access = vm_access_ttm,
	.open = ttm_vm_open,
	.close = ttm_vm_close,
};

static u64 i915_ttm_mmap_offset(struct drm_i915_gem_object *obj)
{
	/* The ttm_bo must be allocated with I915_BO_ALLOC_USER */
	GEM_BUG_ON(!drm_mm_node_allocated(&obj->base.vma_node.vm_node));

	return drm_vma_node_offset_addr(&obj->base.vma_node);
}

static const struct drm_i915_gem_object_ops i915_gem_ttm_obj_ops = {
	.name = "i915_gem_object_ttm",

	.get_pages = i915_ttm_get_pages,
	.put_pages = i915_ttm_put_pages,
	.truncate = i915_ttm_purge,
	.adjust_lru = i915_ttm_adjust_lru,
	.delayed_free = i915_ttm_delayed_free,
	.migrate = i915_ttm_migrate,
	.mmap_offset = i915_ttm_mmap_offset,
	.mmap_ops = &vm_ops_ttm,
};

void i915_ttm_bo_destroy(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	i915_gem_object_release_memory_region(obj);
	mutex_destroy(&obj->ttm.get_io_page.lock);

	if (obj->ttm.created) {
		i915_ttm_backup_free(obj);

		/* This releases all gem object bindings to the backend. */
		__i915_gem_free_object(obj);

		call_rcu(&obj->rcu, __i915_gem_free_object_rcu);
	} else {
		__i915_gem_object_fini(obj);
	}
}

/**
 * __i915_gem_ttm_object_init - Initialize a ttm-backed i915 gem object
 * @mem: The initial memory region for the object.
 * @obj: The gem object.
 * @size: Object size in bytes.
 * @flags: gem object flags.
 *
 * Return: 0 on success, negative error code on failure.
 */
int __i915_gem_ttm_object_init(struct intel_memory_region *mem,
			       struct drm_i915_gem_object *obj,
			       resource_size_t size,
			       resource_size_t page_size,
			       unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	enum ttm_bo_type bo_type;
	int ret;

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &i915_gem_ttm_obj_ops, &lock_class, flags);

	/* Don't put on a region list until we're either locked or fully initialized. */
	obj->mm.region = intel_memory_region_get(mem);
	INIT_LIST_HEAD(&obj->mm.region_link);

	i915_gem_object_make_unshrinkable(obj);
	INIT_RADIX_TREE(&obj->ttm.get_io_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->ttm.get_io_page.lock);
	bo_type = (obj->flags & I915_BO_ALLOC_USER) ? ttm_bo_type_device :
		ttm_bo_type_kernel;

	obj->base.vma_node.driver_private = i915_gem_to_ttm(obj);

	/* Forcing the page size is kernel internal only */
	GEM_BUG_ON(page_size && obj->mm.n_placements);

	/*
	 * If this function fails, it will call the destructor, but
	 * our caller still owns the object. So no freeing in the
	 * destructor until obj->ttm.created is true.
	 * Similarly, in delayed_destroy, we can't call ttm_bo_put()
	 * until successful initialization.
	 */
	ret = ttm_bo_init_reserved(&i915->bdev, i915_gem_to_ttm(obj), size,
				   bo_type, &i915_sys_placement,
				   page_size >> PAGE_SHIFT,
				   &ctx, NULL, NULL, i915_ttm_bo_destroy);
	if (ret)
		return i915_ttm_err_to_gem(ret);

	obj->ttm.created = true;
	i915_gem_object_release_memory_region(obj);
	i915_gem_object_init_memory_region(obj, mem);
	i915_ttm_adjust_domains_after_move(obj);
	i915_ttm_adjust_gem_after_move(obj);
	i915_gem_object_unlock(obj);

	return 0;
}

static const struct intel_memory_region_ops ttm_system_region_ops = {
	.init_object = __i915_gem_ttm_object_init,
};

struct intel_memory_region *
i915_gem_ttm_system_setup(struct drm_i915_private *i915,
			  u16 type, u16 instance)
{
	struct intel_memory_region *mr;

	mr = intel_memory_region_create(i915, 0,
					totalram_pages() << PAGE_SHIFT,
					PAGE_SIZE, 0,
					type, instance,
					&ttm_system_region_ops);
	if (IS_ERR(mr))
		return mr;

	intel_memory_region_set_name(mr, "system-ttm");
	return mr;
}

/**
 * i915_gem_obj_copy_ttm - Copy the contents of one ttm-based gem object to
 * another
 * @dst: The destination object
 * @src: The source object
 * @allow_accel: Allow using the blitter. Otherwise TTM memcpy is used.
 * @intr: Whether to perform waits interruptible:
 *
 * Note: The caller is responsible for assuring that the underlying
 * TTM objects are populated if needed and locked.
 *
 * Return: Zero on success. Negative error code on error. If @intr == true,
 * then it may return -ERESTARTSYS or -EINTR.
 */
int i915_gem_obj_copy_ttm(struct drm_i915_gem_object *dst,
			  struct drm_i915_gem_object *src,
			  bool allow_accel, bool intr)
{
	struct ttm_buffer_object *dst_bo = i915_gem_to_ttm(dst);
	struct ttm_buffer_object *src_bo = i915_gem_to_ttm(src);
	struct ttm_operation_ctx ctx = {
		.interruptible = intr,
	};
	struct i915_refct_sgt *dst_rsgt;
	int ret;

	assert_object_held(dst);
	assert_object_held(src);

	/*
	 * Sync for now. This will change with async moves.
	 */
	ret = ttm_bo_wait_ctx(dst_bo, &ctx);
	if (!ret)
		ret = ttm_bo_wait_ctx(src_bo, &ctx);
	if (ret)
		return ret;

	dst_rsgt = i915_ttm_resource_get_st(dst, dst_bo->resource);
	__i915_ttm_move(src_bo, false, dst_bo->resource, dst_bo->ttm,
			dst_rsgt, allow_accel);

	i915_refct_sgt_put(dst_rsgt);

	return 0;
}
