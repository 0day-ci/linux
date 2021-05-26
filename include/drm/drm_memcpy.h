/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __DRM_MEMCPY_H__
#define __DRM_MEMCPY_H__

#include <linux/types.h>

struct dma_buf_map;

#ifdef CONFIG_X86
bool drm_memcpy_from_wc(void *dst, const void *src, unsigned long len);
bool drm_memcpy_from_wc_dbm(struct dma_buf_map *dst,
			    const struct dma_buf_map *src,
			    unsigned long len);
void drm_unaligned_memcpy_from_wc(void *dst, const void *src, unsigned long len);

/* The movntdqa instructions used for memcpy-from-wc require 16-byte alignment,
 * as well as SSE4.1 support. drm_memcpy_from_wc() will report if it cannot
 * perform the operation. To check beforehand, pass in the parameters to
 * drm_can_memcpy_from_wc() - since we only care about the low 4 bits,
 * you only need to pass in the minor offsets, page-aligned pointers are
 * always valid.
 *
 * For just checking for SSE4.1, in the foreknowledge that the future use
 * will be correctly aligned, just use drm_has_memcpy_from_wc().
 */
#define drm_can_memcpy_from_wc(dst, src, len) \
	drm_memcpy_from_wc((void *)((unsigned long)(dst) | (unsigned long)(src) | (len)), NULL, 0)

#define drm_has_memcpy_from_wc() \
	drm_memcpy_from_wc(NULL, NULL, 0)

void drm_memcpy_init_early(void);

#else

static inline
bool drm_memcpy_from_wc(void *dst, const void *src, unsigned long len)
{
	return false;
}

static inline
bool drm_memcpy_from_wc_dbm(void *dst, const void *src, unsigned long len)
{
	return false;
}

static inline
bool drm_can_memcpy_from_wc_dbm(void *dst, const void *src, unsigned long len)
{
	return false;
}

static inline
bool drm_has_memcpy_from_wc(void)
{
	return false;
}

#define drm_has_memcpy_from_wc() (false)
#define drm_unaligned_memcpy_from_wc(_dst, _src, _len) WARN_ON(1)
#define drm_memcpy_init_early() do {} while (0)
#endif /* CONFIG_X86 */
#endif /* __DRM_MEMCPY_H__ */
