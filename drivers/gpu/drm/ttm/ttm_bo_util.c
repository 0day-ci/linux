/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2007-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_memcpy.h>
#include <drm/drm_vma_manager.h>
#include <linux/dma-buf-map.h>
#include <linux/io.h>
#include <linux/highmem.h>
#include <linux/io-mapping.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/dma-resv.h>
#include <linux/scatterlist.h>

struct ttm_transfer_obj {
	struct ttm_buffer_object base;
	struct ttm_buffer_object *bo;
};

int ttm_mem_io_reserve(struct ttm_device *bdev,
		       struct ttm_resource *mem)
{
	if (mem->bus.offset || mem->bus.addr)
		return 0;

	mem->bus.is_iomem = false;
	if (!bdev->funcs->io_mem_reserve)
		return 0;

	return bdev->funcs->io_mem_reserve(bdev, mem);
}

void ttm_mem_io_free(struct ttm_device *bdev,
		     struct ttm_resource *mem)
{
	if (!mem->bus.offset && !mem->bus.addr)
		return;

	if (bdev->funcs->io_mem_free)
		bdev->funcs->io_mem_free(bdev, mem);

	mem->bus.offset = 0;
	mem->bus.addr = NULL;
}

static pgprot_t ttm_prot_from_caching(enum ttm_caching caching, pgprot_t tmp)
{
	/* Cached mappings need no adjustment */
	if (caching == ttm_cached)
		return tmp;

#if defined(__i386__) || defined(__x86_64__)
	if (caching == ttm_write_combined)
		tmp = pgprot_writecombine(tmp);
	else if (boot_cpu_data.x86 > 3)
		tmp = pgprot_noncached(tmp);
#endif
#if defined(__ia64__) || defined(__arm__) || defined(__aarch64__) || \
	defined(__powerpc__) || defined(__mips__)
	if (caching == ttm_write_combined)
		tmp = pgprot_writecombine(tmp);
	else
		tmp = pgprot_noncached(tmp);
#endif
#if defined(__sparc__)
	tmp = pgprot_noncached(tmp);
#endif
	return tmp;
}

static void ttm_kmap_iter_tt_kmap_local(struct ttm_kmap_iter *iter,
					struct dma_buf_map *dmap,
					pgoff_t i)
{
	struct ttm_kmap_iter_tt *iter_tt =
		container_of(iter, typeof(*iter_tt), base);

	dma_buf_map_set_vaddr(dmap, kmap_local_page_prot(iter_tt->tt->pages[i],
							 iter_tt->prot));
}

