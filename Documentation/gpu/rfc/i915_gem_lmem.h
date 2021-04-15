/*
 * Note that drm_i915_query_item and drm_i915_query are existing bits of uAPI.
 * For the regions query we are just adding a new query id, so no actual new
 * ioctl or anything, but including it here for reference.
 */
struct drm_i915_query_item {
#define DRM_I915_QUERY_MEMORY_REGIONS   0xdeadbeaf
	....
        __u64 query_id;

        /*
         * When set to zero by userspace, this is filled with the size of the
         * data to be written at the data_ptr pointer. The kernel sets this
         * value to a negative value to signal an error on a particular query
         * item.
         */
        __s32 length;

        __u32 flags;
        /*
         * Data will be written at the location pointed by data_ptr when the
         * value of length matches the length of the data to be written by the
         * kernel.
         */
        __u64 data_ptr;
};

struct drm_i915_query {
        __u32 num_items;
        /*
         * Unused for now. Must be cleared to zero.
         */
        __u32 flags;
        /*
         * This points to an array of num_items drm_i915_query_item structures.
         */
        __u64 items_ptr;
};

#define DRM_IOCTL_I915_QUERY	DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_QUERY, struct drm_i915_query)

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
 *
 * Note that we reserve quite a lot of stuff here for potential future work. As
 * an example we might want expose the capabilities(see caps) for a given
 * region, which could include things like if the region is CPU
 * mappable/accessible etc.
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
 *
 * Example for getting the list of supported regions:
 *
 * .. code-block:: C
 *
 *	struct drm_i915_query_memory_regions *info;
 *	struct drm_i915_query_item item = {
 *		.query_id = DRM_I915_QUERY_MEMORY_REGIONS;
 *	};
 *	struct drm_i915_query query = {
 *		.num_items = 1,
 *		.items_ptr = (uintptr_t)&item,
 *	};
 *	int err, i;
 *
 *	// First query the size of the blob we need, this needs to be large
 *	// enough to hold our array of regions. The kernel will fill out the
 *	// item.length for us, which is the number of bytes we need.
 *	err = ioctl(fd, DRM_IOCTL_I915_QUERY, &query);
 *	if (err) ...
 *
 *	info = calloc(1, item.length);
 *	// Now that we allocated the required number of bytes, we call the ioctl
 *	// again, this time with the data_ptr pointing to our newly allocated
 *	// blob, which the kernel can then populate with the all the region info.
 *	item.data_ptr = (uintptr_t)&info,
 *
 *	err = ioctl(fd, DRM_IOCTL_I915_QUERY, &query);
 *	if (err) ...
 *
 *	// We can now access each region in the array
 *	for (i = 0; i < info->num_regions; i++) {
 *		struct drm_i915_memory_region_info mr = info->regions[i];
 *		u16 class = mr.region.class;
 *		u16 instance = mr.region.instance;
 *
 *		....
 *	}
 *
 *	free(info);
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
 *
 * Existing gem_create behaviour, with added extension support.
 *
 * Note that in the future we want to have our buffer flags here, at least for
 * the stuff that is immutable. Previously we would have two ioctls, one to
 * create the object with gem_create, and another to apply various parameters,
 * however this creates some ambiguity for the params which are considered
 * immutable. Also in general we're phasing out the various SET/GET ioctls.
 */
struct drm_i915_gem_create_ext {
	/**
	 * @size: Requested size for the object.
	 *
	 * The (page-aligned) allocated size for the object will be returned.
	 *
	 * Note that for some devices we have might have further minimum
	 * page-size restrictions(larger than 4K), like for device local-memory.
	 * However in general the final size here should always reflect any
	 * rounding up, if for example using the I915_GEM_CREATE_EXT_MEMORY_REGIONS
	 * extension to place the object in device local-memory.
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
	 * @extensions: The chain of extensions to apply to this object.
	 *
	 * This will be useful in the future when we need to support several
	 * different extensions, and we need to apply more than one when
	 * creating the object. See struct i915_user_extension.
	 *
	 * If we don't supply any extensions then we get the same old gem_create
	 * behaviour.
	 *
	 * For I915_GEM_CREATE_EXT_MEMORY_REGIONS usage see
	 * drm_i915_gem_create_ext_memory_regions
	 */
#define I915_GEM_CREATE_EXT_MEMORY_REGIONS 0
	__u64 extensions;
};

/**
 * struct drm_i915_gem_create_ext_memory_regions
 *
 * I915_GEM_CREATE_EXT_MEMORY_REGIONS extension:
 *
 * Set the object with the desired set of placements/regions in priority
 * order(each entry must be unique and supported by the device), as an array of
 * drm_i915_gem_memory_class_instance, or an equivalent layout of class:instance
 * pair encodings. See DRM_I915_QUERY_MEMORY_REGIONS for how to query the
 * supported regions for a device.
 *
 * As an example, on discrete devices, if we wish to set the placement as
 * device local-memory we can do something like:
 *
 * .. code-block:: C
 *
 *	struct drm_i915_gem_memory_class_instance region_lmem = {
 *              .memory_class = I915_MEMORY_CLASS_DEVICE,
 *              .memory_instance = 0,
 *      };
 *      struct drm_i915_gem_create_ext_memory_regions regions = {
 *              .base = { .name = I915_GEM_CREATE_EXT_MEMORY_REGIONS },
 *              .regions = (uintptr_t)&region_lmem,
 *              .num_regions = 1,
 *      };
 *      struct drm_i915_gem_create_ext create_ext = {
 *              .size = 16 * PAGE_SIZE,
 *              .extensions = (uintptr_t)&regions,
 *      };
 *
 *      int err = ioctl(fd, DRM_IOCTL_I915_GEM_CREATE_EXT, &create_ext);
 *      if (err) ...
 *
 * At which point we get the object handle in create_ext.handle, if all went
 * well.
 */
struct drm_i915_gem_create_ext_memory_regions {
	/** @base: Extension link. See struct i915_user_extension. */
	struct i915_user_extension base;

	/** @pad: MBZ */
	__u32 pad;
	/** @num_regions: Number of elements in the placements array. */
	__u32 num_regions;
	/**
	 * @regions: The placements array.
	 *
	 * Should be an array of drm_i915_gem_memory_class_instance.
	 */
	__u64 regions;
};
