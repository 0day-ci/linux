/* SPDX-License-Identifier: MIT
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __AMDGPU_VRAM_MGR_H__
#define __AMDGPU_VRAM_MGR_H__

#include <drm/drm_buddy.h>

struct amdgpu_vram_mgr_node {
	struct ttm_resource base;
	struct list_head blocks;
	unsigned long flags;
};

struct amdgpu_vram_reservation {
	uint64_t start;
	uint64_t size;
	uint64_t min_size;
	unsigned long flags;
	struct list_head block;
	struct list_head node;
};

static inline uint64_t node_start(struct drm_buddy_block *block)
{
	return drm_buddy_block_offset(block);
}

static inline uint64_t node_size(struct drm_buddy_block *block)
{
	return PAGE_SIZE << drm_buddy_block_order(block);
}

static inline struct amdgpu_vram_mgr_node *
to_amdgpu_vram_mgr_node(struct ttm_resource *res)
{
	return container_of(res, struct amdgpu_vram_mgr_node, base);
}

static inline struct amdgpu_vram_mgr *
to_vram_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct amdgpu_vram_mgr, manager);
}

static inline struct amdgpu_device *
to_amdgpu_device(struct amdgpu_vram_mgr *mgr)
{
	return container_of(mgr, struct amdgpu_device, mman.vram_mgr);
}

#endif