static void ttm_kmap_iter_iomap_kmap_local(struct ttm_kmap_iter *iter,
					   struct dma_buf_map *dmap,
					   pgoff_t i)
{
	struct ttm_kmap_iter_iomap *iter_io =
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

static const struct ttm_kmap_iter_ops ttm_kmap_iter_tt_ops = {
	.kmap_local = ttm_kmap_iter_tt_kmap_local,
	.needs_unmap = true
};

static const struct ttm_kmap_iter_ops ttm_kmap_iter_io_ops = {
	.kmap_local =  ttm_kmap_iter_iomap_kmap_local,
	.needs_unmap = true
};

/* If needed, make unmap functionality part of ttm_kmap_iter_ops */
static void kunmap_local_iter(struct ttm_kmap_iter *iter,
			      struct dma_buf_map *map)
{
	if (!iter->ops->needs_unmap)
		return;

	if (map->is_iomem)
		io_mapping_unmap_local(map->vaddr_iomem);
	else
		kunmap_local(map->vaddr);
}

/**
 * ttm_move_memcpy - Helper to perform a memcpy ttm move operation.
 * @bo: The struct ttm_buffer_object.
 * @new_mem: The struct ttm_resource we're moving to (copy destination).
 * @new_iter: A struct ttm_kmap_iter representing the destination resource.
 * @old_iter: A struct ttm_kmap_iter representing the source resource.
 *
 * This function is intended to be able to move out async under a
 * dma-fence if desired.
 */
void ttm_move_memcpy(struct ttm_buffer_object *bo,
		     struct ttm_resource *new_mem,
		     struct ttm_kmap_iter *new_iter,
		     struct ttm_kmap_iter *old_iter)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource *old_mem = &bo->mem;
	struct ttm_resource_manager *old_man = ttm_manager_type(bdev, old_mem->mem_type);
	struct dma_buf_map old_map, new_map;
	bool wc_memcpy;
	pgoff_t i;

	/* Single TTM move. NOP */
	if (old_man->use_tt && man->use_tt)
		return;

	/* Don't move nonexistent data. Clear destination instead. */
	if (old_man->use_tt && !man->use_tt &&
	    (!ttm || !ttm_tt_is_populated(ttm))) {
		if (ttm && !(ttm->page_flags & TTM_PAGE_FLAG_ZERO_ALLOC))
			return;

		for (i = 0; i < new_mem->num_pages; ++i) {
			new_iter->ops->kmap_local(new_iter, &new_map, i);
			if (new_map.is_iomem)
				memset_io(new_map.vaddr_iomem, 0, PAGE_SIZE);
			else
				memset(new_map.vaddr, 0, PAGE_SIZE);
			kunmap_local_iter(new_iter, &new_map);
		}
		return;
	}

	wc_memcpy = ((!old_man->use_tt || bo->ttm->caching != ttm_cached) &&
		     drm_has_memcpy_from_wc());

	/*
	 * We use some nasty aliasing for drm_memcpy_from_wc, but assuming
	 * that we can move to memremapping in the not too distant future,
	 * reduce the fragility for now with a build assert.
	 */
	BUILD_BUG_ON(offsetof(typeof(old_map), vaddr) !=
		     offsetof(typeof(old_map), vaddr_iomem));

	for (i = 0; i < new_mem->num_pages; ++i) {
		new_iter->ops->kmap_local(new_iter, &new_map, i);
		old_iter->ops->kmap_local(old_iter, &old_map, i);

		if (wc_memcpy) {
			drm_memcpy_from_wc(new_map.vaddr, old_map.vaddr,
					   PAGE_SIZE);
		} else if (!old_map.is_iomem && !new_map.is_iomem) {
			memcpy(new_map.vaddr, old_map.vaddr, PAGE_SIZE);
		} else if (!old_map.is_iomem) {
			dma_buf_map_memcpy_to(&new_map, old_map.vaddr,
					      PAGE_SIZE);
		} else if (!new_map.is_iomem) {
			memcpy_fromio(new_map.vaddr, old_map.vaddr_iomem,
				      PAGE_SIZE);
		} else {
			int j;
			u32 __iomem *src = old_map.vaddr_iomem;
			u32 __iomem *dst = new_map.vaddr_iomem;

			for (j = 0; j < (PAGE_SIZE >> 2); ++j)
				iowrite32(ioread32(src++), dst++);
		}
		kunmap_local_iter(old_iter, &old_map);
		kunmap_local_iter(new_iter, &new_map);
	}
}
EXPORT_SYMBOL(ttm_move_memcpy);

/**
 * ttm_kmap_iter_iomap_init - Initialize a struct ttm_kmap_iter_iomap
 * @iter_io: The struct ttm_kmap_iter_iomap to initialize.
 * @iomap: The struct io_mapping representing the underlying linear io_memory.
 * @st: sg_table into @iomap, representing the memory of the struct
 * ttm_resource.
 * @start: Offset that needs to be subtracted from @st to make
 * sg_dma_address(st->sgl) - @start == 0 for @iomap start.
 *
 * Return: Pointer to the embedded struct ttm_kmap_iter.
 */
