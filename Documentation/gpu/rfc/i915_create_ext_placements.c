#define I915_OBJECT_PARAM  (1ull<<32)

/*
 * I915_OBJECT_PARAM_MEMORY_REGIONS
 *
 * Set the data pointer with the desired set of placements in priority
 * order(each entry must be unique and supported by the device), as an array of
 * drm_i915_gem_memory_class_instance, or an equivalent layout of class:instance
 * pair encodings. See DRM_I915_QUERY_MEMORY_REGIONS for how to query the
 * supported regions.
 *
 * In this case the data pointer size should be the number of
 * drm_i915_gem_memory_class_instance elements in the placements array.
 *
 */
#define I915_PARAM_MEMORY_REGIONS 0
#define I915_OBJECT_PARAM_MEMORY_REGIONS (I915_OBJECT_PARAM | \
					  I915_PARAM_MEMORY_REGIONS)
	__u64 param;
