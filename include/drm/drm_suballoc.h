/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 */
#ifndef _DRM_SUBALLOC_H_
#define _DRM_SUBALLOC_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/wait.h>

struct dma_fence;
struct seq_file;

/* sub-allocation manager, it has to be protected by another lock.
 * By conception this is an helper for other part of the driver
 * like the indirect buffer or semaphore, which both have their
 * locking.
 *
 * Principe is simple, we keep a list of sub allocation in offset
 * order (first entry has offset == 0, last entry has the highest
 * offset).
 *
 * When allocating new object we first check if there is room at
 * the end total_size - (last_object_offset + last_object_size) >=
 * alloc_size. If so we allocate new object there.
 *
 * When there is not enough room at the end, we start waiting for
 * each sub object until we reach object_offset+object_size >=
 * alloc_size, this object then become the sub object we return.
 *
 * Alignment can't be bigger than page size.
 *
 * Hole are not considered for allocation to keep things simple.
 * Assumption is that there won't be hole (all object on same
 * alignment).
 *
 * The actual buffer object handling depends on the driver,
 * and is not part of the helper implementation.
 */
#define DRM_SUBALLOC_MAX_QUEUES 32

struct drm_suballoc_manager {
	wait_queue_head_t wq;
	struct list_head *hole, olist, flist[DRM_SUBALLOC_MAX_QUEUES];
	u32 size, align;
};

/* sub-allocation buffer */
struct drm_suballoc {
	struct list_head olist, flist;
	struct drm_suballoc_manager *manager;
	u32 soffset, eoffset;
	struct dma_fence *fence;
};

void drm_suballoc_manager_init(struct drm_suballoc_manager *sa_manager,
			       u32 size, u32 align);
void drm_suballoc_manager_fini(struct drm_suballoc_manager *sa_manager);
struct drm_suballoc *drm_suballoc_new(struct drm_suballoc_manager *sa_manager,
				      u32 size);
void drm_suballoc_free(struct drm_suballoc *sa_bo,
		       struct dma_fence *fence,
		       u32 queue);

#ifdef CONFIG_DEBUG_FS
void drm_suballoc_dump_debug_info(struct drm_suballoc_manager *sa_manager,
				  struct seq_file *m, u64 suballoc_base);
#else
static inline void
drm_suballoc_dump_debug_info(struct drm_suballoc_manager *sa_manager,
			     struct seq_file *m, u64 suballoc_base)
{ }

#endif

#endif /* _DRM_SUBALLOC_H_ */