struct ttm_kmap_iter *
ttm_kmap_iter_iomap_init(struct ttm_kmap_iter_iomap *iter_io,
			 struct io_mapping *iomap,
			 struct sg_table *st,
			 resource_size_t start)
{
	iter_io->base.ops = &ttm_kmap_iter_io_ops;
	iter_io->iomap = iomap;
	iter_io->st = st;
	iter_io->start = start;
	memset(&iter_io->cache, 0, sizeof(iter_io->cache));

	return &iter_io->base;
}
EXPORT_SYMBOL(ttm_kmap_iter_iomap_init);

/**
 * ttm_kmap_iter_tt_init - Initialize a struct ttm_kmap_iter_tt
 * @iter_tt: The struct ttm_kmap_iter_tt to initialize.
 * @tt: Struct ttm_tt holding page pointers of the struct ttm_resource.
 *
 * Return: Pointer to the embedded struct ttm_kmap_iter.
 */
struct ttm_kmap_iter *
ttm_kmap_iter_tt_init(struct ttm_kmap_iter_tt *iter_tt,
		      struct ttm_tt *tt)
{
	iter_tt->base.ops = &ttm_kmap_iter_tt_ops;
	iter_tt->tt = tt;
	iter_tt->prot = ttm_prot_from_caching(tt->caching, PAGE_KERNEL);

	return &iter_tt->base;
}
EXPORT_SYMBOL(ttm_kmap_iter_tt_init);

/**
 * DOC: Linear io iterator
 *
 * This code should die in the not too near future. Best would be if we could
 * make io-mapping use memremap for all io memory, and have memremap
 * implement a kmap_local functionality. We could then strip a huge amount of
 * code. These linear io iterators are implemented to mimic old functionality,
 * and they don't use kmap_local semantics at all internally. Rather ioremap or
 * friends, and at least on 32-bit they add global TLB flushes and points
 * of failure.
 */

/**
 * struct ttm_kmap_iter_linear_io - Iterator specialization for linear io
 * @base: The base iterator
 * @dmap: Points to the starting address of the region
 * @needs_unmap: Whether we need to unmap on fini
 */
struct ttm_kmap_iter_linear_io {
	struct ttm_kmap_iter base;
	struct dma_buf_map dmap;
	bool needs_unmap;
};

static void ttm_kmap_iter_linear_io_kmap_local(struct ttm_kmap_iter *iter,
					       struct dma_buf_map *dmap,
					       pgoff_t i)
{
	struct ttm_kmap_iter_linear_io *iter_io =
		container_of(iter, typeof(*iter_io), base);

	*dmap = iter_io->dmap;
	dma_buf_map_incr(dmap, i * PAGE_SIZE);
}

static const struct ttm_kmap_iter_ops ttm_kmap_iter_linear_io_ops = {
	.kmap_local =  ttm_kmap_iter_linear_io_kmap_local
};

static struct ttm_kmap_iter *
ttm_kmap_iter_linear_io_init(struct ttm_kmap_iter_linear_io *iter_io,
			     struct ttm_device *bdev,
			     struct ttm_resource *mem)
{
	int ret;

	ret = ttm_mem_io_reserve(bdev, mem);
	if (ret)
		goto out_err;
	if (!mem->bus.is_iomem) {
		ret = -EINVAL;
		goto out_io_free;
	}

	if (mem->bus.addr) {
		dma_buf_map_set_vaddr(&iter_io->dmap, mem->bus.addr);
		iter_io->needs_unmap = false;
	} else {
		size_t bus_size = (size_t)mem->num_pages << PAGE_SHIFT;

		iter_io->needs_unmap = true;
		if (mem->bus.caching == ttm_write_combined)
			dma_buf_map_set_vaddr_iomem(&iter_io->dmap,
						    ioremap_wc(mem->bus.offset,
							       bus_size));
		else if (mem->bus.caching == ttm_cached)
			dma_buf_map_set_vaddr(&iter_io->dmap,
					      memremap(mem->bus.offset, bus_size,
						       MEMREMAP_WB));
		else
			dma_buf_map_set_vaddr_iomem(&iter_io->dmap,
						    ioremap(mem->bus.offset,
							    bus_size));
		if (dma_buf_map_is_null(&iter_io->dmap)) {
			ret = -ENOMEM;
			goto out_io_free;
		}
	}

