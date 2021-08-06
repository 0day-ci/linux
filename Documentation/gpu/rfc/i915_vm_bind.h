/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

/* VM_BIND feature availability through drm_i915_getparam */
#define I915_PARAM_HAS_VM_BIND          59

/**
 * struct drm_i915_gem_vm_bind - VA to object/buffer mapping to [un]bind.
 */
struct drm_i915_gem_vm_bind {
	/** vm to [un]bind **/
	__u32 vm_id;

	/** BO handle or file descriptor. 'fd' to -1 for system pages (SVM) **/
	union {
		__u32 handle;
		__s32 fd;
	}

	/** VA start to [un]bind **/
	__u64 start;

	/** Offset in object to [un]bind **/
	__u64 offset;

	/** VA length to [un]bind **/
	__u64 length;

	/** Flags **/
	__u64 flags;
	/** Bind the mapping immediately instead of during next submission */
#define I915_GEM_VM_BIND_IMMEDIATE   (1 << 0)
	/** Read-only mapping */
#define I915_GEM_VM_BIND_READONLY    (1 << 1)
	/** Capture this mapping in the dump upon GPU error */
#define I915_GEM_VM_BIND_CAPTURE     (1 << 2)

	/**
	 * Zero-terminated chain of extensions.
	 *
	 * No current extensions defined; mbz.
	 */
	__u64 extensions;
};

/**
 * struct drm_i915_vm_bind_ext_sync_fence - Bind completion signaling extension.
 */
struct drm_i915_vm_bind_ext_sync_fence {
#define I915_VM_BIND_EXT_SYNC_FENCE     0
	/** @base: Extension link. See struct i915_user_extension. */
	struct i915_user_extension base;

	/** User/Memory fence address */
	__u64 addr;

	/** User/Memory fence value to be written after bind completion */
	__u64 val;
};

/**
 * struct drm_i915_gem_wait_user_fence
 *
 * Wait on user/memory fence. User/Memory fence can be woken up either by,
 *    1. GPU context indicated by 'ctx_id', or,
 *    2. Kerrnel driver async worker upon I915_UFENCE_WAIT_SOFT.
 *       'ctx_id' is ignored when this flag is set.
 *
 * Wakeup when below condition is true.
 * (*addr & MASK) OP (VALUE & MASK)
 *
 */
struct drm_i915_gem_wait_user_fence {
	/** @base: Extension link. See struct i915_user_extension. */
	__u64 extensions;

	/** User/Memory fence address */
	__u64 addr;

	/** Id of the Context which will signal the fence. */
	__u32 ctx_id;

	/** Wakeup condition operator */
	__u16 op;
#define I915_UFENCE_WAIT_EQ      0
#define I915_UFENCE_WAIT_NEQ     1
#define I915_UFENCE_WAIT_GT      2
#define I915_UFENCE_WAIT_GTE     3
#define I915_UFENCE_WAIT_LT      4
#define I915_UFENCE_WAIT_LTE     5
#define I915_UFENCE_WAIT_BEFORE  6
#define I915_UFENCE_WAIT_AFTER   7

	/** Flags */
	__u16 flags;
#define I915_UFENCE_WAIT_SOFT    0x1
#define I915_UFENCE_WAIT_ABSTIME 0x2

	/** Wakeup value */
	__u64 value;

	/** Wakeup mask */
	__u64 mask;
#define I915_UFENCE_WAIT_U8     0xffu
#define I915_UFENCE_WAIT_U16    0xffffu
#define I915_UFENCE_WAIT_U32    0xfffffffful
#define I915_UFENCE_WAIT_U64    0xffffffffffffffffull

	/** Timeout */
	__s64 timeout;
};
