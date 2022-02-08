// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_memcpy.h"
#include "intel_guc_hwconfig.h"

static inline struct intel_guc *hwconfig_to_guc(struct intel_guc_hwconfig *hwconfig)
{
	return container_of(hwconfig, struct intel_guc, hwconfig);
}

/*
 * GuC has a blob containing hardware configuration information (HWConfig).
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
 * The attribute ids are defined in a hardware spec.
 */

static int __guc_action_get_hwconfig(struct intel_guc_hwconfig *hwconfig,
				     u32 ggtt_offset, u32 ggtt_size)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	u32 action[] = {
		INTEL_GUC_ACTION_GET_HWCONFIG,
		ggtt_offset,
		0, /* upper 32 bits of address */
		ggtt_size,
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

static int verify_hwconfig_blob(struct drm_device *drm,
				const struct intel_guc_hwconfig *hwconfig)
{
	struct drm_i915_query_hwconfig_blob_item *pos;
	u32 remaining;

	if (hwconfig->size % 4 != 0 || hwconfig->ptr == NULL)
		return -EINVAL;

	pos = hwconfig->ptr;
	/* The number of dwords in the blob to validate. Each loop
	 * pass will process at least 2 dwords corresponding to the
	 * key and length fields of the item. In addition, the length
	 * field of the item indicates the length of the data array,
	 * and that number of dwords will be processed (skipped) as
	 * well.
	 */
	remaining = hwconfig->size / 4;

	while (remaining > 0) {
		/* Each item requires at least 2 dwords for the key
		 * and length fields. If the length field is 0, then
		 * the data array would be of length 0.
		 */
		if (remaining < 2)
			return -EINVAL;
		/* remaining >= 2, so subtracting 2 is ok, whereas
		 * adding 2 to pos->length could overflow.
		 */
		if (pos->length > remaining - 2)
			return -EINVAL;
		/* The length check above ensures that the adjustment
		 * of the remaining variable will not underflow, and
		 * that the adjustment of the pos variable will not
		 * pass the end of the blob data.
		 */
		remaining -= 2 + pos->length;
		pos = (void *)&pos->data[pos->length];
	}

	drm_dbg(drm, "hwconfig blob format is valid\n");
	return 0;
}

static int guc_hwconfig_fill_buffer(struct drm_device *drm,
				    struct intel_guc_hwconfig *hwconfig)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	struct i915_vma *vma;
	u32 ggtt_offset;
	void *vaddr;
	int ret;

	GEM_BUG_ON(!hwconfig->size);

	ret = intel_guc_allocate_and_map_vma(guc, hwconfig->size, &vma, &vaddr);
	if (ret)
		return ret;

	ggtt_offset = intel_guc_ggtt_offset(guc, vma);

	ret = __guc_action_get_hwconfig(hwconfig, ggtt_offset, hwconfig->size);
	if (ret >= 0) {
		memcpy(hwconfig->ptr, vaddr, hwconfig->size);
		if (verify_hwconfig_blob(drm, hwconfig)) {
			drm_err(drm, "Ignoring invalid hwconfig blob received from GuC!\n");
			ret = -EINVAL;
		}
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);

	return ret;
}

static bool has_table(struct drm_i915_private *i915)
{
	if (IS_ALDERLAKE_P(i915))
		return true;

	return false;
}

/**
 * intel_guc_hwconfig_fini - Finalize the HWConfig
 *
 * Free up the memory allocation holding the table.
 */
void intel_guc_hwconfig_fini(struct intel_guc_hwconfig *hwconfig)
{
	kfree(hwconfig->ptr);
	hwconfig->size = 0;
	hwconfig->ptr = NULL;
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

	ret = guc_hwconfig_fill_buffer(&i915->drm, hwconfig);
	if (ret < 0) {
		intel_guc_hwconfig_fini(hwconfig);
		return ret;
	}

	return 0;
}
