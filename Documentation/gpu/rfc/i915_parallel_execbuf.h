#define I915_CONTEXT_ENGINES_EXT_PARALLEL_SUBMIT 2 /* see i915_context_engines_parallel_submit */

/*
 * i915_context_engines_parallel_submit:
 *
 * Setup a slot in the context engine map to allow multiple BBs to be submitted
 * in a single execbuf IOCTL. Those BBs will then be scheduled to run on the GPU
 * in parallel. Multiple hardware contexts are created internally in the i915
 * run these BBs. Once a slot is configured for N BBs only N BBs can be
 * submitted in each execbuf IOCTL and this is implicit behavior e.g. The user
 * doesn't tell the execbuf IOCTL there are N BBs, the execbuf IOCTL know how
 * many BBs there are based on the slots configuration. The N BBs are the last N
 * buffer objects for first N if I915_EXEC_BATCH_FIRST is set.
 *
 * There are two currently defined ways to control the placement of the
 * hardware contexts on physical engines: default behavior (no flags) and
 * I915_PARALLEL_IMPLICIT_BONDS (a flag). More flags may be added the in the
 * future as new hardware / use cases arise. Details of how to use this
 * interface above the flags field in this structure.
 *
 * Returns -EINVAL if hardware context placement configuration is invalid or if
 * the placement configuration isn't supported on the platform / submission
 * interface.
 * Returns -ENODEV if extension isn't supported on the platform / submission
 * inteface.
 */
struct i915_context_engines_parallel_submit {
	struct i915_user_extension base;

	__u16 engine_index;	/* slot for parallel engine */
	__u16 width;		/* number of contexts per parallel engine */
	__u16 num_siblings;	/* number of siblings per context */
	__u16 mbz16;
/*
 * Default placement behavior (currently unsupported):
 *
 * Allow BBs to be placed on any available engine instance. In this case each
 * context's engine mask indicates where that context can be placed. It is
 * implied in this mode that all contexts have mutual exclusive placement.
 * e.g. If one context is running CSX[0] no other contexts can run on CSX[0]).
 *
 * Example 1 pseudo code:
 * CSX,Y[N] = generic engine class X or Y, logical instance N
 * INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 * set_engines(INVALID)
 * set_parallel(engine_index=0, width=2, num_siblings=2,
 *		engines=CSX[0],CSX[1],CSY[0],CSY[1])
 *
 * Results in the following valid placements:
 * CSX[0], CSY[0]
 * CSX[0], CSY[1]
 * CSX[1], CSY[0]
 * CSX[1], CSY[1]
 *
 * This can also be thought of as 2 virtual engines described by 2-D array in
 * the engines the field:
 * VE[0] = CSX[0], CSX[1]
 * VE[1] = CSY[0], CSY[1]
 *
 * Example 2 pseudo code:
 * CSX[Y] = generic engine of same class X, logical instance N
 * INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 * set_engines(INVALID)
 * set_parallel(engine_index=0, width=2, num_siblings=3,
 *		engines=CSX[0],CSX[1],CSX[2],CSX[0],CSX[1],CSX[2])
 *
 * Results in the following valid placements:
 * CSX[0], CSX[1]
 * CSX[0], CSX[2]
 * CSX[1], CSX[0]
 * CSX[1], CSX[2]
 * CSX[2], CSX[0]
 * CSX[2], CSX[1]
 *
 * This can also be thought of as 2 virtual engines described by 2-D array in
 * the engines the field:
 * VE[0] = CSX[0], CSX[1], CSX[2]
 * VE[1] = CSX[0], CSX[1], CSX[2]

 * This enables a use case where all engines are created equally, we don't care
 * where they are scheduled, we just want a certain number of resources, for
 * those resources to be scheduled in parallel, and possibly across multiple
 * engine classes.
 */

/*
 * I915_PARALLEL_IMPLICIT_BONDS - Create implicit bonds between each context.
 * Each context must have the same number of sibling and bonds are implicitly
 * created between each set of siblings.
 *
 * Example 1 pseudo code:
 * CSX[N] = generic engine of same class X, logical instance N
 * INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 * set_engines(INVALID)
 * set_parallel(engine_index=0, width=2, num_siblings=1,
 *		engines=CSX[0],CSX[1], flags=I915_PARALLEL_IMPLICIT_BONDS)
 *
 * Results in the following valid placements:
 * CSX[0], CSX[1]
 *
 * Example 2 pseudo code:
 * CSX[N] = generic engine of same class X, logical instance N
 * INVALID = I915_ENGINE_CLASS_INVALID, I915_ENGINE_CLASS_INVALID_NONE
 * set_engines(INVALID)
 * set_parallel(engine_index=0, width=2, num_siblings=2,
 *		engines=CSX[0],CSX[2],CSX[1],CSX[3],
 *		flags=I915_PARALLEL_IMPLICIT_BONDS)
 *
 * Results in the following valid placements:
 * CSX[0], CSX[1]
 * CSX[2], CSX[3]
 *
 * This can also be thought of as 2 virtual engines described by 2-D array in
 * the engines the field with bonds placed between each index of the virtual
 * engines. e.g. CSX[0] is bonded to CSX[1], CSX[2] is bonded to CSX[3].
 * VE[0] = CSX[0], CSX[2]
 * VE[1] = CSX[1], CSX[3]
 *
 * This enables a use case where all engines are not equal and certain placement
 * rules are required (i.e. split-frame requires all contexts to be placed in a
 * logically contiguous order on the VCS engines on gen11+ platforms). This use
 * case (logically contiguous placement, within a single engine class) is
 * supported when using GuC submission. Execlist mode could support all possible
 * bonding configurations but currently doesn't support this extension.
 */
#define I915_PARALLEL_IMPLICIT_BONDS			(1 << 0)
/*
 * Do not allow BBs to be preempted mid BB rather insert coordinated preemption
 * points on all hardware contexts between each set of BBs. An example use case
 * of this feature is split-frame on gen11+ hardware.
 */
#define I915_PARALLEL_NO_PREEMPT_MID_BATCH		(1 << 1)
#define __I915_PARALLEL_UNKNOWN_FLAGS	(-(I915_PARALLEL_NO_PREEMPT_MID_BATCH << 1))
	__u64 flags;		/* all undefined flags must be zero */
	__u64 mbz64[3];		/* reserved for future use; must be zero */

	/*
	 * 2-D array of engines
	 *
	 * width (i) * num_siblings (j) in length
	 * index = j + i * num_siblings
	 */
	struct i915_engine_class_instance engines[0];
} __attribute__ ((packed));

