// SPDX-License-Identifier: MIT
/*
 * Copyright �� 2021 Intel Corporation
 */

#include "selftests/igt_spinner.h"
#include "selftests/igt_reset.h"
#include "selftests/intel_scheduler_helpers.h"
#include "gt/intel_engine_heartbeat.h"
#include "gem/selftests/mock_context.h"

static int __request_add_spin(struct i915_request *rq, struct igt_spinner *spin)
{
	int err = 0;

	i915_request_get(rq);
	i915_request_add(rq);
	if (spin && !igt_wait_for_spinner(spin, rq))
		err = -ETIMEDOUT;

	return err;
}

static struct i915_request *nop_kernel_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	rq = intel_engine_create_kernel_request(engine);
	if (IS_ERR(rq))
		return rq;

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static struct i915_request *nop_user_request(struct intel_context *ce,
					     struct i915_request *from)
{
	struct i915_request *rq;
	int ret;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return rq;

	if (from) {
		ret = i915_sw_fence_await_dma_fence(&rq->submit,
						    &from->fence, 0,
						    I915_FENCE_GFP);
		if (ret < 0) {
			i915_request_put(rq);
			return ERR_PTR(ret);
		}
	}

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static int nop_request_wait(struct intel_engine_cs *engine, bool kernel,
			    bool flow_control)
{
	struct i915_gpu_error *global = &engine->gt->i915->gpu_error;
	unsigned int reset_count = i915_reset_count(global);
	struct intel_guc *guc = &engine->gt->uc.guc;
	struct guc_submit_engine *gse = guc->gse[GUC_SUBMIT_ENGINE_SINGLE_LRC];
	u64 tasklets_submit_count = gse->tasklets_submit_count;
	struct intel_context *ce;
	struct i915_request *nop;
	int ret;

	if (kernel) {
		nop = nop_kernel_request(engine);
	} else {
		ce = intel_context_create(engine);
		if (IS_ERR(ce))
			return PTR_ERR(ce);
		nop = nop_user_request(ce, NULL);
		intel_context_put(ce);
	}
	if (IS_ERR(nop))
		return PTR_ERR(nop);

	ret = intel_selftest_wait_for_rq(nop);
	i915_request_put(nop);
	if (ret)
		return ret;

	if (!flow_control &&
	    gse->tasklets_submit_count != tasklets_submit_count) {
		pr_err("Flow control for single-lrc unexpectedly kicked in\n");
		ret = -EINVAL;
	}

	if (flow_control &&
	    gse->tasklets_submit_count == tasklets_submit_count) {
		pr_err("Flow control for single-lrc did not kick in\n");
		ret = -EINVAL;
	}

	if (i915_reset_count(global) != reset_count) {
		pr_err("Unexpected GPU reset during single-lrc submit\n");
		ret = -EINVAL;
	}

	return ret;
}

static int multi_lrc_not_blocked(struct intel_gt *gt, bool flow_control)
{
	struct intel_guc *guc = &gt->uc.guc;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct guc_submit_engine *gse = guc->gse[GUC_SUBMIT_ENGINE_MULTI_LRC];
	unsigned int reset_count = i915_reset_count(global);
	u64 tasklets_submit_count = gse->tasklets_submit_count;
	struct intel_context *parent;
	struct i915_request *rq;
	int ret;

	parent = multi_lrc_create_parent(gt, VIDEO_DECODE_CLASS, 0);
	if (IS_ERR(parent)) {
		pr_err("Failed creating multi-lrc contexts: %ld",
		       PTR_ERR(parent));
		return PTR_ERR(parent);
	} else if (!parent) {
		pr_debug("Not enough engines in class: %d",
			 VIDEO_DECODE_CLASS);
		return 0;
	}

	rq = multi_lrc_nop_request(parent, NULL);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		pr_err("Failed creating multi-lrc requests: %d", ret);
		goto out;
	}

	ret = intel_selftest_wait_for_rq(rq);
	if (ret)
		pr_err("Failed waiting on multi-lrc request: %d", ret);

	i915_request_put(rq);
	if (ret)
		goto out;

	if (!flow_control &&
	    gse->tasklets_submit_count != tasklets_submit_count) {
		pr_err("Flow control for multi-lrc unexpectedly kicked in\n");
		ret = -EINVAL;
	}

	if (flow_control &&
	    gse->tasklets_submit_count == tasklets_submit_count) {
		pr_err("Flow control for multi-lrc did not kick in\n");
		ret = -EINVAL;
	}

