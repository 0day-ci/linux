/**
 * struct __drm_i915_gem_create_ext - Existing gem_create behaviour, with added
 * extension support using struct i915_user_extension.
 *
 * Note that in the future we want to have our buffer flags here, at least for
 * the stuff that is immutable. Previously we would have two ioctls, one to
 * create the object with gem_create, and another to apply various parameters,
 * however this creates some ambiguity for the params which are considered
 * immutable. Also in general we're phasing out the various SET/GET ioctls.
 */
struct __drm_i915_gem_create_ext {
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
	/**
	 * @flags: Optional flags.
	 *
	 * Supported values:
	 *
	 * I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS - Signal to the kernel that
	 * the object will need to be accessed via the CPU.
	 *
	 * Only valid when placing objects in I915_MEMORY_CLASS_DEVICE, and
	 * only strictly required on platforms where only some of the device
	 * memory is directly visible or mappable through the CPU, like on DG2+.
	 *
	 * One of the placements MUST also be I915_MEMORY_CLASS_SYSTEM, to
	 * ensure we can always spill the allocation to system memory, if we
	 * can't place the object in the mappable part of
	 * I915_MEMORY_CLASS_DEVICE.
	 *
	 * Note that buffers that need to be captured with EXEC_OBJECT_CAPTURE,
	 * will need to enable this hint, if the object can also be placed in
	 * I915_MEMORY_CLASS_DEVICE, starting from DG2+. The execbuf call will
	 * throw an error otherwise. This also means that such objects will need
	 * I915_MEMORY_CLASS_SYSTEM set as a possible placement.
	 *
	 * Without this hint, the kernel will assume that non-mappable
	 * I915_MEMORY_CLASS_DEVICE is preferred for this object. Note that the
	 * kernel can still migrate the object to the mappable part, as a last
	 * resort, if userspace ever CPU faults this object, but this might be
	 * expensive, and so ideally should be avoided.
	 */
#define I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS (1 << 0)
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
	 * struct drm_i915_gem_create_ext_memory_regions.
	 *
	 * For I915_GEM_CREATE_EXT_PROTECTED_CONTENT usage see
	 * struct drm_i915_gem_create_ext_protected_content.
	 */
#define I915_GEM_CREATE_EXT_MEMORY_REGIONS 0
#define I915_GEM_CREATE_EXT_PROTECTED_CONTENT 1
	__u64 extensions;
};

#define DRM_I915_QUERY_VMA_INFO	5

/**
 * struct __drm_i915_query_vma_info
 *
 * Given a vm and GTT address, lookup the corresponding vma, returning its set
 * of attributes.
 *
 * .. code-block:: C
 *
 *	struct drm_i915_query_vma_info info = {};
 *	struct drm_i915_query_item item = {
 *		.data_ptr = (uintptr_t)&info,
 *		.query_id = DRM_I915_QUERY_VMA_INFO,
 *	};
 *	struct drm_i915_query query = {
 *		.num_items = 1,
 *		.items_ptr = (uintptr_t)&item,
 *	};
 *	int err;
 *
 *	// Unlike some other types of queries, there is no need to first query
 *	// the size of the data_ptr blob here, since we already know ahead of
 *	// time how big this needs to be.
 *	item.length = sizeof(info);
 *
 *	// Next we fill in the vm_id and ppGTT address of the vma we wish
 *	// to query, before then firing off the query.
 *	info.vm_id = vm_id;
 *	info.offset = gtt_address;
 *	err = ioctl(fd, DRM_IOCTL_I915_QUERY, &query);
 *	if (err || item.length < 0) ...
 *
 *	// If all went well we can now inspect the returned attributes.
 *	if (info.attributes & DRM_I915_QUERY_VMA_INFO_CPU_VISIBLE) ...
 */
struct __drm_i915_query_vma_info {
	/**
	 * @vm_id: The given vm id that contains the vma. The id is the value
	 * returned by the DRM_I915_GEM_VM_CREATE. See struct
	 * drm_i915_gem_vm_control.vm_id.
	 */
	__u32 vm_id;
	/** @pad: MBZ. */
	__u32 pad;
	/**
	 * @offset: The corresponding ppGTT address of the vma which the kernel
	 * will use to perform the lookup.
	 */
	__u64 offset;
	/**
	 * @attributes: The returned attributes for the given vma.
	 *
	 * Possible values:
	 *
	 * DRM_I915_QUERY_VMA_INFO_CPU_VISIBLE - Set if the pages backing the
	 * vma are currently CPU accessible. If this is not set then the vma is
	 * currently backed by I915_MEMORY_CLASS_DEVICE memory, which the CPU
	 * cannot directly access(this is only possible on discrete devices with
	 * a small BAR). Attempting to MMAP and fault such an object will
	 * require the kernel first synchronising any GPU work tied to the
	 * object, before then migrating the pages, either to the CPU accessible
	 * part of I915_MEMORY_CLASS_DEVICE, or I915_MEMORY_CLASS_SYSTEM, if the
	 * placements permit it. See I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS.
	 *
	 * Note that this is inherently racy.
	 */
#define DRM_I915_QUERY_VMA_INFO_CPU_VISIBLE (1<<0)
	__u64 attributes;
	/** @rsvd: MBZ */
	__u32 rsvd[4];
};
