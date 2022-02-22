// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_memcpy.h"
#include "intel_guc_hwconfig.h"

static struct intel_guc *hwconfig_to_guc(struct intel_guc_hwconfig *hwconfig)
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

static int __guc_action_get_hwconfig(struct intel_guc *guc,
				     u32 ggtt_offset, u32 ggtt_size)
{
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

	return ret;
}

static int guc_hwconfig_discover_size(struct intel_guc_hwconfig *hwconfig)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	int ret;

	/* Sending a query with zero offset and size will return the
	 * size of the blob.
	 */
	ret = __guc_action_get_hwconfig(guc, 0, 0);
	if (ret < 0)
		return ret;

	if (ret == 0)
		return -EINVAL;

	hwconfig->size = ret;
	return 0;
}

static int verify_hwconfig_blob(struct intel_guc_hwconfig *hwconfig)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	struct drm_device *drm = &guc_to_gt(guc)->i915->drm;
	struct drm_i915_query_hwconfig_blob_item *item = hwconfig->ptr;
	u64 offset = 0;
	u64 remaining = hwconfig->size;
	/* Everything before the data field is required */
	u64 min_item_size = offsetof(struct drm_i915_query_hwconfig_blob_item, data);
	u64 item_size;

	if (!IS_ALIGNED(hwconfig->size, sizeof(u32))) {
		drm_err(drm, "hwconfig blob size (%d) is not u32 aligned\n", hwconfig->size);
		return -EINVAL;
	}

	while (offset < hwconfig->size) {
		if (remaining < min_item_size) {
			drm_err(drm, "hwconfig blob invalid (no room for item required fields at offset %lld)\n",
				offset);
			return -EINVAL;
		}
		item_size = min_item_size + sizeof(u32) * item->length;
		if (item_size > remaining) {
			drm_err(drm, "hwconfig blob invalid (no room for data array of item at offset %lld)\n",
				offset);
			return -EINVAL;
		}
		offset += item_size;
		remaining -= item_size;
		item = (void *)&item->data[item->length];
	}

	return 0;
}

static int guc_hwconfig_fill_buffer(struct intel_guc_hwconfig *hwconfig)
{
	struct intel_guc *guc = hwconfig_to_guc(hwconfig);
	struct drm_device *drm = &guc_to_gt(guc)->i915->drm;
	struct i915_vma *vma;
	u32 ggtt_offset;
	void *vaddr;
	int ret;

	GEM_BUG_ON(!hwconfig->size);

	ret = intel_guc_allocate_and_map_vma(guc, hwconfig->size, &vma, &vaddr);
	if (ret)
		return ret;

	ggtt_offset = intel_guc_ggtt_offset(guc, vma);

	ret = __guc_action_get_hwconfig(guc, ggtt_offset, hwconfig->size);
	if (ret >= 0) {
		memcpy(hwconfig->ptr, vaddr, hwconfig->size);
		if (verify_hwconfig_blob(hwconfig)) {
			drm_err(drm, "Ignoring invalid hwconfig blob received from GuC!\n");
			ret = -EINVAL;
		}
	}

	i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);

	return ret;
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

	if (!INTEL_INFO(i915)->has_guc_hwconfig)
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
		intel_guc_hwconfig_fini(hwconfig);
		return ret;
	}

	return 0;
}