	if (i915_reset_count(global) != reset_count) {
		pr_err("Unexpected GPU reset during multi-lrc submit\n");
		ret = -EINVAL;
	}

out:
	multi_lrc_context_put(parent);
	return ret;
}

#define NUM_GUC_ID		256
#define NUM_CONTEXT		1024
#define NUM_RQ_PER_CONTEXT	2
#define HEARTBEAT_INTERVAL	1500

static int __intel_guc_flow_control_guc(void *arg, bool limit_guc_ids, bool hang)
{
	struct intel_gt *gt = arg;
	struct intel_guc *guc = &gt->uc.guc;
	struct guc_submit_engine *gse = guc->gse[GUC_SUBMIT_ENGINE_SINGLE_LRC];
	struct intel_context **contexts;
	int ret = 0;
	int i, j, k;
	struct intel_context *ce;
	struct igt_spinner spin;
	struct i915_request *spin_rq = NULL, *last = NULL;
	intel_wakeref_t wakeref;
	struct intel_engine_cs *engine;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	unsigned int reset_count;
	u64 tasklets_submit_count = gse->tasklets_submit_count;
	u32 old_beat;

	contexts = kmalloc(sizeof(*contexts) * NUM_CONTEXT, GFP_KERNEL);
	if (!contexts) {
		pr_err("Context array allocation failed\n");
		return -ENOMEM;
	}

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	if (limit_guc_ids)
		guc->num_guc_ids = NUM_GUC_ID;

	ce = intel_context_create(intel_selftest_find_any_engine(gt));
	if (IS_ERR(ce)) {
		ret = PTR_ERR(ce);
		pr_err("Failed to create context: %d\n", ret);
		goto err;
	}

	reset_count = i915_reset_count(global);
	engine = ce->engine;

	old_beat = engine->props.heartbeat_interval_ms;
	if (hang) {
		ret = intel_engine_set_heartbeat(engine, HEARTBEAT_INTERVAL);
		if (ret) {
			pr_err("Failed to boost heartbeat interval: %d\n", ret);
			goto err;
		}
	}

	/* Create spinner to block requests in below loop */
	ret = igt_spinner_init(&spin, engine->gt);
	if (ret) {
		pr_err("Failed to create spinner: %d\n", ret);
		goto err_heartbeat;
	}
	spin_rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
	intel_context_put(ce);
	if (IS_ERR(spin_rq)) {
		ret = PTR_ERR(spin_rq);
		pr_err("Failed to create spinner request: %d\n", ret);
		goto err_heartbeat;
	}
	ret = __request_add_spin(spin_rq, &spin);
	if (ret) {
		pr_err("Failed to add Spinner request: %d\n", ret);
		goto err_spin_rq;
	}

	/*
	 * Create of lot of requests in a loop to trigger the flow control state
	 * machine. Using a three level loop as it is interesting to hit flow
	 * control with more than 1 request on each context in a row and also
	 * interleave requests with other contexts.
	 */
	for (i = 0; i < NUM_RQ_PER_CONTEXT; ++i) {
		for (j = 0; j < NUM_CONTEXT; ++j) {
			for (k = 0; k < NUM_RQ_PER_CONTEXT; ++k) {
				bool first_pass = !i && !k;

				if (last)
					i915_request_put(last);
				last = NULL;

				if (first_pass)
					contexts[j] = intel_context_create(engine);
				ce = contexts[j];

				if (IS_ERR(ce)) {
					ret = PTR_ERR(ce);
					pr_err("Failed to create context, %d,%d,%d: %d\n",
					       i, j, k, ret);
					goto err_spin_rq;
				}

				last = nop_user_request(ce, spin_rq);
				if (first_pass)
					intel_context_put(ce);
				if (IS_ERR(last)) {
					ret = PTR_ERR(last);
					pr_err("Failed to create request, %d,%d,%d: %d\n",
					       i, j, k, ret);
					goto err_spin_rq;
				}
			}
		}
	}

	/* Verify GuC submit engine state */
	if (limit_guc_ids && !guc_ids_exhausted(gse)) {
		pr_err("guc_ids not exhausted\n");
		ret = -EINVAL;
		goto err_spin_rq;
	}
	if (!limit_guc_ids && guc_ids_exhausted(gse)) {
		pr_err("guc_ids exhausted\n");
		ret = -EINVAL;
		goto err_spin_rq;
	}

	/* Ensure no DoS from unready requests */
	ret = nop_request_wait(engine, false, true);
	if (ret < 0) {
		pr_err("User NOP request DoS: %d\n", ret);
		goto err_spin_rq;
	}

	/* Ensure Multi-LRC not blocked */
	ret = multi_lrc_not_blocked(gt, !limit_guc_ids);
	if (ret < 0) {
		pr_err("Multi-lrc can't make progress: %d\n", ret);
		goto err_spin_rq;
	}

	/* Inject hang in flow control state machine */
	if (hang) {
		guc->gse_hang_expected = true;
		guc->inject_bad_sched_disable = true;
	}

	/* Release blocked requests */
	igt_spinner_end(&spin);
	ret = intel_selftest_wait_for_rq(spin_rq);
	if (ret) {
		pr_err("Spin request failed to complete: %d\n", ret);
		goto err_spin_rq;
	}
	i915_request_put(spin_rq);
	igt_spinner_fini(&spin);
	spin_rq = NULL;

	/* Wait for last request / GT to idle */
	ret = i915_request_wait(last, 0, hang ? HZ * 30 : HZ * 10);
	if (ret < 0) {
		pr_err("Last request failed to complete: %d\n", ret);
		goto err_spin_rq;
	}
	i915_request_put(last);
	last = NULL;
	ret = intel_gt_wait_for_idle(gt, HZ * 5);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err_spin_rq;
	}

	/* Check state after idle */
	if (guc_ids_exhausted(gse)) {
		pr_err("guc_ids exhausted after last request signaled\n");
		ret = -EINVAL;
		goto err_spin_rq;
	}
	if (hang) {
		if (i915_reset_count(global) == reset_count) {
			pr_err("Failed to record a GPU reset\n");
			ret = -EINVAL;
			goto err_spin_rq;
		}
	} else {
		if (i915_reset_count(global) != reset_count) {
			pr_err("Unexpected GPU reset\n");
			ret = -EINVAL;
			goto err_spin_rq;
		}
		if (gse->tasklets_submit_count == tasklets_submit_count) {
			pr_err("Flow control failed to kick in\n");
			ret = -EINVAL;
			goto err_spin_rq;
		}
	}

	/* Verify requests can be submitted after flow control */
	ret = nop_request_wait(engine, true, false);
	if (ret < 0) {
		pr_err("Kernel NOP failed to complete: %d\n", ret);
		goto err_spin_rq;
	}
	ret = nop_request_wait(engine, false, false);
	if (ret < 0) {
		pr_err("User NOP failed to complete: %d\n", ret);
		goto err_spin_rq;
	}

