// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_active.h"
#include "intel_context.h"
#include "intel_context_param.h"
#include "intel_ring.h"

int intel_context_set_ring_size(struct intel_context *ce, long sz)
{
	struct intel_ring *ring;
	int err;

	if (ce->engine->gt->submission_method == INTEL_SUBMISSION_RING)
		return 0;

	/* Try fast case (unallocated) first */
	if (!test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		bool done = false;

		err = mutex_lock_interruptible(&ce->alloc_mutex);
		if (err)
			return err;

		if (!test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
			ce->ring = __intel_context_ring_size(sz);
			done = true;
		}
		mutex_unlock(&ce->alloc_mutex);

		if (done)
			return 0;
	}

	/* Context already allocated */
	err = intel_context_lock_pinned(ce);
	if (err)
		return err;

	err = i915_active_wait(&ce->active);
	if (err < 0)
		goto unlock;

	if (intel_context_is_pinned(ce)) {
		err = -EBUSY; /* In active use, come back later! */
		goto unlock;
	}

	/* Replace the existing ringbuffer */
	ring = intel_engine_create_ring(ce->engine, sz);
	if (IS_ERR(ring)) {
		err = PTR_ERR(ring);
		goto unlock;
	}

	intel_ring_put(ce->ring);
	ce->ring = ring;

	/* Context image will be updated on next pin */

unlock:
	intel_context_unlock_pinned(ce);
	return err;
}

long intel_context_get_ring_size(struct intel_context *ce)
{
	long sz = (unsigned long)READ_ONCE(ce->ring);

	if (test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		if (intel_context_lock_pinned(ce))
			return -EINTR;

		sz = ce->ring->size;
		intel_context_unlock_pinned(ce);
	}

	return sz;
}
