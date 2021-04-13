/* The new query_id for struct drm_i915_query_item */
#define DRM_I915_QUERY_MEMORY_REGIONS   0xdeadbeaf

/**
 * enum drm_i915_gem_memory_class
 */
enum drm_i915_gem_memory_class {
	/** @I915_MEMORY_CLASS_SYSTEM: system memory */
	I915_MEMORY_CLASS_SYSTEM = 0,
	/** @I915_MEMORY_CLASS_DEVICE: device local-memory */
	I915_MEMORY_CLASS_DEVICE,
};

/**
 * struct drm_i915_gem_memory_class_instance
 */
struct drm_i915_gem_memory_class_instance {
	/** @memory_class: see enum drm_i915_gem_memory_class */
	__u16 memory_class;

	/** @memory_instance: which instance */
	__u16 memory_instance;
};

/**
 * struct drm_i915_memory_region_info
 *
 * Describes one region as known to the driver.
 */
struct drm_i915_memory_region_info {
	/** @region: class:instance pair encoding */
	struct drm_i915_gem_memory_class_instance region;

	/** @rsvd0: MBZ */
	__u32 rsvd0;

	/** @caps: MBZ */
	__u64 caps;

	/** @flags: MBZ */
	__u64 flags;

	/** @probed_size: Memory probed by the driver (-1 = unknown) */
	__u64 probed_size;

	/** @unallocated_size: Estimate of memory remaining (-1 = unknown) */
	__u64 unallocated_size;

	/** @rsvd1: MBZ */
	__u64 rsvd1[8];
};

/**
 * struct drm_i915_query_memory_regions
 *
 * Region info query enumerates all regions known to the driver by filling in
 * an array of struct drm_i915_memory_region_info structures.
 */
struct drm_i915_query_memory_regions {
	/** @num_regions: Number of supported regions */
	__u32 num_regions;

	/** @rsvd: MBZ */
	__u32 rsvd[3];

	/** @regions: Info about each supported region */
	struct drm_i915_memory_region_info regions[];
};

#define DRM_I915_GEM_CREATE_EXT		0xdeadbeaf
#define DRM_IOCTL_I915_GEM_CREATE_EXT	DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_CREATE_EXT, struct drm_i915_gem_create_ext)

/**
 * struct drm_i915_gem_create_ext
 */
struct drm_i915_gem_create_ext {
	/**
	 * @size: Requested size for the object.
	 *
	 * The (page-aligned) allocated size for the object will be returned.
	 */
	__u64 size;
	/**
	 * @handle: Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;
	/** @flags: MBZ */
	__u32 flags;
	/**
	 * @extensions:
	 * For I915_GEM_CREATE_EXT_SETPARAM extension usage see both:
	 *	struct drm_i915_gem_create_ext_setparam.
	 *	struct drm_i915_gem_object_param for the possible parameters.
	 */
#define I915_GEM_CREATE_EXT_SETPARAM 0
	__u64 extensions;
};

/**
 * struct drm_i915_gem_object_param
 */
struct drm_i915_gem_object_param {
	/** @handle: Object handle (0 for I915_GEM_CREATE_EXT_SETPARAM) */
	__u32 handle;

	/** @size: Data pointer size */
	__u32 size;

/*
 * I915_OBJECT_PARAM:
 *
 * Select object namespace for the param.
 */
#define I915_OBJECT_PARAM  (1ull<<32)

/**
 * @param: select the desired param.
 *
 * I915_OBJECT_PARAM_MEMORY_REGIONS:
 *
 * Set the data pointer with the desired set of placements in priority
 * order(each entry must be unique and supported by the device), as an array of
 * drm_i915_gem_memory_class_instance, or an equivalent layout of class:instance
 * pair encodings. See DRM_I915_QUERY_MEMORY_REGIONS for how to query the
 * supported regions.
 *
 * In this case the data pointer size should be the number of
 * drm_i915_gem_memory_class_instance elements in the placements array.
 */
#define I915_PARAM_MEMORY_REGIONS 0
#define I915_OBJECT_PARAM_MEMORY_REGIONS (I915_OBJECT_PARAM | \
					  I915_PARAM_MEMORY_REGIONS)
	__u64 param;

	/** @data: Data value or pointer */
	__u64 data;
};

/**
 * struct drm_i915_gem_create_ext_setparam
 */
struct drm_i915_gem_create_ext_setparam {
	/** @base: extension link */
	struct i915_user_extension base;
	/** @param: param to apply for this extension */
	struct drm_i915_gem_object_param param;
};