err_spin_rq:
	if (spin_rq) {
		igt_spinner_end(&spin);
		intel_selftest_wait_for_rq(spin_rq);
		i915_request_put(spin_rq);
		igt_spinner_fini(&spin);
		intel_gt_wait_for_idle(gt, HZ * 5);
	}
err_heartbeat:
	if (last)
		i915_request_put(last);
	intel_engine_set_heartbeat(engine, old_beat);
err:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	guc->num_guc_ids = guc->max_guc_ids;
	guc->gse_hang_expected = false;
	guc->inject_bad_sched_disable = false;
	kfree(contexts);

	return ret;
}

static int intel_guc_flow_control_guc_ids(void *arg)
{
	return __intel_guc_flow_control_guc(arg, true, false);
}

static int intel_guc_flow_control_lrcd_reg(void *arg)
{
	return __intel_guc_flow_control_guc(arg, false, false);
}

static int intel_guc_flow_control_hang_state_machine(void *arg)
{
	return __intel_guc_flow_control_guc(arg, true, true);
}

#define NUM_RQ_STRESS_CTBS	0x4000
static int intel_guc_flow_control_stress_ctbs(void *arg)
{
	struct intel_gt *gt = arg;
	int ret = 0;
	int i;
	struct intel_context *ce;
	struct i915_request *last = NULL, *rq;
	intel_wakeref_t wakeref;
	struct intel_engine_cs *engine;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	unsigned int reset_count;
	struct intel_guc *guc = &gt->uc.guc;
	struct intel_guc_ct_buffer *ctb = &guc->ct.ctbs.recv;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	reset_count = i915_reset_count(global);
	engine = intel_selftest_find_any_engine(gt);

	/*
	 * Create a bunch of requests, and then idle the GT which will create a
	 * lot of H2G / G2H traffic.
	 */
	for (i = 0; i < NUM_RQ_STRESS_CTBS; ++i) {
		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			ret = PTR_ERR(ce);
			pr_err("Failed to create context, %d: %d\n", i, ret);
			goto err;
		}

		rq = nop_user_request(ce, NULL);
		intel_context_put(ce);

		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			pr_err("Failed to create request, %d: %d\n", i, ret);
			goto err;
		}

		if (last)
			i915_request_put(last);
		last = rq;
	}

	ret = i915_request_wait(last, 0, HZ * 10);
	if (ret < 0) {
		pr_err("Last request failed to complete: %d\n", ret);
		goto err;
	}
	i915_request_put(last);
	last = NULL;

	ret = intel_gt_wait_for_idle(gt, HZ * 10);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err;
	}

	if (i915_reset_count(global) != reset_count) {
		pr_err("Unexpected GPU reset\n");
		ret = -EINVAL;
		goto err;
	}

	ret = nop_request_wait(engine, true, false);
	if (ret < 0) {
		pr_err("Kernel NOP failed to complete: %d\n", ret);
		goto err;
	}

	ret = nop_request_wait(engine, false, false);
	if (ret < 0) {
		pr_err("User NOP failed to complete: %d\n", ret);
		goto err;
	}

	ret = intel_gt_wait_for_idle(gt, HZ);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err;
	}

	ret = wait_for(intel_guc_ct_is_recv_buffer_empty(&guc->ct), HZ);
	if (ret) {
		pr_err("Recv CTB not expected value=%d,%d outstanding_ctb=%d\n",
		       atomic_read(&ctb->space),
		       CIRC_SPACE(0, 0, ctb->size) - ctb->resv_space,
		       atomic_read(&guc->outstanding_submission_g2h));
		ret = -EINVAL;
		goto err;
	}