	iter_io->base.ops = &ttm_kmap_iter_linear_io_ops;
	return &iter_io->base;

out_io_free:
	ttm_mem_io_free(bdev, mem);
out_err:
	return ERR_PTR(ret);
}

static void
ttm_kmap_iter_linear_io_fini(struct ttm_kmap_iter_linear_io *iter_io,
			     struct ttm_device *bdev,
			     struct ttm_resource *mem)
{
	if (iter_io->needs_unmap && dma_buf_map_is_set(&iter_io->dmap)) {
		if (iter_io->dmap.is_iomem)
			iounmap(iter_io->dmap.vaddr_iomem);
		else
			memunmap(iter_io->dmap.vaddr);
	}

	ttm_mem_io_free(bdev, mem);
}

static int ttm_bo_wait_free_node(struct ttm_buffer_object *bo,
				 bool dst_use_tt);

int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
		       struct ttm_operation_ctx *ctx,
		       struct ttm_resource *new_mem)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	struct ttm_resource_manager *new_man =
		ttm_manager_type(bo->bdev, new_mem->mem_type);
	struct ttm_tt *ttm = bo->ttm;
	struct ttm_resource *old_mem = &bo->mem;
	struct ttm_resource old_copy = *old_mem;
	union {
		struct ttm_kmap_iter_tt tt;
		struct ttm_kmap_iter_linear_io io;
	} _new_iter, _old_iter;
	struct ttm_kmap_iter *new_iter, *old_iter;
	int ret;

	if (ttm) {
		ret = ttm_tt_populate(bdev, ttm, ctx);
		if (ret)
			return ret;
	}

	new_iter = new_man->use_tt ?
		ttm_kmap_iter_tt_init(&_new_iter.tt, bo->ttm) :
		ttm_kmap_iter_linear_io_init(&_new_iter.io, bdev, new_mem);
	if (IS_ERR(new_iter))
		return PTR_ERR(new_iter);

	old_iter = man->use_tt ?
		ttm_kmap_iter_tt_init(&_old_iter.tt, bo->ttm) :
		ttm_kmap_iter_linear_io_init(&_old_iter.io, bdev, old_mem);
	if (IS_ERR(old_iter)) {
		ret = PTR_ERR(old_iter);
		goto out_old_iter;
	}

	ttm_move_memcpy(bo, new_mem, new_iter, old_iter);
	old_copy = *old_mem;
	ret = ttm_bo_wait_free_node(bo, new_man->use_tt);

	if (!man->use_tt)
		ttm_kmap_iter_linear_io_fini(&_old_iter.io, bdev, &old_copy);
out_old_iter:
	if (!new_man->use_tt)
		ttm_kmap_iter_linear_io_fini(&_new_iter.io, bdev, new_mem);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_move_memcpy);

static void ttm_transfered_destroy(struct ttm_buffer_object *bo)
{
	struct ttm_transfer_obj *fbo;

	fbo = container_of(bo, struct ttm_transfer_obj, base);
	ttm_bo_put(fbo->bo);
	kfree(fbo);
}

/**
 * ttm_buffer_object_transfer
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @new_obj: A pointer to a pointer to a newly created ttm_buffer_object,
 * holding the data of @bo with the old placement.
 *
 * This is a utility function that may be called after an accelerated move
 * has been scheduled. A new buffer object is created as a placeholder for
 * the old data while it's being copied. When that buffer object is idle,
 * it can be destroyed, releasing the space of the old placement.
 * Returns:
 * !0: Failure.
 */

static int ttm_buffer_object_transfer(struct ttm_buffer_object *bo,
				      struct ttm_buffer_object **new_obj)
{
	struct ttm_transfer_obj *fbo;
	int ret;

	fbo = kmalloc(sizeof(*fbo), GFP_KERNEL);
	if (!fbo)
		return -ENOMEM;

