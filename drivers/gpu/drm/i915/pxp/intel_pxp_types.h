/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TYPES_H__
#define __INTEL_PXP_TYPES_H__

#include <linux/completion.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct intel_context;
struct i915_pxp_component;

struct intel_pxp {
	struct i915_pxp_component *pxp_component;

	struct intel_context *ce;

	/*
	 * After a teardown, the arb session can still be in play on the HW
	 * even if the keys are gone, so we can't rely on the HW state of the
	 * session to know if it's valid and need to track the status in SW.
	 */
	bool arb_is_valid;
	bool global_state_attacked;
	bool irq_enabled;
	struct completion termination;

	struct work_struct session_work;
	u32 session_events; /* protected with gt->irq_lock */
#define PXP_TERMINATION_REQUEST  BIT(0)
#define PXP_TERMINATION_COMPLETE BIT(1)
};

#endif /* __INTEL_PXP_TYPES_H__ */