err:
	if (last)
		i915_request_put(last);
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);

	return ret;
}

#define NUM_RQ_DEADLOCK		2048
static int __intel_guc_flow_control_deadlock_h2g(void *arg, bool bad_desc)
{
	struct intel_gt *gt = arg;
	struct intel_guc *guc = &gt->uc.guc;
	int ret = 0;
	int i;
	struct intel_context *ce;
	struct i915_request *last = NULL, *rq;
	intel_wakeref_t wakeref;
	struct intel_engine_cs *engine;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	unsigned int reset_count;
	u32 old_beat;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	reset_count = i915_reset_count(global);
	engine = intel_selftest_find_any_engine(gt);

	old_beat = engine->props.heartbeat_interval_ms;
	ret = intel_engine_set_heartbeat(engine, HEARTBEAT_INTERVAL);
	if (ret) {
		pr_err("Failed to boost heartbeat interval: %d\n", ret);
		goto err;
	}

	guc->inject_corrupt_h2g = true;
	if (bad_desc)
		guc->bad_desc_expected = true;
	else
		guc->deadlock_expected = true;

	for (i = 0; i < NUM_RQ_DEADLOCK; ++i) {
		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			ret = PTR_ERR(ce);
			pr_err("Failed to create context, %d: %d\n", i, ret);
			goto err_heartbeat;
		}

		rq = nop_user_request(ce, NULL);
		intel_context_put(ce);

		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			pr_err("Failed to create request, %d: %d\n", i, ret);
			goto err_heartbeat;
		}

		if (last)
			i915_request_put(last);
		last = rq;
	}

	pr_debug("Number requests before deadlock: %d\n", i);

	ret = i915_request_wait(last, 0, HZ * 5);
	if (ret < 0) {
		pr_err("Last request failed to complete: %d\n", ret);
		goto err_heartbeat;
	}
	i915_request_put(last);
	last = NULL;

	ret = intel_gt_wait_for_idle(gt, HZ * 10);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err_heartbeat;
	}

	if (i915_reset_count(global) == reset_count) {
		pr_err("Failed to record a GPU reset\n");
		ret = -EINVAL;
		goto err_heartbeat;
	}

	ret = nop_request_wait(engine, true, false);
	if (ret < 0) {
		pr_err("Kernel NOP failed to complete: %d\n", ret);
		goto err_heartbeat;
	}

	ret = nop_request_wait(engine, false, false);
	if (ret < 0) {
		pr_err("User NOP failed to complete: %d\n", ret);
		goto err_heartbeat;
	}

