// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifdef CONFIG_X86
#include <linux/dma-buf-map.h>
#include <linux/kernel.h>
#include <asm/fpu/api.h>

#include "drm/drm_memcpy.h"

static DEFINE_STATIC_KEY_FALSE(has_movntdqa);

static void __memcpy_ntdqa(void *dst, const void *src, unsigned long len)
{
	kernel_fpu_begin();

	while (len >= 4) {
		asm("movntdqa   (%0), %%xmm0\n"
		    "movntdqa 16(%0), %%xmm1\n"
		    "movntdqa 32(%0), %%xmm2\n"
		    "movntdqa 48(%0), %%xmm3\n"
		    "movaps %%xmm0,   (%1)\n"
		    "movaps %%xmm1, 16(%1)\n"
		    "movaps %%xmm2, 32(%1)\n"
		    "movaps %%xmm3, 48(%1)\n"
		    :: "r" (src), "r" (dst) : "memory");
		src += 64;
		dst += 64;
		len -= 4;
	}
	while (len--) {
		asm("movntdqa (%0), %%xmm0\n"
		    "movaps %%xmm0, (%1)\n"
		    :: "r" (src), "r" (dst) : "memory");
		src += 16;
		dst += 16;
	}

	kernel_fpu_end();
}

static void __memcpy_ntdqu(void *dst, const void *src, unsigned long len)
{
	kernel_fpu_begin();

	while (len >= 4) {
		asm("movntdqa   (%0), %%xmm0\n"
		    "movntdqa 16(%0), %%xmm1\n"
		    "movntdqa 32(%0), %%xmm2\n"
		    "movntdqa 48(%0), %%xmm3\n"
		    "movups %%xmm0,   (%1)\n"
		    "movups %%xmm1, 16(%1)\n"
		    "movups %%xmm2, 32(%1)\n"
		    "movups %%xmm3, 48(%1)\n"
		    :: "r" (src), "r" (dst) : "memory");
		src += 64;
		dst += 64;
		len -= 4;
	}
	while (len--) {
		asm("movntdqa (%0), %%xmm0\n"
		    "movups %%xmm0, (%1)\n"
		    :: "r" (src), "r" (dst) : "memory");
		src += 16;
		dst += 16;
	}

	kernel_fpu_end();
}

/**
 * drm_memcpy_from_wc: perform an accelerated *aligned* read from WC
 * @dst: destination pointer
 * @src: source pointer
 * @len: how many bytes to copy
 *
 * drm_memcpy_from_wc copies @len bytes from @src to @dst using
 * non-temporal instructions where available. Note that all arguments
 * (@src, @dst) must be aligned to 16 bytes and @len must be a multiple
 * of 16.
 *
 * To test whether accelerated reads from WC are supported, use
 * drm_memcpy_from_wc(NULL, NULL, 0);
 * This interface is intended for memremapped memory without the __iomem tag.
 *
 * Returns true if the copy was successful, false if the preconditions
 * are not met.
 */
bool drm_memcpy_from_wc(void *dst, const void *src, unsigned long len)
{
	if (unlikely(((unsigned long)dst | (unsigned long)src | len) & 15))
		return false;

	if (static_branch_likely(&has_movntdqa)) {
		if (likely(len))
			__memcpy_ntdqa(dst, src, len >> 4);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(drm_memcpy_from_wc);

/**
 * drm_memcpy_from_wc_dbm: perform an accelerated *aligned* read from WC with
 * struct dma_buf_map arguments.
 * @dst: destination map
 * @src: source map
 * @len: how many bytes to copy
 *
 * This is identical to drm_memcpy_from_wc, except it's intended for
 * potentially ioremapped memory rather than memremapped memory.
 *
 * Returns true if the copy was successful, false if the preconditions
 * are not met.
 */
bool drm_memcpy_from_wc_dbm(struct dma_buf_map *dst,
			    const struct dma_buf_map *src,
			    unsigned long len)
{
	/* For X86 we can safely drop __iomem */
	return drm_memcpy_from_wc(dst->is_iomem ?
				  (void __force *)dst->vaddr_iomem :
				  dst->vaddr,
				  src->is_iomem ?
				  (void const __force *)src->vaddr_iomem :
				  src->vaddr,
				  len);
}
EXPORT_SYMBOL(drm_memcpy_from_wc_dbm);

/**
 * drm_unaligned_memcpy_from_wc: perform a mostly accelerated read from WC
 * @dst: destination pointer
 * @src: source pointer
 * @len: how many bytes to copy
 *
 * Like drm_memcpy_from_wc(), the unaligned variant copies @len bytes from
 * @src to @dst using * non-temporal instructions where available, but
 * accepts that its arguments may not be aligned, but are valid for the
 * potential 16-byte read past the end.
 *
 * This interface is intended for mremapped memory without the __iomem tag.
 */
void drm_unaligned_memcpy_from_wc(void *dst, const void *src, unsigned long len)
{
	unsigned long addr;

	addr = (unsigned long)src;
	if (!IS_ALIGNED(addr, 16)) {
		unsigned long x = min(ALIGN(addr, 16) - addr, len);

		memcpy(dst, src, x);

		len -= x;
		dst += x;
		src += x;
	}

	if (likely(len))
		__memcpy_ntdqu(dst, src, DIV_ROUND_UP(len, 16));
}
EXPORT_SYMBOL(drm_unaligned_memcpy_from_wc);

void drm_memcpy_init_early(void)
{
	/*
	 * Some hypervisors (e.g. KVM) don't support VEX-prefix instructions
	 * emulation. So don't enable movntdqa in hypervisor guest.
	 */
	if (static_cpu_has(X86_FEATURE_XMM4_1) &&
	    !boot_cpu_has(X86_FEATURE_HYPERVISOR))
		static_branch_enable(&has_movntdqa);
}
#endif
