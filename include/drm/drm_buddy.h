/* SPDX-License-Identifier: MIT */
/*
 * Copyright � 2021 Intel Corporation
 */

#ifndef __DRM_BUDDY_H__
#define __DRM_BUDDY_H__

#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>

#define range_overflows(start, size, max) ({ \
	typeof(start) start__ = (start); \
	typeof(size) size__ = (size); \
	typeof(max) max__ = (max); \
	(void)(&start__ == &size__); \
	(void)(&start__ == &max__); \
	start__ >= max__ || size__ > max__ - start__; \
})

enum drm_buddy_alloc_mode {
	 DRM_BUDDY_TOP_DOWN = 0,
	 DRM_BUDDY_BOTTOM_UP,
	 DRM_BUDDY_ALLOC_RANGE
};

struct drm_buddy_block {
#define DRM_BUDDY_HEADER_OFFSET GENMASK_ULL(63, 12)
#define DRM_BUDDY_HEADER_STATE  GENMASK_ULL(11, 10)
#define   DRM_BUDDY_ALLOCATED	   (1 << 10)
#define   DRM_BUDDY_FREE	   (2 << 10)
#define   DRM_BUDDY_SPLIT	   (3 << 10)
/* Free to be used, if needed in the future */
#define DRM_BUDDY_HEADER_UNUSED GENMASK_ULL(9, 6)
#define DRM_BUDDY_HEADER_ORDER  GENMASK_ULL(5, 0)
	u64 header;
	/* Store start and size fields in pages */
	u64 start;
	u64 size;

	struct drm_buddy_block *left;
	struct drm_buddy_block *right;
	struct drm_buddy_block *parent;

	void *private; /* owned by creator */

	/*
	 * While the block is allocated by the user through drm_buddy_alloc*,
	 * the user has ownership of the link, for example to maintain within
	 * a list, if so desired. As soon as the block is freed with
	 * drm_buddy_free* ownership is given back to the mm.
	 */
	struct list_head link;
	struct list_head tmp_link;
};

/* Order-zero must be at least PAGE_SIZE */
#define DRM_BUDDY_MAX_ORDER (63 - PAGE_SHIFT)

/*
 * Binary Buddy System.
 *
 * Locking should be handled by the user, a simple mutex around
 * drm_buddy_alloc* and drm_buddy_free* should suffice.
 */
struct drm_buddy_mm {
	struct kmem_cache *slab_blocks;
	/* Maintain a free list for each order. */
	struct list_head *free_list;

	/*
	 * Maintain explicit binary tree(s) to track the allocation of the
	 * address space. This gives us a simple way of finding a buddy block
	 * and performing the potentially recursive merge step when freeing a
	 * block.  Nodes are either allocated or free, in which case they will
	 * also exist on the respective free list.
	 */
	struct drm_buddy_block **roots;

	/*
	 * Anything from here is public, and remains static for the lifetime of
	 * the mm. Everything above is considered do-not-touch.
	 */
	unsigned int n_roots;
	unsigned int max_order;

	/* Must be at least PAGE_SIZE */
	u64 chunk_size;
	u64 size;
};

static inline u64
drm_buddy_block_offset(struct drm_buddy_block *block)
{
	return block->header & DRM_BUDDY_HEADER_OFFSET;
}

static inline unsigned int
drm_buddy_block_order(struct drm_buddy_block *block)
{
	return block->header & DRM_BUDDY_HEADER_ORDER;
}

static inline unsigned int
drm_buddy_block_state(struct drm_buddy_block *block)
{
	return block->header & DRM_BUDDY_HEADER_STATE;
}

static inline bool
drm_buddy_block_is_allocated(struct drm_buddy_block *block)
{
	return drm_buddy_block_state(block) == DRM_BUDDY_ALLOCATED;
}

static inline bool
drm_buddy_block_is_free(struct drm_buddy_block *block)
{
	return drm_buddy_block_state(block) == DRM_BUDDY_FREE;
}

static inline bool
drm_buddy_block_is_split(struct drm_buddy_block *block)
{
	return drm_buddy_block_state(block) == DRM_BUDDY_SPLIT;
}

static inline u64
drm_buddy_block_size(struct drm_buddy_mm *mm,
		      struct drm_buddy_block *block)
{
	return mm->chunk_size << drm_buddy_block_order(block);
}

int drm_buddy_init(struct drm_buddy_mm *mm, u64 size, u64 chunk_size);

void drm_buddy_fini(struct drm_buddy_mm *mm);

struct drm_buddy_block *
drm_buddy_alloc(struct drm_buddy_mm *mm, unsigned int order,
		bool bar_limit_enabled, unsigned int limit,
		enum drm_buddy_alloc_mode mode);

int drm_buddy_alloc_range(struct drm_buddy_mm *mm,
			   struct list_head *blocks,
			   u64 start, u64 size);

void drm_buddy_free(struct drm_buddy_mm *mm, struct drm_buddy_block *block);

void drm_buddy_free_list(struct drm_buddy_mm *mm, struct list_head *objects);

#endif
