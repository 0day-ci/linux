struct drm_i915_gem_create_ext {
	/*
	 * Requested size for the object.
	 *
	 * The (page-aligned) allocated size for the object will be returned.
	 */
	__u64 size;
	/*
	 * Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;
	/* MBZ */
	__u32 flags;
	/*
	 * For I915_GEM_CREATE_EXT_SETPARAM extension usage see both:
	 *	struct drm_i915_gem_create_ext_setparam.
	 *	struct drm_i915_gem_object_param for the possible parameters.
	 */
#define I915_GEM_CREATE_EXT_SETPARAM 0
	__u64 extensions;
};

struct drm_i915_gem_object_param {
	/* Object handle (0 for I915_GEM_CREATE_EXT_SETPARAM) */
	__u32 handle;

	/* Data pointer size */
	__u32 size;

/*
 * I915_OBJECT_PARAM:
 *
 * Select object namespace for the param.
 */
#define I915_OBJECT_PARAM  (1ull<<32)

	__u64 param;

	/* Data value or pointer */
	__u64 data;
};

struct drm_i915_gem_create_ext_setparam {
	struct i915_user_extension base;
	struct drm_i915_gem_object_param param;
};
