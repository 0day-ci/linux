// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "gt/intel_hwconfig_types.h"
#include "i915_drv.h"
#include "i915_memcpy.h"
#include "intel_guc_hwconfig.h"

static
inline struct intel_guc *hwconfig_to_guc(struct intel_guc_hwconfig *hwconfig)
{
	return container_of(hwconfig, struct intel_guc, hwconfig);
}

/**
 * GuC has a blob containing hardware configuration information (hwconfig).
 * This is formatted as a simple and flexible KLV (Key/Length/Value) table.
 *
 * For example, a minimal version could be:
 *   enum device_attr {
 *     ATTR_SOME_VALUE = 0,
 *     ATTR_SOME_MASK  = 1,
 *   };
 *
 *   static const u32 hwconfig[] = {
 *     ATTR_SOME_VALUE,
 *     1,		// Value Length in DWords
 *     8,		// Value
 *
 *     ATTR_SOME_MASK,
 *     3,
 *     0x00FFFFFFFF, 0xFFFFFFFF, 0xFF000000,
 *   };
 *
 * The attribute ids are defined in a hardware spec. The current list as
 * known to the i915 driver can be found in i915/gt/intel_guc_hwconfig_types.h
 */

static int __guc_action_get_hwconfig(struct intel_guc_hwconfig *hwconfig,
				     u32 ggtt_offset, u32 ggtt_size)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	u32 action[] = {
		INTEL_GUC_ACTION_GET_HWCONFIG,
		ggtt_offset,
		ggtt_size,
		(INTEL_DEVID(i915) << 16) | INTEL_REVID(i915),
	};
	int ret;

	ret = intel_guc_send_mmio(guc, action, ARRAY_SIZE(action), NULL, 0);
	if (ret == -ENXIO)
		return -ENOENT;

	if (!ggtt_size && !ret)
		ret = -EINVAL;

	return ret;
}

static int guc_hwconfig_discover_size(struct intel_guc_hwconfig *hwconfig)
{
	int ret;

	/* Sending a query with too small a table will return the size of the table */
	ret = __guc_action_get_hwconfig(hwconfig, 0, 0);
	if (ret < 0)
		return ret;

	hwconfig->size = ret;
	return 0;
}

static int guc_hwconfig_fill_buffer(struct intel_guc_hwconfig *hwconfig)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	u32 ggtt_offset;
	int ret;
	struct i915_vma *vma;
	void *vaddr;

	GEM_BUG_ON(!hwconfig->size);

	ret = intel_guc_allocate_and_map_vma(guc, hwconfig->size, &vma, &vaddr);
	if (ret) {
		drm_err(&i915->drm, "HWConfig: Failed to allocate buffer: %d!\n", ret);
		return ret;
	}

	ggtt_offset = intel_guc_ggtt_offset(guc, vma);

	ret = __guc_action_get_hwconfig(hwconfig, ggtt_offset, hwconfig->size);
	if (ret >= 0)
		memcpy(hwconfig->ptr, vaddr, hwconfig->size);

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);

	return ret;
}

static bool has_table(struct drm_i915_private *i915)
{
	if (IS_ALDERLAKE_P(i915)) {
		if (IS_ADLP_GT_STEP(i915, STEP_A0, STEP_A0))
			return 0;

		return 1;
	}

	return 0;
}

/**
 * intel_guc_hwconfig_init - Initialize the HWConfig
 *
 * Retrieve the HWConfig table from the GuC and save it away in a local memory
 * allocation. It can then be queried on demand by other users later on.
 */
int intel_guc_hwconfig_init(struct intel_guc_hwconfig *hwconfig)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	int ret;

	if (!has_table(i915))
		return 0;

	ret = guc_hwconfig_discover_size(hwconfig);
	if (ret)
		return ret;

	hwconfig->ptr = kmalloc(hwconfig->size, GFP_KERNEL);
	if (!hwconfig->ptr) {
		hwconfig->size = 0;
		return -ENOMEM;
	}

	ret = guc_hwconfig_fill_buffer(hwconfig);
	if (ret < 0) {
		kfree(hwconfig->ptr);
		hwconfig->size = 0;
		hwconfig->ptr = NULL;
		return ret;
	}

	return 0;
}

/**
 * intel_guc_hwconfig_fini - Finalize the HWConfig
 *
 * Free up the memory allocation holding the table.
 */
void intel_guc_hwconfig_fini(struct intel_guc_hwconfig *hwconfig)
{
	if (!hwconfig->size)
		return;

	kfree(hwconfig->ptr);
	hwconfig->size = 0;
	hwconfig->ptr = NULL;
}