	fbo->base = *bo;

	ttm_bo_get(bo);
	fbo->bo = bo;

	/**
	 * Fix up members that we shouldn't copy directly:
	 * TODO: Explicit member copy would probably be better here.
	 */

	atomic_inc(&ttm_glob.bo_count);
	INIT_LIST_HEAD(&fbo->base.ddestroy);
	INIT_LIST_HEAD(&fbo->base.lru);
	fbo->base.moving = NULL;
	drm_vma_node_reset(&fbo->base.base.vma_node);

	kref_init(&fbo->base.kref);
	fbo->base.destroy = &ttm_transfered_destroy;
	fbo->base.pin_count = 0;
	if (bo->type != ttm_bo_type_sg)
		fbo->base.base.resv = &fbo->base.base._resv;

	dma_resv_init(&fbo->base.base._resv);
	fbo->base.base.dev = NULL;
	ret = dma_resv_trylock(&fbo->base.base._resv);
	WARN_ON(!ret);

	ttm_bo_move_to_lru_tail_unlocked(&fbo->base);

	*new_obj = &fbo->base;
	return 0;
}

pgprot_t ttm_io_prot(struct ttm_buffer_object *bo, struct ttm_resource *res,
		     pgprot_t tmp)
{
	struct ttm_resource_manager *man;
	enum ttm_caching caching;

	man = ttm_manager_type(bo->bdev, res->mem_type);
	caching = man->use_tt ? bo->ttm->caching : res->bus.caching;

	return ttm_prot_from_caching(caching, tmp);
}
EXPORT_SYMBOL(ttm_io_prot);

static int ttm_bo_ioremap(struct ttm_buffer_object *bo,
			  unsigned long offset,
			  unsigned long size,
			  struct ttm_bo_kmap_obj *map)
{
	struct ttm_resource *mem = &bo->mem;

	if (bo->mem.bus.addr) {
		map->bo_kmap_type = ttm_bo_map_premapped;
		map->virtual = (void *)(((u8 *)bo->mem.bus.addr) + offset);
	} else {
		map->bo_kmap_type = ttm_bo_map_iomap;
		if (mem->bus.caching == ttm_write_combined)
			map->virtual = ioremap_wc(bo->mem.bus.offset + offset,
						  size);
#ifdef CONFIG_X86
		else if (mem->bus.caching == ttm_cached)
			map->virtual = ioremap_cache(bo->mem.bus.offset + offset,
						  size);
#endif
		else
			map->virtual = ioremap(bo->mem.bus.offset + offset,
					       size);
	}
	return (!map->virtual) ? -ENOMEM : 0;
}

static int ttm_bo_kmap_ttm(struct ttm_buffer_object *bo,
			   unsigned long start_page,
			   unsigned long num_pages,
			   struct ttm_bo_kmap_obj *map)
{
	struct ttm_resource *mem = &bo->mem;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	struct ttm_tt *ttm = bo->ttm;
	pgprot_t prot;
	int ret;

	BUG_ON(!ttm);

	ret = ttm_tt_populate(bo->bdev, ttm, &ctx);
	if (ret)
		return ret;

	if (num_pages == 1 && ttm->caching == ttm_cached) {
		/*
		 * We're mapping a single page, and the desired
		 * page protection is consistent with the bo.
		 */

		map->bo_kmap_type = ttm_bo_map_kmap;
		map->page = ttm->pages[start_page];
		map->virtual = kmap(map->page);
	} else {
		/*
		 * We need to use vmap to get the desired page protection
		 * or to make the buffer object look contiguous.
		 */
		prot = ttm_io_prot(bo, mem, PAGE_KERNEL);
		map->bo_kmap_type = ttm_bo_map_vmap;
		map->virtual = vmap(ttm->pages + start_page, num_pages,
				    0, prot);
	}
	return (!map->virtual) ? -ENOMEM : 0;
}

