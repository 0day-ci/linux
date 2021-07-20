/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_SUBMISSION_TYPES_H_
#define _INTEL_GUC_SUBMISSION_TYPES_H_

#include "gt/intel_engine_types.h"
#include "gt/intel_context_types.h"
#include "i915_scheduler_types.h"

struct intel_guc;
struct i915_request;

/* GuC Virtual Engine */
struct guc_virtual_engine {
	struct intel_engine_cs base;
	struct intel_context context;
};

/*
 * Object which encapsulates the globally operated on i915_sched_engine +
 * the GuC submission state machine described in intel_guc_submission.c.
 */
struct guc_submit_engine {
	struct i915_sched_engine sched_engine;
	struct work_struct retire_worker;
	struct i915_request *stalled_rq;
	struct intel_context *stalled_context;
	unsigned long flags;
	int total_num_rq_with_no_guc_id;
	atomic_t num_guc_ids_not_ready;
	int id;

	/*
	 * Submisson stall reason. See intel_guc_submission.c for detailed
	 * description.
	 */
	enum {
		STALL_NONE,
		STALL_GUC_ID_WORKQUEUE,
		STALL_GUC_ID_TASKLET,
		STALL_SCHED_DISABLE,
		STALL_REGISTER_CONTEXT,
		STALL_DEREGISTER_CONTEXT,
		STALL_MOVE_LRC_TAIL,
		STALL_ADD_REQUEST,
	} submission_stall_reason;

	I915_SELFTEST_DECLARE(u64 tasklets_submit_count;)
};

#endif
