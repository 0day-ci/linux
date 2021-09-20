// SPDX-License-Identifier: MIT
/*
 * Copyright � 2021 Intel Corporation
 */

#include <linux/kmemleak.h>
#include <drm/drm_buddy.h>

static struct drm_buddy_block *drm_block_alloc(struct drm_buddy_mm *mm,
			struct drm_buddy_block *parent, unsigned int order,
			u64 offset)
{
	struct drm_buddy_block *block;

	BUG_ON(order > DRM_BUDDY_MAX_ORDER);

	block = kmem_cache_zalloc(mm->slab_blocks, GFP_KERNEL);
	if (!block)
		return NULL;

	block->header = offset;
	block->header |= order;
	block->parent = parent;
	block->start = offset >> PAGE_SHIFT;
	block->size = (mm->chunk_size << order) >> PAGE_SHIFT;

	BUG_ON(block->header & DRM_BUDDY_HEADER_UNUSED);
	return block;
}

static void drm_block_free(struct drm_buddy_mm *mm, struct drm_buddy_block *block)
{
	kmem_cache_free(mm->slab_blocks, block);
}

static void add_ordered(struct drm_buddy_mm *mm, struct drm_buddy_block *block)
{
	struct drm_buddy_block *node;

	if (list_empty(&mm->free_list[drm_buddy_block_order(block)])) {
		list_add(&block->link,
				&mm->free_list[drm_buddy_block_order(block)]);
		return;
	}

	list_for_each_entry(node, &mm->free_list[drm_buddy_block_order(block)], link)
		if (block->start > node->start)
			break;

	__list_add(&block->link, node->link.prev, &node->link);
}

static void mark_allocated(struct drm_buddy_block *block)
{
	block->header &= ~DRM_BUDDY_HEADER_STATE;
	block->header |= DRM_BUDDY_ALLOCATED;

	list_del(&block->link);
}

static void mark_free(struct drm_buddy_mm *mm,
		      struct drm_buddy_block *block)
{
	block->header &= ~DRM_BUDDY_HEADER_STATE;
	block->header |= DRM_BUDDY_FREE;

	add_ordered(mm, block);
}

static void mark_split(struct drm_buddy_block *block)
{
	block->header &= ~DRM_BUDDY_HEADER_STATE;
	block->header |= DRM_BUDDY_SPLIT;

	list_del(&block->link);
}

int drm_buddy_init(struct drm_buddy_mm *mm, u64 size, u64 chunk_size)
{
	unsigned int i;
	u64 offset;

	if (size < chunk_size)
		return -EINVAL;

	if (chunk_size < PAGE_SIZE)
		return -EINVAL;

	if (!is_power_of_2(chunk_size))
		return -EINVAL;

	size = round_down(size, chunk_size);

	mm->size = size;
	mm->chunk_size = chunk_size;
	mm->max_order = ilog2(size) - ilog2(chunk_size);

	BUG_ON(mm->max_order > DRM_BUDDY_MAX_ORDER);

	mm->slab_blocks = KMEM_CACHE(drm_buddy_block, SLAB_HWCACHE_ALIGN);

	if (!mm->slab_blocks)
		return -ENOMEM;

	mm->free_list = kmalloc_array(mm->max_order + 1,
				      sizeof(struct list_head),
				      GFP_KERNEL);
	if (!mm->free_list)
		goto out_destroy_slab;

	for (i = 0; i <= mm->max_order; ++i)
		INIT_LIST_HEAD(&mm->free_list[i]);

	mm->n_roots = hweight64(size);

	mm->roots = kmalloc_array(mm->n_roots,
				  sizeof(struct drm_buddy_block *),
				  GFP_KERNEL);
	if (!mm->roots)
		goto out_free_list;

	offset = 0;
	i = 0;

	/*
	 * Split into power-of-two blocks, in case we are given a size that is
	 * not itself a power-of-two.
	 */
	do {
		struct drm_buddy_block *root;
		unsigned int order;
		u64 root_size;

		root_size = rounddown_pow_of_two(size);
		order = ilog2(root_size) - ilog2(chunk_size);

		root = drm_block_alloc(mm, NULL, order, offset);
		if (!root)
			goto out_free_roots;

		mark_free(mm, root);

		BUG_ON(i > mm->max_order);
		BUG_ON(drm_buddy_block_size(mm, root) < chunk_size);

		mm->roots[i] = root;

		offset += root_size;
		size -= root_size;
		i++;
	} while (size);

	return 0;

out_free_roots:
	while (i--)
		drm_block_free(mm, mm->roots[i]);
	kfree(mm->roots);
out_free_list:
	kfree(mm->free_list);
out_destroy_slab:
	kmem_cache_destroy(mm->slab_blocks);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_buddy_init);

