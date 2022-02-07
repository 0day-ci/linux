/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_MEMCPY_H__
#define __I915_MEMCPY_H__

#include <linux/types.h>

struct drm_i915_private;

void i915_memcpy_init_early(struct drm_i915_private *i915);

void i915_memcpy_from_wc(void *dst, const void *src, unsigned long len);
void i915_unaligned_memcpy_from_wc(void *dst, const void *src, unsigned long len);
void i915_io_memcpy_from_wc(void *dst, const void __iomem *src, unsigned long len);

bool i915_can_memcpy_from_wc(void *dst, const void *src, unsigned long len);

#define i915_has_memcpy_from_wc() \
	i915_can_memcpy_from_wc(NULL, NULL, 0)

#endif /* __I915_MEMCPY_H__ */