err_heartbeat:
	if (last)
		i915_request_put(last);
	intel_engine_set_heartbeat(engine, old_beat);
err:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	guc->inject_corrupt_h2g = false;
	guc->deadlock_expected = false;
	guc->bad_desc_expected = false;

	return ret;
}

static int intel_guc_flow_control_deadlock_h2g(void *arg)
{
	return __intel_guc_flow_control_deadlock_h2g(arg, false);
}

static int intel_guc_flow_control_bad_desc_h2g(void *arg)
{
	return __intel_guc_flow_control_deadlock_h2g(arg, true);
}

#define NUM_CONTEXT_MULTI_LRC	256

static int
__intel_guc_flow_control_multi_lrc_guc(void *arg, bool limit_guc_ids, bool hang)
{
	struct intel_gt *gt = arg;
	struct intel_guc *guc = &gt->uc.guc;
	struct guc_submit_engine *gse = guc->gse[GUC_SUBMIT_ENGINE_MULTI_LRC];
	struct intel_context **contexts;
	int ret = 0;
	int i, j, k;
	struct intel_context *ce;
	struct igt_spinner spin;
	struct i915_request *spin_rq, *last = NULL;
	intel_wakeref_t wakeref;
	struct intel_engine_cs *engine;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	unsigned int reset_count;
	u64 tasklets_submit_count = gse->tasklets_submit_count;
	u32 old_beat;

	if (limit_guc_ids)
		guc->num_guc_ids = NUM_GUC_ID;

	contexts = kmalloc(sizeof(*contexts) * NUM_CONTEXT, GFP_KERNEL);
	if (!contexts) {
		pr_err("Context array allocation failed\n");
		return -ENOMEM;
	}

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	ce = intel_context_create(intel_selftest_find_any_engine(gt));
	if (IS_ERR(ce)) {
		ret = PTR_ERR(ce);
		pr_err("Failed to create context: %d\n", ret);
		goto err;
	}

	reset_count = i915_reset_count(global);
	engine = ce->engine;

	old_beat = engine->props.heartbeat_interval_ms;
	if (hang) {
		ret = intel_engine_set_heartbeat(engine, HEARTBEAT_INTERVAL);
		if (ret) {
			pr_err("Failed to boost heatbeat interval: %d\n", ret);
			intel_context_put(ce);
			goto err;
		}
	}

	/* Create spinner to block requests in below loop */
	ret = igt_spinner_init(&spin, engine->gt);
	if (ret) {
		pr_err("Failed to create spinner: %d\n", ret);
		intel_context_put(ce);
		goto err_heartbeat;
	}
	spin_rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
	intel_context_put(ce);
	if (IS_ERR(spin_rq)) {
		ret = PTR_ERR(spin_rq);
		pr_err("Failed to create spinner request: %d\n", ret);
		goto err_spin_rq;
	}
	ret = __request_add_spin(spin_rq, &spin);
	if (ret) {
		pr_err("Failed to add Spinner request: %d\n", ret);
		goto err_spin_rq;
	}

	for (i = 0; i < NUM_RQ_PER_CONTEXT; ++i) {
		for (j = 0; j < NUM_CONTEXT_MULTI_LRC; ++j) {
			for (k = 0; k < NUM_RQ_PER_CONTEXT; ++k) {
				bool first_pass = !i && !k;

				if (last)
					i915_request_put(last);
				last = NULL;
				if (first_pass)
					contexts[j] = multi_lrc_create_parent(gt, VIDEO_DECODE_CLASS, 0);
				ce = contexts[j];

				if (IS_ERR(ce)) {
					ret = PTR_ERR(ce);
					pr_err("Failed to create context: %d\n", ret);
					goto err_spin_rq;
				} else if (!ce) {
					ret = 0;
					goto err_spin_rq;
				}

				last = multi_lrc_nop_request(ce, spin_rq);
				if (first_pass)
					multi_lrc_context_put(ce);
				if (IS_ERR(last)) {
					ret = PTR_ERR(last);
					pr_err("Failed to create request: %d\n", ret);
					goto err_spin_rq;
				}
			}
		}
	}

	/* Verify GuC submit engine state */
	if (limit_guc_ids && !guc_ids_exhausted(gse)) {
		pr_err("guc_ids not exhausted\n");
		ret = -EINVAL;
		goto err_spin_rq;
	}
	if (!limit_guc_ids && guc_ids_exhausted(gse)) {
		pr_err("guc_ids exhausted\n");
		ret = -EINVAL;
		goto err_spin_rq;
	}

	/* Ensure no DoS from unready requests */
	ret = multi_lrc_not_blocked(gt, true);
	if (ret < 0) {
		pr_err("Multi-lrc DoS: %d\n", ret);
		goto err_spin_rq;
	}

	/* Ensure Single-LRC not blocked, not in flow control */
	ret = nop_request_wait(engine, false, !limit_guc_ids);
	if (ret < 0) {
		pr_err("User NOP request DoS: %d\n", ret);
		goto err_spin_rq;
	}

	/* Inject hang in flow control state machine */
	if (hang) {
		guc->gse_hang_expected = true;
		guc->inject_bad_sched_disable = true;
	}

	/* Release blocked requests */
	igt_spinner_end(&spin);
	ret = intel_selftest_wait_for_rq(spin_rq);
	if (ret) {
		pr_err("Spin request failed to complete: %d\n", ret);
		goto err_spin_rq;
	}
	i915_request_put(spin_rq);
	igt_spinner_fini(&spin);
	spin_rq = NULL;

	/* Wait for last request / GT to idle */
	ret = i915_request_wait(last, 0, hang ? HZ * 30 : HZ * 5);
	if (ret < 0) {
		pr_err("Last request failed to complete: %d\n", ret);
		goto err_spin_rq;
	}
	i915_request_put(last);
	last = NULL;
	ret = intel_gt_wait_for_idle(gt, HZ * 5);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err_spin_rq;
	}

	/* Check state after idle */
	if (guc_ids_exhausted(gse)) {
		pr_err("guc_ids exhausted after last request signaled\n");
		ret = -EINVAL;
		goto err_spin_rq;
	}
	if (hang) {
		if (i915_reset_count(global) == reset_count) {
			pr_err("Failed to record a GPU reset\n");
			ret = -EINVAL;
			goto err_spin_rq;
		}
	} else {
		if (i915_reset_count(global) != reset_count) {
			pr_err("Unexpected GPU reset\n");
			ret = -EINVAL;
			goto err_spin_rq;
		}
		if (gse->tasklets_submit_count == tasklets_submit_count) {
			pr_err("Flow control failed to kick in\n");
			ret = -EINVAL;
			goto err_spin_rq;
		}
	}

	/* Verify requests can be submitted after flow control */
	ret = nop_request_wait(engine, true, false);
	if (ret < 0) {
		pr_err("Kernel NOP failed to complete: %d\n", ret);
		goto err_spin_rq;
	}
	ret = nop_request_wait(engine, false, false);
	if (ret < 0) {
		pr_err("User NOP failed to complete: %d\n", ret);
		goto err_spin_rq;
	}