int ttm_bo_kmap(struct ttm_buffer_object *bo,
		unsigned long start_page, unsigned long num_pages,
		struct ttm_bo_kmap_obj *map)
{
	unsigned long offset, size;
	int ret;

	map->virtual = NULL;
	map->bo = bo;
	if (num_pages > bo->mem.num_pages)
		return -EINVAL;
	if ((start_page + num_pages) > bo->mem.num_pages)
		return -EINVAL;

	ret = ttm_mem_io_reserve(bo->bdev, &bo->mem);
	if (ret)
		return ret;
	if (!bo->mem.bus.is_iomem) {
		return ttm_bo_kmap_ttm(bo, start_page, num_pages, map);
	} else {
		offset = start_page << PAGE_SHIFT;
		size = num_pages << PAGE_SHIFT;
		return ttm_bo_ioremap(bo, offset, size, map);
	}
}
EXPORT_SYMBOL(ttm_bo_kmap);

void ttm_bo_kunmap(struct ttm_bo_kmap_obj *map)
{
	if (!map->virtual)
		return;
	switch (map->bo_kmap_type) {
	case ttm_bo_map_iomap:
		iounmap(map->virtual);
		break;
	case ttm_bo_map_vmap:
		vunmap(map->virtual);
		break;
	case ttm_bo_map_kmap:
		kunmap(map->page);
		break;
	case ttm_bo_map_premapped:
		break;
	default:
		BUG();
	}
	ttm_mem_io_free(map->bo->bdev, &map->bo->mem);
	map->virtual = NULL;
	map->page = NULL;
}
EXPORT_SYMBOL(ttm_bo_kunmap);

int ttm_bo_vmap(struct ttm_buffer_object *bo, struct dma_buf_map *map)
{
	struct ttm_resource *mem = &bo->mem;
	int ret;

	ret = ttm_mem_io_reserve(bo->bdev, mem);
	if (ret)
		return ret;

	if (mem->bus.is_iomem) {
		void __iomem *vaddr_iomem;

		if (mem->bus.addr)
			vaddr_iomem = (void __iomem *)mem->bus.addr;
		else if (mem->bus.caching == ttm_write_combined)
			vaddr_iomem = ioremap_wc(mem->bus.offset,
						 bo->base.size);
#ifdef CONFIG_X86
		else if (mem->bus.caching == ttm_cached)
			vaddr_iomem = ioremap_cache(mem->bus.offset,
						  bo->base.size);
#endif
		else
			vaddr_iomem = ioremap(mem->bus.offset, bo->base.size);

		if (!vaddr_iomem)
			return -ENOMEM;

		dma_buf_map_set_vaddr_iomem(map, vaddr_iomem);

	} else {
		struct ttm_operation_ctx ctx = {
			.interruptible = false,
			.no_wait_gpu = false
		};
		struct ttm_tt *ttm = bo->ttm;
		pgprot_t prot;
		void *vaddr;

		ret = ttm_tt_populate(bo->bdev, ttm, &ctx);
		if (ret)
			return ret;

		/*
		 * We need to use vmap to get the desired page protection
		 * or to make the buffer object look contiguous.
		 */
		prot = ttm_io_prot(bo, mem, PAGE_KERNEL);
		vaddr = vmap(ttm->pages, ttm->num_pages, 0, prot);
		if (!vaddr)
			return -ENOMEM;

		dma_buf_map_set_vaddr(map, vaddr);
	}

	return 0;
}
EXPORT_SYMBOL(ttm_bo_vmap);

void ttm_bo_vunmap(struct ttm_buffer_object *bo, struct dma_buf_map *map)
{
	struct ttm_resource *mem = &bo->mem;

	if (dma_buf_map_is_null(map))
		return;

	if (!map->is_iomem)
		vunmap(map->vaddr);
	else if (!mem->bus.addr)
		iounmap(map->vaddr_iomem);
	dma_buf_map_clear(map);

	ttm_mem_io_free(bo->bdev, &bo->mem);
}
EXPORT_SYMBOL(ttm_bo_vunmap);

