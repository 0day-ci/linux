/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef _TTM_RANGE_MANAGER_H_
#define _TTM_RANGE_MANAGER_H_

#include <drm/ttm/ttm_resource.h>
#include <drm/drm_mm.h>

/**
 * struct ttm_range_mgr_node
 *
 * @base: base clase we extend
 * @mm_nodes: MM nodes, usually 1
 *
 * Extending the ttm_resource object to manage an address space allocation with
 * one or more drm_mm_nodes.
 */
struct ttm_range_mgr_node {
	struct ttm_resource base;
	struct drm_mm_node mm_nodes[];
};

/**
 * to_ttm_range_mgr_node
 *
 * @res: the resource to upcast
 *
 * Upcast the ttm_resource object into a ttm_range_mgr_node object.
 */
static inline struct ttm_range_mgr_node *
to_ttm_range_mgr_node(struct ttm_resource *res)
{
	return container_of(res, struct ttm_range_mgr_node, base);
}

int ttm_range_man_init(struct ttm_device *bdev,
		       unsigned type, bool use_tt,
		       unsigned long p_size);
int ttm_range_man_fini(struct ttm_device *bdev,
		       unsigned type);
#define ttm_range_man_init(bdev, type, use_tt, size) ({	\
	BUILD_BUG_ON(__builtin_constant_p(type)		\
			&& type >= TTM_NUM_MEM_TYPES);	\
	ttm_range_man_init(bdev, type, use_tt, size);	\
})
#define ttm_range_man_fini(bdev, type) ({		\
	BUILD_BUG_ON(__builtin_constant_p(type)		\
			&& type >= TTM_NUM_MEM_TYPES);	\
	ttm_range_man_fini(bdev, type);			\
})

#endif