err_spin_rq:
	if (spin_rq) {
		igt_spinner_end(&spin);
		intel_selftest_wait_for_rq(spin_rq);
		i915_request_put(spin_rq);
		igt_spinner_fini(&spin);
		intel_gt_wait_for_idle(gt, HZ * 5);
	}
err_heartbeat:
	if (last)
		i915_request_put(last);
	intel_engine_set_heartbeat(engine, old_beat);
err:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	guc->num_guc_ids = guc->max_guc_ids;
	guc->gse_hang_expected = false;
	guc->inject_bad_sched_disable = false;
	kfree(contexts);

	return ret;
}

static int intel_guc_flow_control_multi_lrc_guc_ids(void *arg)
{
	return __intel_guc_flow_control_multi_lrc_guc(arg, true, false);
}

static int intel_guc_flow_control_multi_lrc_hang(void *arg)
{
	return __intel_guc_flow_control_multi_lrc_guc(arg, true, true);
}

int intel_guc_flow_control(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(intel_guc_flow_control_stress_ctbs),
		SUBTEST(intel_guc_flow_control_guc_ids),
		SUBTEST(intel_guc_flow_control_lrcd_reg),
		SUBTEST(intel_guc_flow_control_hang_state_machine),
		SUBTEST(intel_guc_flow_control_multi_lrc_guc_ids),
		SUBTEST(intel_guc_flow_control_multi_lrc_hang),
		SUBTEST(intel_guc_flow_control_deadlock_h2g),
		SUBTEST(intel_guc_flow_control_bad_desc_h2g),
	};
	struct intel_gt *gt = &i915->gt;

	if (intel_gt_is_wedged(gt))
		return 0;

	if (!intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
