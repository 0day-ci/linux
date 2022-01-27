/*
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _GVT_MMIO_TABLE_H_
#define _GVT_MMIO_TABLE_H_

#include <linux/kernel.h>

/* Describe per-platform limitations. */
struct intel_gvt_device_info {
	u32 max_support_vgpus;
	u32 cfg_space_size;
	u32 mmio_size;
	u32 mmio_bar;
	unsigned long msi_cap_offset;
	u32 gtt_start_offset;
	u32 gtt_entry_size;
	u32 gtt_entry_size_shift;
	int gmadr_bytes_in_cmd;
	u32 max_surface_size;
};

struct intel_gvt_mmio_table_iter {
	struct drm_i915_private *i915;
	void *data;
	int (*do_mmio)(u32 offset, u16 flags, u32 size, u32 addr_mask,
		       u32 ro_mask, u32 device,
		       struct intel_gvt_mmio_table_iter *iter);
	int (*do_mmio_block)(u32 offset, u32 size, u32 device,
		       struct intel_gvt_mmio_table_iter *iter);
};

void intel_gvt_init_device_info(struct drm_i915_private *i915, struct intel_gvt_device_info *info);
unsigned long intel_gvt_get_device_type(struct drm_i915_private *i915);
int intel_gvt_iterate_mmio_table(struct intel_gvt_mmio_table_iter *iter);

#endif
