/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX_SLAB_H
#define __TOOLS_LINUX_SLAB_H

#include <linux/gfp.h>
#include <linux/cache.h>

static inline void *kmalloc(size_t size, gfp_t gfp)
{
	void *p;

	p = memalign(SMP_CACHE_BYTES, size);
	if (!p)
		return p;

	if (gfp & __GFP_ZERO)
		memset(p, 0, size);

	return p;
}

static inline void *kzalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	return kmalloc(n * size, flags);
}

static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

static inline void kfree(void *p)
{
	free(p);
}

#define kvmalloc_array		kmalloc_array
#define kvfree			kfree
#define KMALLOC_MAX_SIZE	SIZE_MAX

#endif