static int ttm_bo_wait_free_node(struct ttm_buffer_object *bo,
				 bool dst_use_tt)
{
	int ret;
	ret = ttm_bo_wait(bo, false, false);
	if (ret)
		return ret;

	if (!dst_use_tt)
		ttm_bo_tt_destroy(bo);
	ttm_resource_free(bo, &bo->mem);
	return 0;
}

static int ttm_bo_move_to_ghost(struct ttm_buffer_object *bo,
				struct dma_fence *fence,
				bool dst_use_tt)
{
	struct ttm_buffer_object *ghost_obj;
	int ret;

	/**
	 * This should help pipeline ordinary buffer moves.
	 *
	 * Hang old buffer memory on a new buffer object,
	 * and leave it to be released when the GPU
	 * operation has completed.
	 */

	dma_fence_put(bo->moving);
	bo->moving = dma_fence_get(fence);

	ret = ttm_buffer_object_transfer(bo, &ghost_obj);
	if (ret)
		return ret;

	dma_resv_add_excl_fence(&ghost_obj->base._resv, fence);

	/**
	 * If we're not moving to fixed memory, the TTM object
	 * needs to stay alive. Otherwhise hang it on the ghost
	 * bo to be unbound and destroyed.
	 */

	if (dst_use_tt)
		ghost_obj->ttm = NULL;
	else
		bo->ttm = NULL;

	dma_resv_unlock(&ghost_obj->base._resv);
	ttm_bo_put(ghost_obj);
	return 0;
}

static void ttm_bo_move_pipeline_evict(struct ttm_buffer_object *bo,
				       struct dma_fence *fence)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *from = ttm_manager_type(bdev, bo->mem.mem_type);

	/**
	 * BO doesn't have a TTM we need to bind/unbind. Just remember
	 * this eviction and free up the allocation
	 */
	spin_lock(&from->move_lock);
	if (!from->move || dma_fence_is_later(fence, from->move)) {
		dma_fence_put(from->move);
		from->move = dma_fence_get(fence);
	}
	spin_unlock(&from->move_lock);

	ttm_resource_free(bo, &bo->mem);

	dma_fence_put(bo->moving);
	bo->moving = dma_fence_get(fence);
}

int ttm_bo_move_accel_cleanup(struct ttm_buffer_object *bo,
			      struct dma_fence *fence,
			      bool evict,
			      bool pipeline,
			      struct ttm_resource *new_mem)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *from = ttm_manager_type(bdev, bo->mem.mem_type);
	struct ttm_resource_manager *man = ttm_manager_type(bdev, new_mem->mem_type);
	int ret = 0;

	dma_resv_add_excl_fence(bo->base.resv, fence);
	if (!evict)
		ret = ttm_bo_move_to_ghost(bo, fence, man->use_tt);
	else if (!from->use_tt && pipeline)
		ttm_bo_move_pipeline_evict(bo, fence);
	else
		ret = ttm_bo_wait_free_node(bo, man->use_tt);

	if (ret)
		return ret;

	ttm_bo_assign_mem(bo, new_mem);

	return 0;
}
EXPORT_SYMBOL(ttm_bo_move_accel_cleanup);

int ttm_bo_pipeline_gutting(struct ttm_buffer_object *bo)
{
	static const struct ttm_place sys_mem = { .mem_type = TTM_PL_SYSTEM };
	struct ttm_buffer_object *ghost;
	int ret;

	ret = ttm_buffer_object_transfer(bo, &ghost);
	if (ret)
		return ret;

	ret = dma_resv_copy_fences(&ghost->base._resv, bo->base.resv);
	/* Last resort, wait for the BO to be idle when we are OOM */
	if (ret)
		ttm_bo_wait(bo, false, false);

	ttm_resource_alloc(bo, &sys_mem, &bo->mem);
	bo->ttm = NULL;

	dma_resv_unlock(&ghost->base._resv);
	ttm_bo_put(ghost);

	return 0;
}