void drm_buddy_fini(struct drm_buddy_mm *mm)
{
	int i;

	for (i = 0; i < mm->n_roots; ++i) {
		WARN_ON(!drm_buddy_block_is_free(mm->roots[i]));
		drm_block_free(mm, mm->roots[i]);
	}

	kfree(mm->roots);
	kfree(mm->free_list);
	kmem_cache_destroy(mm->slab_blocks);
}
EXPORT_SYMBOL(drm_buddy_fini);

static int split_block(struct drm_buddy_mm *mm,
		       struct drm_buddy_block *block)
{
	unsigned int block_order = drm_buddy_block_order(block) - 1;
	u64 offset = drm_buddy_block_offset(block);

	BUG_ON(!drm_buddy_block_is_free(block));
	BUG_ON(!drm_buddy_block_order(block));

	block->left = drm_block_alloc(mm, block, block_order, offset);
	if (!block->left)
		return -ENOMEM;

	block->right = drm_block_alloc(mm, block, block_order,
					offset + (mm->chunk_size << block_order));
	if (!block->right) {
		drm_block_free(mm, block->left);
		return -ENOMEM;
	}

	mark_free(mm, block->left);
	mark_free(mm, block->right);

	mark_split(block);

	return 0;
}

static struct drm_buddy_block *
get_buddy(struct drm_buddy_block *block)
{
	struct drm_buddy_block *parent;

	parent = block->parent;
	if (!parent)
		return NULL;

	if (parent->left == block)
		return parent->right;

	return parent->left;
}

static void __drm_buddy_free(struct drm_buddy_mm *mm,
			      struct drm_buddy_block *block)
{
	struct drm_buddy_block *parent;

	while ((parent = block->parent)) {
		struct drm_buddy_block *buddy;

		buddy = get_buddy(block);

		if (!drm_buddy_block_is_free(buddy))
			break;

		list_del(&buddy->link);

		drm_block_free(mm, block);
		drm_block_free(mm, buddy);

		block = parent;
	}

	mark_free(mm, block);
}

void drm_buddy_free(struct drm_buddy_mm *mm,
		     struct drm_buddy_block *block)
{
	BUG_ON(!drm_buddy_block_is_allocated(block));
	__drm_buddy_free(mm, block);
}
EXPORT_SYMBOL(drm_buddy_free);

void drm_buddy_free_list(struct drm_buddy_mm *mm, struct list_head *objects)
{
	struct drm_buddy_block *block, *on;

	list_for_each_entry_safe(block, on, objects, link) {
		drm_buddy_free(mm, block);
		cond_resched();
	}
	INIT_LIST_HEAD(objects);
}
EXPORT_SYMBOL(drm_buddy_free_list);

/*
 * Allocate power-of-two block. The order value here translates to:
 *
 *   0 = 2^0 * mm->chunk_size
 *   1 = 2^1 * mm->chunk_size
 *   2 = 2^2 * mm->chunk_size
 *   ...
 */
struct drm_buddy_block *
drm_buddy_alloc(struct drm_buddy_mm *mm, unsigned int order,
		bool bar_enabled, unsigned int limit,
		enum drm_buddy_alloc_mode mode)
{
	struct drm_buddy_block *block = NULL;
	unsigned int pages;
	unsigned int i;
	int err;

	pages = (mm->chunk_size << order) >> PAGE_SHIFT;

	for (i = order; i <= mm->max_order; ++i) {
		if (mode == DRM_BUDDY_TOP_DOWN) {
			if (!list_empty(&mm->free_list[i])) {
				block = list_first_entry(&mm->free_list[i],
						struct drm_buddy_block, link);

				if (bar_enabled) {
					if (!(block->start > limit))
						continue;
				}

				break;
			}
		} else if (mode == DRM_BUDDY_BOTTOM_UP) {
			if (!list_empty(&mm->free_list[i])) {
				block = list_last_entry(&mm->free_list[i],
						struct drm_buddy_block, link);

				if (bar_enabled) {
					if (!(block->start < limit &&
							(block->start + pages) < limit))
						continue;
				}

				break;
			}
		}
	}

