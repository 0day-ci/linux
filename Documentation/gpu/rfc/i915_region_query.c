enum drm_i915_gem_memory_class {
	I915_MEMORY_CLASS_SYSTEM = 0,
	I915_MEMORY_CLASS_DEVICE,
};

struct drm_i915_gem_memory_class_instance {
	__u16 memory_class; /* see enum drm_i915_gem_memory_class */
	__u16 memory_instance;
};

/**
 * struct drm_i915_memory_region_info
 *
 * Describes one region as known to the driver.
 */
struct drm_i915_memory_region_info {
	/** class:instance pair encoding */
	struct drm_i915_gem_memory_class_instance region;

	/** MBZ */
	__u32 rsvd0;

	/** MBZ */
	__u64 caps;

	/** MBZ */
	__u64 flags;

	/** Memory probed by the driver (-1 = unknown) */
	__u64 probed_size;

	/** Estimate of memory remaining (-1 = unknown) */
	__u64 unallocated_size;

	/** MBZ */
	__u64 rsvd1[8];
};

/**
 * struct drm_i915_query_memory_regions
 *
 * Region info query enumerates all regions known to the driver by filling in
 * an array of struct drm_i915_memory_region_info structures.
 */
struct drm_i915_query_memory_regions {
	/** Number of supported regions */
	__u32 num_regions;

	/** MBZ */
	__u32 rsvd[3];

	/* Info about each supported region */
	struct drm_i915_memory_region_info regions[];
};