	if (!block)
		return ERR_PTR(-ENOSPC);

	BUG_ON(!drm_buddy_block_is_free(block));

	while (i != order) {
		err = split_block(mm, block);
		if (unlikely(err))
			goto out_free;

		/* Go low */
		if (mode == DRM_BUDDY_TOP_DOWN)
			block = block->right;
		else
			block = block->left;
		i--;
	}

	if (mode == DRM_BUDDY_TOP_DOWN && bar_enabled) {
		if (!(block->start > limit))
			return ERR_PTR(-ENOSPC);
	} else if (mode == DRM_BUDDY_BOTTOM_UP && bar_enabled) {
		if (!(block->start < limit &&
				(block->start + pages) < limit))
			return ERR_PTR(-ENOSPC);
	}

	mark_allocated(block);
	kmemleak_update_trace(block);
	return block;

out_free:
	if (i != order)
		__drm_buddy_free(mm, block);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(drm_buddy_alloc);

static inline bool overlaps(u64 s1, u64 e1, u64 s2, u64 e2)
{
	return s1 <= e2 && e1 >= s2;
}

static inline bool contains(u64 s1, u64 e1, u64 s2, u64 e2)
{
	return s1 <= s2 && e1 >= e2;
}

/*
 * Allocate range. Note that it's safe to chain together multiple alloc_ranges
 * with the same blocks list.
 *
 * Intended for pre-allocating portions of the address space, for example to
 * reserve a block for the initial framebuffer or similar, hence the expectation
 * here is that drm_buddy_alloc() is still the main vehicle for
 * allocations, so if that's not the case then the drm_mm range allocator is
 * probably a much better fit, and so you should probably go use that instead.
 */
int drm_buddy_alloc_range(struct drm_buddy_mm *mm,
			struct list_head *blocks,
			u64 start, u64 size)
{
	struct drm_buddy_block *block;
	struct drm_buddy_block *buddy;
	LIST_HEAD(allocated);
	LIST_HEAD(dfs);
	u64 end;
	int err;
	int i;

	if (size < mm->chunk_size)
		return -EINVAL;

	if (!IS_ALIGNED(size | start, mm->chunk_size))
		return -EINVAL;

	if (range_overflows(start, size, mm->size))
		return -EINVAL;

	for (i = 0; i < mm->n_roots; ++i)
		list_add_tail(&mm->roots[i]->tmp_link, &dfs);

	end = start + size - 1;

	do {
		u64 block_start;
		u64 block_end;

		block = list_first_entry_or_null(&dfs,
						 struct drm_buddy_block,
						 tmp_link);
		if (!block)
			break;

		list_del(&block->tmp_link);

		block_start = drm_buddy_block_offset(block);
		block_end = block_start + drm_buddy_block_size(mm, block) - 1;

		if (!overlaps(start, end, block_start, block_end))
			continue;

		if (drm_buddy_block_is_allocated(block)) {
			err = -ENOSPC;
			goto err_free;
		}

		if (contains(start, end, block_start, block_end)) {
			if (!drm_buddy_block_is_free(block)) {
				err = -ENOSPC;
				goto err_free;
			}

			mark_allocated(block);
			list_add_tail(&block->link, &allocated);
			continue;
		}

		if (!drm_buddy_block_is_split(block)) {
			err = split_block(mm, block);
			if (unlikely(err))
				goto err_undo;
		}

		list_add(&block->right->tmp_link, &dfs);
		list_add(&block->left->tmp_link, &dfs);
	} while (1);

	list_splice_tail(&allocated, blocks);
	return 0;

err_undo:
	/*
	 * We really don't want to leave around a bunch of split blocks, since
	 * bigger is better, so make sure we merge everything back before we
	 * free the allocated blocks.
	 */
	buddy = get_buddy(block);
	if (buddy &&
	    (drm_buddy_block_is_free(block) &&
	     drm_buddy_block_is_free(buddy)))
		__drm_buddy_free(mm, block);

err_free:
	drm_buddy_free_list(mm, &allocated);
	return err;
}
EXPORT_SYMBOL(drm_buddy_alloc_range);
