// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014 Intel Corporation
 */

#include <linux/circ_buf.h>

#include "gem/i915_gem_context.h"
#include "gt/gen8_engine_cs.h"
#include "gt/intel_breadcrumbs.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_irq.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_requests.h"
#include "gt/intel_lrc.h"
#include "gt/intel_lrc_reg.h"
#include "gt/intel_mocs.h"
#include "gt/intel_ring.h"

#include "intel_guc_submission.h"
#include "intel_guc_submission_types.h"

#include "i915_drv.h"
#include "i915_trace.h"

/**
 * DOC: GuC-based command submission
 *
 * IMPORTANT NOTE: GuC submission is currently not supported in i915. The GuC
 * firmware is moving to an updated submission interface and we plan to
 * turn submission back on when that lands. The below documentation (and related
 * code) matches the old submission model and will be updated as part of the
 * upgrade to the new flow.
 *
 * GuC stage descriptor:
 * During initialization, the driver allocates a static pool of 1024 such
 * descriptors, and shares them with the GuC. Currently, we only use one
 * descriptor. This stage descriptor lets the GuC know about the workqueue and
 * process descriptor. Theoretically, it also lets the GuC know about our HW
 * contexts (context ID, etc...), but we actually employ a kind of submission
 * where the GuC uses the LRCA sent via the work item instead. This is called
 * a "proxy" submission.
 *
 * The Scratch registers:
 * There are 16 MMIO-based registers start from 0xC180. The kernel driver writes
 * a value to the action register (SOFT_SCRATCH_0) along with any data. It then
 * triggers an interrupt on the GuC via another register write (0xC4C8).
 * Firmware writes a success/fail code back to the action register after
 * processes the request. The kernel driver polls waiting for this update and
 * then proceeds.
 *
 * Work Items:
 * There are several types of work items that the host may place into a
 * workqueue, each with its own requirements and limitations. Currently only
 * WQ_TYPE_INORDER is needed to support legacy submission via GuC, which
 * represents in-order queue. The kernel driver packs ring tail pointer and an
 * ELSP context descriptor dword into Work Item.
 * See gse_add_request()
 *
 * GuC flow control state machine:
 * The tasklet, workqueue (retire_worker), and the G2H handlers together more or
 * less form a state machine which is used to submit requests + flow control
 * requests, while waiting on resources / actions, if necessary. The enum,
 * submission_stall_reason, controls the handoff of stalls between these
 * entities with stalled_rq & stalled_context being the arguments. Each state
 * described below.
 *
 * STALL_NONE			No stall condition
 * STALL_GUC_ID_WORKQUEUE	Workqueue will try to free guc_ids
 * STALL_GUC_ID_TASKLET		Tasklet will try to find guc_id
 * STALL_SCHED_DISABLE		Workqueue will issue context schedule disable
 *				H2G
 * STALL_REGISTER_CONTEXT	Tasklet needs to register context
 * STALL_DEREGISTER_CONTEXT	G2H handler is waiting for context deregister,
 *				will register context upon receipt of G2H
 * STALL_MOVE_LRC_TAIL		Tasklet will try to move LRC tail
 * STALL_ADD_REQUEST		Tasklet will try to add the request (submit
 *				context)
 */

static struct intel_context *
guc_create_virtual(struct intel_engine_cs **siblings, unsigned int count);

#define GUC_REQUEST_SIZE 64 /* bytes */

static inline struct guc_submit_engine *ce_to_gse(struct intel_context *ce)
{
	return container_of(ce->engine->sched_engine, struct guc_submit_engine,
			    sched_engine);
}

/*
 * Global GuC flags helper functions
 */
enum {
	GSE_STATE_TASKLET_BLOCKED,
	GSE_STATE_GUC_IDS_EXHAUSTED,
};

static bool tasklet_blocked(struct guc_submit_engine *gse)
{
	return test_bit(GSE_STATE_TASKLET_BLOCKED, &gse->flags);
}

static void set_tasklet_blocked(struct guc_submit_engine *gse)
{
	lockdep_assert_held(&gse->sched_engine.lock);
	set_bit(GSE_STATE_TASKLET_BLOCKED, &gse->flags);
}

static void __clr_tasklet_blocked(struct guc_submit_engine *gse)
{
	lockdep_assert_held(&gse->sched_engine.lock);
	clear_bit(GSE_STATE_TASKLET_BLOCKED, &gse->flags);
}

static void clr_tasklet_blocked(struct guc_submit_engine *gse)
{
	unsigned long flags;

	spin_lock_irqsave(&gse->sched_engine.lock, flags);
	__clr_tasklet_blocked(gse);
	spin_unlock_irqrestore(&gse->sched_engine.lock, flags);
}

static bool guc_ids_exhausted(struct guc_submit_engine *gse)
{
	return test_bit(GSE_STATE_GUC_IDS_EXHAUSTED, &gse->flags);
}

static bool test_and_update_guc_ids_exhausted(struct guc_submit_engine *gse)
{
	unsigned long flags;
	bool ret = false;

	/*
	 * Strict ordering on checking if guc_ids are exhausted isn't required,
	 * so let's avoid grabbing the submission lock if possible.
	 */
	if (guc_ids_exhausted(gse)) {
		spin_lock_irqsave(&gse->sched_engine.lock, flags);
		ret = guc_ids_exhausted(gse);
		if (ret)
			++gse->total_num_rq_with_no_guc_id;
		spin_unlock_irqrestore(&gse->sched_engine.lock, flags);
	}

	return ret;
}

static void set_and_update_guc_ids_exhausted(struct guc_submit_engine *gse)
{
	unsigned long flags;

	spin_lock_irqsave(&gse->sched_engine.lock, flags);
	++gse->total_num_rq_with_no_guc_id;
	set_bit(GSE_STATE_GUC_IDS_EXHAUSTED, &gse->flags);
	spin_unlock_irqrestore(&gse->sched_engine.lock, flags);
}

static void clr_guc_ids_exhausted(struct guc_submit_engine *gse)
{
	lockdep_assert_held(&gse->sched_engine.lock);
	GEM_BUG_ON(gse->total_num_rq_with_no_guc_id);

	clear_bit(GSE_STATE_GUC_IDS_EXHAUSTED, &gse->flags);
}

/*
 * Below is a set of functions which control the GuC scheduling state which do
 * not require a lock as all state transitions are mutually exclusive. i.e. It
 * is not possible for the context pinning code and submission, for the same
 * context, to be executing simultaneously. We still need an atomic as it is
 * possible for some of the bits to changing at the same time though.
 */
#define SCHED_STATE_NO_LOCK_ENABLED			BIT(0)
#define SCHED_STATE_NO_LOCK_PENDING_ENABLE		BIT(1)
#define SCHED_STATE_NO_LOCK_REGISTERED			BIT(2)
#define SCHED_STATE_NO_LOCK_BLOCK_TASKLET		BIT(3)
#define SCHED_STATE_NO_LOCK_GUC_ID_STOLEN		BIT(4)
#define SCHED_STATE_NO_LOCK_NEEDS_REGISTER		BIT(5)
#define SCHED_STATE_NO_LOCK_BLOCKED_SHIFT		6
#define SCHED_STATE_NO_LOCK_BLOCKED \
	BIT(SCHED_STATE_NO_LOCK_BLOCKED_SHIFT)
#define SCHED_STATE_NO_LOCK_BLOCKED_MASK \
	(0xffff << SCHED_STATE_NO_LOCK_BLOCKED_SHIFT)
static inline bool context_enabled(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_ENABLED);
}

static inline void set_context_enabled(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_ENABLED, &ce->guc_sched_state_no_lock);
}

static inline void clr_context_enabled(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_ENABLED,
		   &ce->guc_sched_state_no_lock);
}

static inline bool context_pending_enable(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_PENDING_ENABLE);
}

static inline void set_context_pending_enable(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_PENDING_ENABLE,
		  &ce->guc_sched_state_no_lock);
}

static inline void clr_context_pending_enable(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_PENDING_ENABLE,
		   &ce->guc_sched_state_no_lock);
}

static inline u32 context_blocked(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_BLOCKED_MASK) >>
		SCHED_STATE_NO_LOCK_BLOCKED_SHIFT;
}

static inline void incr_context_blocked(struct intel_context *ce)
{
	lockdep_assert_held(&ce->engine->sched_engine->lock);
	atomic_add(SCHED_STATE_NO_LOCK_BLOCKED,
		   &ce->guc_sched_state_no_lock);
}

static inline void decr_context_blocked(struct intel_context *ce)
{
	lockdep_assert_held(&ce->engine->sched_engine->lock);
	atomic_sub(SCHED_STATE_NO_LOCK_BLOCKED,
		   &ce->guc_sched_state_no_lock);
}

static inline bool context_registered(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_REGISTERED);
}

static inline void set_context_registered(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_REGISTERED,
		  &ce->guc_sched_state_no_lock);
}

static inline void clr_context_registered(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_REGISTERED,
		   &ce->guc_sched_state_no_lock);
}

static inline bool context_block_tasklet(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_BLOCK_TASKLET);
}

static inline void set_context_block_tasklet(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_BLOCK_TASKLET,
		  &ce->guc_sched_state_no_lock);
}

static inline void clr_context_block_tasklet(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_BLOCK_TASKLET,
		   &ce->guc_sched_state_no_lock);
}

static inline bool context_guc_id_stolen(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_GUC_ID_STOLEN);
}

static inline void set_context_guc_id_stolen(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_GUC_ID_STOLEN,
		  &ce->guc_sched_state_no_lock);
}

static inline void clr_context_guc_id_stolen(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_GUC_ID_STOLEN,
		   &ce->guc_sched_state_no_lock);
}

static inline bool context_needs_register(struct intel_context *ce)
{
	return (atomic_read(&ce->guc_sched_state_no_lock) &
		SCHED_STATE_NO_LOCK_NEEDS_REGISTER);
}

static inline void set_context_needs_register(struct intel_context *ce)
{
	atomic_or(SCHED_STATE_NO_LOCK_NEEDS_REGISTER,
		  &ce->guc_sched_state_no_lock);
}

static inline void clr_context_needs_register(struct intel_context *ce)
{
	atomic_and((u32)~SCHED_STATE_NO_LOCK_NEEDS_REGISTER,
		   &ce->guc_sched_state_no_lock);
}

/*
 * Below is a set of functions which control the GuC scheduling state which
 * require a lock, aside from the special case where the functions are called
 * from guc_lrc_desc_pin(). In that case it isn't possible for any other code
 * path to be executing on the context.
 */
#define SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER	BIT(0)
#define SCHED_STATE_DESTROYED				BIT(1)
#define SCHED_STATE_PENDING_DISABLE			BIT(2)
#define SCHED_STATE_BANNED				BIT(3)
static inline void init_sched_state(struct intel_context *ce)
{
	/* Only should be called from guc_lrc_desc_pin() */
	atomic_set(&ce->guc_sched_state_no_lock, 0);
	ce->guc_state.sched_state = 0;
}

static inline bool
context_wait_for_deregister_to_register(struct intel_context *ce)
{
	return ce->guc_state.sched_state &
		SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER;
}

static inline void
set_context_wait_for_deregister_to_register(struct intel_context *ce)
{
	/* Only should be called from guc_lrc_desc_pin() without lock */
	ce->guc_state.sched_state |=
		SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER;
}

static inline void
clr_context_wait_for_deregister_to_register(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state &=
		~SCHED_STATE_WAIT_FOR_DEREGISTER_TO_REGISTER;
}

static inline bool
context_destroyed(struct intel_context *ce)
{
	return ce->guc_state.sched_state & SCHED_STATE_DESTROYED;
}

static inline void
set_context_destroyed(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state |= SCHED_STATE_DESTROYED;
}

static inline bool context_pending_disable(struct intel_context *ce)
{
	return ce->guc_state.sched_state & SCHED_STATE_PENDING_DISABLE;
}

static inline void set_context_pending_disable(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state |= SCHED_STATE_PENDING_DISABLE;
}

static inline void clr_context_pending_disable(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state &= ~SCHED_STATE_PENDING_DISABLE;
}

static inline bool context_banned(struct intel_context *ce)
{
	return ce->guc_state.sched_state & SCHED_STATE_BANNED;
}

static inline void set_context_banned(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state |= SCHED_STATE_BANNED;
}

static inline void clr_context_banned(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	ce->guc_state.sched_state &= ~SCHED_STATE_BANNED;
}

static inline bool context_guc_id_invalid(struct intel_context *ce)
{
	return (ce->guc_id == GUC_INVALID_LRC_ID);
}

static inline void set_context_guc_id_invalid(struct intel_context *ce)
{
	ce->guc_id = GUC_INVALID_LRC_ID;
}

static inline struct intel_guc *ce_to_guc(struct intel_context *ce)
{
	return &ce->engine->gt->uc.guc;
}

static inline struct i915_sched_engine *
ce_to_sched_engine(struct intel_context *ce)
{
	return ce->engine->sched_engine;
}

static inline struct i915_sched_engine *
guc_to_sched_engine(struct intel_guc *guc, int index)
{
	GEM_BUG_ON(index < 0 || index >= GUC_SUBMIT_ENGINE_MAX);

	return &guc->gse[index]->sched_engine;
}

static inline struct i915_priolist *to_priolist(struct rb_node *rb)
{
	return rb_entry(rb, struct i915_priolist, node);
}

static struct guc_lrc_desc *__get_lrc_desc(struct intel_guc *guc, u32 index)
{
	struct guc_lrc_desc *base = guc->lrc_desc_pool_vaddr;

	GEM_BUG_ON(index >= guc->max_guc_ids);

	return &base[index];
}

static inline struct intel_context *__get_context(struct intel_guc *guc, u32 id)
{
	struct intel_context *ce = xa_load(&guc->context_lookup, id);

	GEM_BUG_ON(id >= guc->max_guc_ids);

	return ce;
}

static int guc_lrc_desc_pool_create(struct intel_guc *guc)
{
	u32 size;
	int ret;

	size = PAGE_ALIGN(sizeof(struct guc_lrc_desc) * guc->max_guc_ids);
	ret = intel_guc_allocate_and_map_vma(guc, size, &guc->lrc_desc_pool,
					     (void **)&guc->lrc_desc_pool_vaddr);
	if (ret)
		return ret;

	return 0;
}

static void guc_lrc_desc_pool_destroy(struct intel_guc *guc)
{
	guc->lrc_desc_pool_vaddr = NULL;
	i915_vma_unpin_and_release(&guc->lrc_desc_pool, I915_VMA_RELEASE_MAP);
}

static inline bool guc_submission_initialized(struct intel_guc *guc)
{
	return guc->lrc_desc_pool_vaddr != NULL;
}

static inline void reset_lrc_desc(struct intel_guc *guc, u32 id)
{
	if (likely(guc_submission_initialized(guc))) {
		struct guc_lrc_desc *desc = __get_lrc_desc(guc, id);
		unsigned long flags;

		memset(desc, 0, sizeof(*desc));

		/*
		 * xarray API doesn't have xa_erase_irqsave wrapper, so calling
		 * the lower level functions directly.
		 */
		xa_lock_irqsave(&guc->context_lookup, flags);
		__xa_erase(&guc->context_lookup, id);
		xa_unlock_irqrestore(&guc->context_lookup, flags);
	}
}

static inline bool lrc_desc_registered(struct intel_guc *guc, u32 id)
{
	return __get_context(guc, id);
}

static inline void set_lrc_desc_registered(struct intel_guc *guc, u32 id,
					   struct intel_context *ce)
{
	unsigned long flags;

	/*
	 * xarray API doesn't have xa_save_irqsave wrapper, so calling the
	 * lower level functions directly.
	 */
	xa_lock_irqsave(&guc->context_lookup, flags);
	__xa_store(&guc->context_lookup, id, ce, GFP_ATOMIC);
	xa_unlock_irqrestore(&guc->context_lookup, flags);
}

static int guc_submission_send_busy_loop(struct intel_guc* guc,
					 const u32 *action,
					 u32 len,
					 u32 g2h_len_dw,
					 bool loop)
{
	int err;

	err = intel_guc_send_busy_loop(guc, action, len, g2h_len_dw, loop);

	if (!err && g2h_len_dw)
		atomic_inc(&guc->outstanding_submission_g2h);

	return err;
}

int intel_guc_wait_for_pending_msg(struct intel_guc *guc,
				   atomic_t *wait_var,
				   bool interruptible,
				   long timeout)
{
	const int state = interruptible ?
		TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
	DEFINE_WAIT(wait);

	might_sleep();
	GEM_BUG_ON(timeout < 0);

	if (!atomic_read(wait_var))
		return 0;

	if (!timeout)
		return -ETIME;

	for (;;) {
		prepare_to_wait(&guc->ct.wq, &wait, state);

		if (!atomic_read(wait_var))
			break;

		if (signal_pending_state(state, current)) {
			timeout = -EINTR;
			break;
		}

		if (!timeout) {
			timeout = -ETIME;
			break;
		}

		timeout = io_schedule_timeout(timeout);
	}
	finish_wait(&guc->ct.wq, &wait);

	return (timeout < 0) ? timeout : 0;
}

int intel_guc_wait_for_idle(struct intel_guc *guc, long timeout)
{
	if (!intel_uc_uses_guc_submission(&guc_to_gt(guc)->uc))
		return 0;

	return intel_guc_wait_for_pending_msg(guc,
					      &guc->outstanding_submission_g2h,
					      true, timeout);
}

static inline bool request_has_no_guc_id(struct i915_request *rq)
{
	return test_bit(I915_FENCE_FLAG_GUC_ID_NOT_PINNED, &rq->fence.flags);
}

static int __guc_add_request(struct intel_guc *guc, struct i915_request *rq)
{
	int err = 0;
	struct intel_context *ce = rq->context;
	u32 action[3];
	int len = 0;
	u32 g2h_len_dw = 0;
	bool enabled;

	/*
	 * Corner case where requests were sitting in the priority list or a
	 * request resubmitted after the context was banned.
	 */
	if (unlikely(intel_context_is_banned(ce))) {
		i915_request_put(i915_request_mark_eio(rq));
		intel_engine_signal_breadcrumbs(ce->engine);
		goto out;
	}

	/* Ensure context is in correct state before a submission */
	GEM_BUG_ON(ce->guc_num_rq_submit_no_id);
	GEM_BUG_ON(request_has_no_guc_id(rq));
	GEM_BUG_ON(!atomic_read(&ce->guc_id_ref));
	GEM_BUG_ON(context_needs_register(ce));
	GEM_BUG_ON(context_guc_id_invalid(ce));
	GEM_BUG_ON(context_pending_disable(ce));
	GEM_BUG_ON(context_wait_for_deregister_to_register(ce));
	GEM_BUG_ON(!lrc_desc_registered(guc, ce->guc_id));

	if (unlikely(context_blocked(ce)))
		goto out;

	enabled = context_enabled(ce);

	if (!enabled) {
		GEM_BUG_ON(context_pending_enable(ce));

		action[len++] = INTEL_GUC_ACTION_SCHED_CONTEXT_MODE_SET;
		action[len++] = ce->guc_id;
		action[len++] = GUC_CONTEXT_ENABLE;
		set_context_pending_enable(ce);
		intel_context_get(ce);
		g2h_len_dw = G2H_LEN_DW_SCHED_CONTEXT_MODE_SET;
	} else {
		action[len++] = INTEL_GUC_ACTION_SCHED_CONTEXT;
		action[len++] = ce->guc_id;
	}

	err = intel_guc_send_nb(guc, action, len, g2h_len_dw);
	if (!enabled && !err) {
		trace_intel_context_sched_enable(ce);
		atomic_inc(&guc->outstanding_submission_g2h);
		set_context_enabled(ce);
	} else if (!enabled) {
		clr_context_pending_enable(ce);
		intel_context_put(ce);
	}
	if (likely(!err))
		trace_i915_request_guc_submit(rq);

out:
	return err;
}

static int gse_add_request(struct guc_submit_engine *gse,
			   struct i915_request *rq)
{
	int ret;

	lockdep_assert_held(&gse->sched_engine.lock);

	ret = __guc_add_request(gse->sched_engine.private_data, rq);
	if (ret == -EBUSY) {
		gse->stalled_rq = rq;
		gse->submission_stall_reason = STALL_ADD_REQUEST;
	} else {
		gse->stalled_rq = NULL;
		gse->submission_stall_reason = STALL_NONE;
	}

	return ret;
}

static int guc_lrc_desc_pin(struct intel_context *ce, bool loop);

static int tasklet_register_context(struct guc_submit_engine *gse,
				    struct i915_request *rq)
{
	struct intel_context *ce = rq->context;
	struct intel_guc *guc = gse->sched_engine.private_data;
	int ret = 0;

	/* Check state */
	lockdep_assert_held(&gse->sched_engine.lock);
	GEM_BUG_ON(ce->guc_num_rq_submit_no_id);
	GEM_BUG_ON(request_has_no_guc_id(rq));
	GEM_BUG_ON(context_guc_id_invalid(ce));
	GEM_BUG_ON(!atomic_read(&ce->guc_id_ref));

	/*
	 * The guc_id is getting pinned during the tasklet and we need to
	 * register this context or a corner case where the GuC firwmare was
	 * blown away and reloaded while this context was pinned
	 */
	if (unlikely((!lrc_desc_registered(guc, ce->guc_id) ||
		      context_needs_register(ce)) &&
		     !intel_context_is_banned(ce))) {
		GEM_BUG_ON(context_pending_disable(ce));
		GEM_BUG_ON(context_wait_for_deregister_to_register(ce));

		ret = guc_lrc_desc_pin(ce, false);

		if (likely(ret != -EBUSY))
			clr_context_needs_register(ce);

		if (unlikely(ret == -EBUSY)) {
			gse->stalled_rq = rq;
			gse->submission_stall_reason = STALL_REGISTER_CONTEXT;
		} else if (unlikely(ret == -EINPROGRESS)) {
			gse->stalled_rq = rq;
			gse->submission_stall_reason = STALL_DEREGISTER_CONTEXT;
		}
	}

	return ret;
}


static inline void guc_set_lrc_tail(struct i915_request *rq)
{
	rq->context->lrc_reg_state[CTX_RING_TAIL] =
		intel_ring_set_tail(rq->ring, rq->tail);
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority;
}

static void kick_retire_wq(struct guc_submit_engine *gse)
{
	queue_work(system_unbound_wq, &gse->retire_worker);
}

static int tasklet_pin_guc_id(struct guc_submit_engine *gse,
			      struct i915_request *rq);

static int gse_dequeue_one_context(struct guc_submit_engine *gse)
{
	struct i915_sched_engine * const sched_engine = &gse->sched_engine;
	struct i915_request *last = gse->stalled_rq;
	bool submit = !!last;
	struct rb_node *rb;
	int ret;

	lockdep_assert_held(&sched_engine->lock);
	GEM_BUG_ON(gse->stalled_context);
	GEM_BUG_ON(!submit && gse->submission_stall_reason);

	if (submit) {
		/* Flow control conditions */
		switch (gse->submission_stall_reason) {
		case STALL_GUC_ID_TASKLET:
			goto done;
		case STALL_REGISTER_CONTEXT:
			goto register_context;
		case STALL_MOVE_LRC_TAIL:
			goto move_lrc_tail;
		case STALL_ADD_REQUEST:
			goto add_request;
		default:
			GEM_BUG_ON("Invalid stall state");
		}
	} else {
		GEM_BUG_ON(!gse->total_num_rq_with_no_guc_id &&
			   guc_ids_exhausted(gse));

		while ((rb = rb_first_cached(&sched_engine->queue))) {
			struct i915_priolist *p = to_priolist(rb);
			struct i915_request *rq, *rn;

			priolist_for_each_request_consume(rq, rn, p) {
				if (last && rq->context != last->context)
					goto done;

				list_del_init(&rq->sched.link);

				__i915_request_submit(rq);

				trace_i915_request_in(rq, 0);
				last = rq;
				submit = true;
			}

			rb_erase_cached(&p->node, &sched_engine->queue);
			i915_priolist_free(p);
		}
	}

done:
	if (submit) {
		struct intel_context *ce = last->context;

		if (ce->guc_num_rq_submit_no_id) {
			ret = tasklet_pin_guc_id(gse, last);
			if (ret)
				goto blk_tasklet_kick;
		}

register_context:
		ret = tasklet_register_context(gse, last);
		if (unlikely(ret == -EINPROGRESS))
			goto blk_tasklet;
		else if (unlikely(ret == -EPIPE))
			goto deadlk;
		else if (ret == -EBUSY)
			goto schedule_tasklet;
		else if (unlikely(ret != 0)) {
			GEM_WARN_ON(ret);	/* Unexpected */
			goto deadlk;
		}

move_lrc_tail:
		guc_set_lrc_tail(last);

add_request:
		ret = gse_add_request(gse, last);
		if (unlikely(ret == -EPIPE))
			goto deadlk;
		else if (ret == -EBUSY)
			goto schedule_tasklet;
		else if (unlikely(ret != 0)) {
			GEM_WARN_ON(ret);	/* Unexpected */
			goto deadlk;
		}
	}

	/*
	 * No requests without a guc_id, enable guc_id allocation at request
	 * creation time (guc_request_alloc).
	 */
	if (!gse->total_num_rq_with_no_guc_id)
		clr_guc_ids_exhausted(gse);

	return submit;

schedule_tasklet:
	tasklet_schedule(&sched_engine->tasklet);
	return false;

deadlk:
	sched_engine->tasklet.callback = NULL;
	tasklet_disable_nosync(&sched_engine->tasklet);
	return false;

blk_tasklet_kick:
	kick_retire_wq(gse);
blk_tasklet:
	set_tasklet_blocked(gse);
	return false;
}

static void gse_submission_tasklet(struct tasklet_struct *t)
{
	struct i915_sched_engine *sched_engine =
		from_tasklet(sched_engine, t, tasklet);
	struct guc_submit_engine *gse =
		container_of(sched_engine, typeof(*gse), sched_engine);
	unsigned long flags;
	bool loop;

	spin_lock_irqsave(&sched_engine->lock, flags);

	if (likely(!tasklet_blocked(gse)))
		do {
			loop = gse_dequeue_one_context(gse);
		} while (loop);

	i915_sched_engine_reset_on_empty(sched_engine);

	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void cs_irq_handler(struct intel_engine_cs *engine, u16 iir)
{
	if (iir & GT_RENDER_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(engine);
}

static void __guc_context_destroy(struct intel_context *ce);
static void release_guc_id(struct intel_guc *guc, struct intel_context *ce);
static void guc_signal_context_fence(struct intel_context *ce);
static void guc_cancel_context_requests(struct intel_context *ce);
static void guc_blocked_fence_complete(struct intel_context *ce);

static void scrub_guc_desc_for_outstanding_g2h(struct intel_guc *guc)
{
	struct intel_context *ce;
	unsigned long index, flags;
	bool pending_disable, pending_enable, deregister, destroyed, banned;

	xa_for_each(&guc->context_lookup, index, ce) {
		/* Flush context */
		spin_lock_irqsave(&ce->guc_state.lock, flags);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		/*
		 * Once we are at this point submission_disabled() is guaranteed
		 * to visible to all callers who set the below flags (see above
		 * flush and flushes in reset_prepare). If submission_disabled()
		 * is set, the caller shouldn't set these flags.
		 */

		destroyed = context_destroyed(ce);
		pending_enable = context_pending_enable(ce);
		pending_disable = context_pending_disable(ce);
		deregister = context_wait_for_deregister_to_register(ce);
		banned = context_banned(ce);
		init_sched_state(ce);

		if (pending_enable || destroyed || deregister) {
			atomic_dec(&guc->outstanding_submission_g2h);
			if (deregister)
				guc_signal_context_fence(ce);
			if (destroyed) {
				release_guc_id(guc, ce);
				__guc_context_destroy(ce);
			}
			if (pending_enable|| deregister)
				intel_context_put(ce);
		}

		/* Not mutualy exclusive with above if statement. */
		if (pending_disable) {
			guc_signal_context_fence(ce);
			if (banned) {
				guc_cancel_context_requests(ce);
				intel_engine_signal_breadcrumbs(ce->engine);
			}
			intel_context_sched_disable_unpin(ce);
			atomic_dec(&guc->outstanding_submission_g2h);
			spin_lock_irqsave(&ce->guc_state.lock, flags);
			guc_blocked_fence_complete(ce);
			spin_unlock_irqrestore(&ce->guc_state.lock, flags);

			intel_context_put(ce);
		}
	}
}

static bool submission_disabled(struct intel_guc *guc)
{
	int i;

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i) {
		struct i915_sched_engine *sched_engine;

		if (unlikely(!guc->gse[i]))
			return true;

		sched_engine = guc_to_sched_engine(guc, i);

		if (unlikely(!__tasklet_is_enabled(&sched_engine->tasklet)))
			return true;
	}

	return false;
}

static void kick_tasklet(struct guc_submit_engine *gse)
{
	struct i915_sched_engine *sched_engine = &gse->sched_engine;

	if (likely(!tasklet_blocked(gse)))
		tasklet_hi_schedule(&sched_engine->tasklet);
}

static void disable_submission(struct intel_guc *guc)
{
	int i;

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i) {
		struct i915_sched_engine *sched_engine =
			guc_to_sched_engine(guc, i);

		if (__tasklet_is_enabled(&sched_engine->tasklet)) {
			GEM_BUG_ON(!guc->ct.enabled);
			__tasklet_disable_sync_once(&sched_engine->tasklet);
			sched_engine->tasklet.callback = NULL;
		}
	}
}

static void enable_submission(struct intel_guc *guc)
{
	unsigned long flags;
	int i;

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i) {
		struct i915_sched_engine *sched_engine =
			guc_to_sched_engine(guc, i);
		struct guc_submit_engine *gse = guc->gse[i];

		spin_lock_irqsave(&sched_engine->lock, flags);
		sched_engine->tasklet.callback = gse_submission_tasklet;
		wmb();
		if (!__tasklet_is_enabled(&sched_engine->tasklet) &&
		    __tasklet_enable(&sched_engine->tasklet)) {
			GEM_BUG_ON(!guc->ct.enabled);

			/* Reset GuC submit engine state */
			gse->stalled_rq = NULL;
			if (gse->stalled_context)
				intel_context_put(gse->stalled_context);
			gse->stalled_context = NULL;
			gse->submission_stall_reason = STALL_NONE;
			gse->flags = 0;

			/* And kick in case we missed a new request submission. */
			kick_tasklet(gse);
		}
		spin_unlock_irqrestore(&sched_engine->lock, flags);
	}
}

static void gse_flush_submissions(struct guc_submit_engine *gse)
{
	struct i915_sched_engine * const sched_engine = &gse->sched_engine;
	unsigned long flags;

	spin_lock_irqsave(&sched_engine->lock, flags);
	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void guc_flush_submissions(struct intel_guc *guc)
{
	int i;

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i)
		if (likely(guc->gse[i]))
			gse_flush_submissions(guc->gse[i]);
}

void intel_guc_submission_reset_prepare(struct intel_guc *guc)
{
	int i;

	if (unlikely(!guc_submission_initialized(guc)))
		/* Reset called during driver load? GuC not yet initialised! */
		return;

	intel_gt_park_heartbeats(guc_to_gt(guc));
	disable_submission(guc);
	guc->interrupts.disable(guc);

	/* Flush IRQ handler */
	spin_lock_irq(&guc_to_gt(guc)->irq_lock);
	spin_unlock_irq(&guc_to_gt(guc)->irq_lock);

	guc_flush_submissions(guc);

	/*
	 * Handle any outstanding G2Hs before reset. Call IRQ handler directly
	 * each pass as interrupt have been disabled. We always scrub for
	 * outstanding G2H as it is possible for outstanding_submission_g2h to
	 * be incremented after the context state update.
 	 */
	for (i = 0; i < 4 && atomic_read(&guc->outstanding_submission_g2h); ++i) {
		intel_guc_to_host_event_handler(guc);
#define wait_for_reset(guc, wait_var) \
		intel_guc_wait_for_pending_msg(guc, wait_var, false, (HZ / 20))
		do {
			wait_for_reset(guc, &guc->outstanding_submission_g2h);
		} while (!list_empty(&guc->ct.requests.incoming));
	}
	scrub_guc_desc_for_outstanding_g2h(guc);
}

static struct intel_engine_cs *
guc_virtual_get_sibling(struct intel_engine_cs *ve, unsigned int sibling)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp, mask = ve->mask;
	unsigned int num_siblings = 0;

	for_each_engine_masked(engine, ve->gt, mask, tmp)
		if (num_siblings++ == sibling)
			return engine;

	return NULL;
}

static inline struct intel_engine_cs *
__context_to_physical_engine(struct intel_context *ce)
{
	struct intel_engine_cs *engine = ce->engine;

	if (intel_engine_is_virtual(engine))
		engine = guc_virtual_get_sibling(engine, 0);

	return engine;
}

static void guc_reset_state(struct intel_context *ce, u32 head, bool scrub)
{
	struct intel_engine_cs *engine = __context_to_physical_engine(ce);

	if (intel_context_is_banned(ce))
		return;

	GEM_BUG_ON(!intel_context_is_pinned(ce));

	/*
	 * We want a simple context + ring to execute the breadcrumb update.
	 * We cannot rely on the context being intact across the GPU hang,
	 * so clear it and rebuild just what we need for the breadcrumb.
	 * All pending requests for this context will be zapped, and any
	 * future request will be after userspace has had the opportunity
	 * to recreate its own state.
	 */
	if (scrub)
		lrc_init_regs(ce, engine, true);

	/* Rerun the request; its payload has been neutered (if guilty). */
	lrc_update_regs(ce, engine, head);
}

static void guc_reset_nop(struct intel_engine_cs *engine)
{
}

static void guc_rewind_nop(struct intel_engine_cs *engine, bool stalled)
{
}

static void
__unwind_incomplete_requests(struct intel_context *ce)
{
	struct i915_request *rq, *rn;
	struct list_head *pl;
	int prio = I915_PRIORITY_INVALID;
	struct i915_sched_engine * const sched_engine =
		ce->engine->sched_engine;
	unsigned long flags;

	spin_lock_irqsave(&sched_engine->lock, flags);
	spin_lock(&ce->guc_active.lock);
	list_for_each_entry_safe(rq, rn,
				 &ce->guc_active.requests,
				 sched.link) {
		if (i915_request_completed(rq))
			continue;

		list_del_init(&rq->sched.link);
		spin_unlock(&ce->guc_active.lock);

		__i915_request_unsubmit(rq);

		/* Push the request back into the queue for later resubmission. */
		GEM_BUG_ON(rq_prio(rq) == I915_PRIORITY_INVALID);
		if (rq_prio(rq) != prio) {
			prio = rq_prio(rq);
			pl = i915_sched_lookup_priolist(sched_engine, prio);
		}
		GEM_BUG_ON(i915_sched_engine_is_empty(sched_engine));

		list_add_tail(&rq->sched.link, pl);
		set_bit(I915_FENCE_FLAG_PQUEUE, &rq->fence.flags);

		spin_lock(&ce->guc_active.lock);
	}
	spin_unlock(&ce->guc_active.lock);
	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void __guc_reset_context(struct intel_context *ce, bool stalled)
{
	struct i915_request *rq;
	u32 head;

	intel_context_get(ce);

	/*
	 * GuC will implicitly mark the context as non-schedulable
	 * when it sends the reset notification. Make sure our state
	 * reflects this change. The context will be marked enabled
	 * on resubmission.
	 */
	clr_context_enabled(ce);

	rq = intel_context_find_active_request(ce);
	if (!rq) {
		head = ce->ring->tail;
		stalled = false;
		goto out_replay;
	}

	if (!i915_request_started(rq))
		stalled = false;

	GEM_BUG_ON(i915_active_is_idle(&ce->active));
	head = intel_ring_wrap(ce->ring, rq->head);
	__i915_request_reset(rq, stalled);

out_replay:
	guc_reset_state(ce, head, stalled);
	__unwind_incomplete_requests(ce);
	ce->guc_num_rq_submit_no_id = 0;
	intel_context_put(ce);
}

void intel_guc_submission_reset(struct intel_guc *guc, bool stalled)
{
	struct intel_context *ce;
	unsigned long index;

	if (unlikely(!guc_submission_initialized(guc)))
		/* Reset called during driver load? GuC not yet initialised! */
		return;

	xa_for_each(&guc->context_lookup, index, ce)
		if (intel_context_is_pinned(ce))
			__guc_reset_context(ce, stalled);

	xa_destroy(&guc->context_lookup);
}

static void guc_cancel_context_requests(struct intel_context *ce)
{
	struct i915_sched_engine *sched_engine = ce_to_sched_engine(ce);
	struct i915_request *rq;
	unsigned long flags;

	/* Mark all executing requests as skipped. */
	spin_lock_irqsave(&sched_engine->lock, flags);
	spin_lock(&ce->guc_active.lock);
	list_for_each_entry(rq, &ce->guc_active.requests, sched.link)
		i915_request_put(i915_request_mark_eio(rq));
	ce->guc_num_rq_submit_no_id = 0;
	spin_unlock(&ce->guc_active.lock);
	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void
gse_cancel_requests(struct guc_submit_engine *gse)
{
	struct i915_sched_engine *sched_engine = &gse->sched_engine;
	struct i915_request *rq, *rn;
	struct rb_node *rb;
	unsigned long flags;

	/* Can be called during boot if GuC fails to load */
	if (!sched_engine)
		return;

	/*
	 * Before we call engine->cancel_requests(), we should have exclusive
	 * access to the submission state. This is arranged for us by the
	 * caller disabling the interrupt generation, the tasklet and other
	 * threads that may then access the same state, giving us a free hand
	 * to reset state. However, we still need to let lockdep be aware that
	 * we know this state may be accessed in hardirq context, so we
	 * disable the irq around this manipulation and we want to keep
	 * the spinlock focused on its duties and not accidentally conflate
	 * coverage to the submission's irq state. (Similarly, although we
	 * shouldn't need to disable irq around the manipulation of the
	 * submission's irq state, we also wish to remind ourselves that
	 * it is irq state.)
	 */
	spin_lock_irqsave(&sched_engine->lock, flags);

	/* Flush the queued requests to the timeline list (for retiring). */
	while ((rb = rb_first_cached(&sched_engine->queue))) {
		struct i915_priolist *p = to_priolist(rb);

		priolist_for_each_request_consume(rq, rn, p) {
			struct intel_context *ce = rq->context;

			list_del_init(&rq->sched.link);

			__i915_request_submit(rq);

			i915_request_put(i915_request_mark_eio(rq));

			ce->guc_num_rq_submit_no_id = 0;
		}

		rb_erase_cached(&p->node, &sched_engine->queue);
		i915_priolist_free(p);
	}

	/* Remaining _unready_ requests will be nop'ed when submitted */

	sched_engine->queue_priority_hint = INT_MIN;
	sched_engine->queue = RB_ROOT_CACHED;

	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

void intel_guc_submission_cancel_requests(struct intel_guc *guc)
{
	struct intel_context *ce;
	unsigned long index;
	int i;

	xa_for_each(&guc->context_lookup, index, ce)
		if (intel_context_is_pinned(ce))
			guc_cancel_context_requests(ce);

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i)
		gse_cancel_requests(guc->gse[i]);

	/* GuC is blown away, drop all references to contexts */
	xa_destroy(&guc->context_lookup);
}

void intel_guc_submission_reset_finish(struct intel_guc *guc)
{
	/* Reset called during driver load or during wedge? */
	if (unlikely(!guc_submission_initialized(guc) ||
		     test_bit(I915_WEDGED, &guc_to_gt(guc)->reset.flags)))
		return;

	/*
	 * Technically possible for either of these values to be non-zero here,
	 * but very unlikely + harmless. Regardless let's add a warn so we can
	 * see in CI if this happens frequently / a precursor to taking down the
	 * machine.
	 */
	GEM_WARN_ON(atomic_read(&guc->outstanding_submission_g2h));
	atomic_set(&guc->outstanding_submission_g2h, 0);

	intel_guc_global_policies_update(guc);
	enable_submission(guc);
	intel_gt_unpark_heartbeats(guc_to_gt(guc));
}

static void retire_worker_sched_disable(struct guc_submit_engine *gse,
					struct intel_context *ce);

static void retire_worker_func(struct work_struct *w)
{
	struct guc_submit_engine *gse =
		container_of(w, struct guc_submit_engine, retire_worker);

	/*
	 * It is possible that another thread issues the schedule disable + that
	 * G2H completes moving the state machine further along to a point
	 * where nothing needs to be done here. Let's be paranoid and kick the
	 * tasklet in that case.
	 */
	if (gse->submission_stall_reason != STALL_SCHED_DISABLE &&
	    gse->submission_stall_reason != STALL_GUC_ID_WORKQUEUE) {
		kick_tasklet(gse);
		return;
	}

	if (gse->submission_stall_reason == STALL_SCHED_DISABLE) {
		GEM_BUG_ON(!gse->stalled_context);
		GEM_BUG_ON(context_guc_id_invalid(gse->stalled_context));

		retire_worker_sched_disable(gse, gse->stalled_context);
	}

	/*
	 * guc_id pressure, always try to release it regardless of state,
	 * albeit after possibly issuing a schedule disable as that is async
	 * operation.
	 */
	intel_gt_retire_requests(guc_to_gt(gse->sched_engine.private_data));

	if (gse->submission_stall_reason == STALL_GUC_ID_WORKQUEUE) {
		GEM_BUG_ON(gse->stalled_context);

		/* Hopefully guc_ids are now available, kick tasklet */
		gse->submission_stall_reason = STALL_GUC_ID_TASKLET;
		clr_tasklet_blocked(gse);

		kick_tasklet(gse);
	}
}

/*
 * Set up the memory resources to be shared with the GuC (via the GGTT)
 * at firmware loading time.
 */
int intel_guc_submission_init(struct intel_guc *guc)
{
	int ret;

	if (guc->lrc_desc_pool)
		return 0;

	ret = guc_lrc_desc_pool_create(guc);
	if (ret)
		return ret;
	/*
	 * Keep static analysers happy, let them know that we allocated the
	 * vma after testing that it didn't exist earlier.
	 */
	GEM_BUG_ON(!guc->lrc_desc_pool);

	xa_init_flags(&guc->context_lookup, XA_FLAGS_LOCK_IRQ);

	spin_lock_init(&guc->contexts_lock);
	INIT_LIST_HEAD(&guc->guc_id_list_no_ref);
	INIT_LIST_HEAD(&guc->guc_id_list_unpinned);
	ida_init(&guc->guc_ids);

	return 0;
}

void intel_guc_submission_fini(struct intel_guc *guc)
{
	int i;

	if (!guc->lrc_desc_pool)
		return;

	guc_lrc_desc_pool_destroy(guc);

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i) {
		struct i915_sched_engine *sched_engine =
			guc_to_sched_engine(guc, i);

		i915_sched_engine_put(sched_engine);
	}
}

static inline void queue_request(struct i915_sched_engine *sched_engine,
				 struct i915_request *rq,
				 int prio)
{
	bool empty = i915_sched_engine_is_empty(sched_engine);

	GEM_BUG_ON(!list_empty(&rq->sched.link));
	list_add_tail(&rq->sched.link,
		      i915_sched_lookup_priolist(sched_engine, prio));
	set_bit(I915_FENCE_FLAG_PQUEUE, &rq->fence.flags);

	if (empty)
		kick_tasklet(ce_to_gse(rq->context));
}

/* Macro to tweak heuristic, using a simple over 50% not ready for now */
#define TOO_MANY_GUC_IDS_NOT_READY(avail, consumed) \
	(consumed > avail / 2)
static bool too_many_guc_ids_not_ready(struct guc_submit_engine *gse,
				       struct intel_context *ce)
{
	u32 available_guc_ids, guc_ids_consumed;
	struct intel_guc *guc = gse->sched_engine.private_data;

	available_guc_ids = guc->num_guc_ids;
	guc_ids_consumed = atomic_read(&gse->num_guc_ids_not_ready);

	if (TOO_MANY_GUC_IDS_NOT_READY(available_guc_ids, guc_ids_consumed)) {
		set_and_update_guc_ids_exhausted(gse);
		return true;
	}

	return false;
}

static void incr_num_rq_not_ready(struct intel_context *ce)
{
	struct guc_submit_engine *gse = ce_to_gse(ce);

	if (!atomic_fetch_add(1, &ce->guc_num_rq_not_ready))
		atomic_inc(&gse->num_guc_ids_not_ready);
}

void intel_guc_decr_num_rq_not_ready(struct intel_context *ce)
{
	struct guc_submit_engine *gse = ce_to_gse(ce);

	if (atomic_fetch_add(-1, &ce->guc_num_rq_not_ready) == 1) {
		GEM_BUG_ON(!atomic_read(&gse->num_guc_ids_not_ready));
		atomic_dec(&gse->num_guc_ids_not_ready);
	}
}

static bool need_tasklet(struct guc_submit_engine *gse, struct intel_context *ce)
{
	struct i915_sched_engine * const sched_engine = &gse->sched_engine;
	struct intel_guc *guc = gse->sched_engine.private_data;

	lockdep_assert_held(&sched_engine->lock);

	return guc_ids_exhausted(gse) || submission_disabled(guc) ||
		gse->stalled_rq || gse->stalled_context ||
		!lrc_desc_registered(guc, ce->guc_id) ||
		!i915_sched_engine_is_empty(sched_engine);
}

static int gse_bypass_tasklet_submit(struct guc_submit_engine *gse,
				     struct i915_request *rq)
{
	int ret;

	__i915_request_submit(rq);

	trace_i915_request_in(rq, 0);

	guc_set_lrc_tail(rq);
	ret = gse_add_request(gse, rq);

	if (unlikely(ret == -EPIPE))
		disable_submission(gse->sched_engine.private_data);

	return ret;
}

static void guc_submit_request(struct i915_request *rq)
{
	struct guc_submit_engine *gse = ce_to_gse(rq->context);
	struct i915_sched_engine *sched_engine = &gse->sched_engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&sched_engine->lock, flags);

	if (need_tasklet(gse, rq->context))
		queue_request(sched_engine, rq, rq_prio(rq));
	else if (gse_bypass_tasklet_submit(gse, rq) == -EBUSY)
		kick_tasklet(gse);

	spin_unlock_irqrestore(&sched_engine->lock, flags);

	intel_guc_decr_num_rq_not_ready(rq->context);
}

static int new_guc_id(struct intel_guc *guc)
{
	return ida_simple_get(&guc->guc_ids, 0,
			      guc->num_guc_ids, GFP_KERNEL |
			      __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
}

static void __release_guc_id(struct intel_guc *guc, struct intel_context *ce)
{
	if (!context_guc_id_invalid(ce)) {
		ida_simple_remove(&guc->guc_ids, ce->guc_id);
		reset_lrc_desc(guc, ce->guc_id);
		set_context_guc_id_invalid(ce);
	}
	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);
}

static void release_guc_id(struct intel_guc *guc, struct intel_context *ce)
{
	unsigned long flags;

	spin_lock_irqsave(&guc->contexts_lock, flags);
	__release_guc_id(guc, ce);
	spin_unlock_irqrestore(&guc->contexts_lock, flags);
}

/*
 * We have two lists for guc_ids available to steal. One list is for contexts
 * that to have a zero guc_id_ref but are still pinned (scheduling enabled, only
 * available inside tasklet) and the other is for contexts that are not pinned
 * but still registered (available both outside and inside tasklet). Stealing
 * from the latter only requires a deregister H2G, while the former requires a
 * schedule disable H2G + a deregister H2G.
 */
static struct list_head *get_guc_id_list(struct intel_guc *guc,
					 bool unpinned)
{
	if (unpinned)
		return &guc->guc_id_list_unpinned;
	else
		return &guc->guc_id_list_no_ref;
}

static int steal_guc_id(struct intel_guc *guc, bool unpinned)
{
	struct intel_context *ce;
	int guc_id;
	struct list_head *guc_id_list = get_guc_id_list(guc, unpinned);

	lockdep_assert_held(&guc->contexts_lock);

	if (!list_empty(guc_id_list)) {
		ce = list_first_entry(guc_id_list,
				      struct intel_context,
				      guc_id_link);

		/* Ensure context getting stolen in expected state */
		GEM_BUG_ON(atomic_read(&ce->guc_id_ref));
		GEM_BUG_ON(context_guc_id_invalid(ce));
		GEM_BUG_ON(context_guc_id_stolen(ce));

		list_del_init(&ce->guc_id_link);
		guc_id = ce->guc_id;
		clr_context_registered(ce);

		/*
		 * If stealing from the pinned list, defer invalidating
		 * the guc_id until the retire workqueue processes this
		 * context.
		 */
		if (!unpinned) {
			GEM_BUG_ON(ce_to_gse(ce)->stalled_context);

			ce_to_gse(ce)->stalled_context = intel_context_get(ce);
			set_context_guc_id_stolen(ce);
		} else {
			set_context_guc_id_invalid(ce);
		}

		return guc_id;
	} else {
		return -EAGAIN;
	}
}

enum {	/* Return values for pin_guc_id / assign_guc_id */
	SAME_GUC_ID		=0,
	NEW_GUC_ID_DISABLED	=1,
	NEW_GUC_ID_ENABLED	=2,
};

static int assign_guc_id(struct intel_guc *guc, u16 *out, bool tasklet)
{
	int ret;

	lockdep_assert_held(&guc->contexts_lock);

	ret = new_guc_id(guc);
	if (unlikely(ret < 0)) {
		ret = steal_guc_id(guc, true);
		if (ret >= 0) {
			*out = ret;
			ret = NEW_GUC_ID_DISABLED;
		} else if (ret < 0 && tasklet) {
			/*
			 * We only steal a guc_id from a context with scheduling
			 * enabled if guc_ids are exhausted and we are submitting
			 * from the tasklet.
			 */
			ret = steal_guc_id(guc, false);
			if (ret >= 0) {
				*out = ret;
				ret = NEW_GUC_ID_ENABLED;
			}
		}
	} else {
		*out = ret;
		ret = SAME_GUC_ID;
	}

	return ret;
}

#define PIN_GUC_ID_TRIES	4
static int pin_guc_id(struct intel_guc *guc, struct intel_context *ce,
		      bool tasklet)
{
	int ret = 0;
	unsigned long flags, tries = PIN_GUC_ID_TRIES;

	GEM_BUG_ON(atomic_read(&ce->guc_id_ref));

try_again:
	spin_lock_irqsave(&guc->contexts_lock, flags);

	if (!tasklet && guc_ids_exhausted(ce_to_gse(ce))) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	if (context_guc_id_invalid(ce)) {
		ret = assign_guc_id(guc, &ce->guc_id, tasklet);
		if (unlikely(ret < 0))
			goto out_unlock;
	}
	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);
	atomic_inc(&ce->guc_id_ref);

out_unlock:
	spin_unlock_irqrestore(&guc->contexts_lock, flags);

	/*
	 * -EAGAIN indicates no guc_ids are available, let's retire any
	 * outstanding requests to see if that frees up a guc_id. If the first
	 * retire didn't help, insert a sleep with the timeslice duration before
	 * attempting to retire more requests. Double the sleep period each
	 * subsequent pass before finally giving up. The sleep period has max of
	 * 100ms and minimum of 1ms.
	 *
	 * We only try this if outside the tasklet, inside the tasklet we have a
	 * (slower, more complex, blocking) different flow control algorithm.
	 */
	if (ret == -EAGAIN && --tries && !tasklet) {
		if (PIN_GUC_ID_TRIES - tries > 1) {
			unsigned int timeslice_shifted =
				ce->engine->props.timeslice_duration_ms <<
				(PIN_GUC_ID_TRIES - tries - 2);
			unsigned int max = min_t(unsigned int, 100,
						 timeslice_shifted);

			msleep(max_t(unsigned int, max, 1));
		}
		intel_gt_retire_requests(guc_to_gt(guc));
		goto try_again;
	}

	return ret;
}

static void unpin_guc_id(struct intel_guc *guc,
			 struct intel_context *ce,
			 bool unpinned)
{
	unsigned long flags;

	GEM_BUG_ON(atomic_read(&ce->guc_id_ref) < 0);

	spin_lock_irqsave(&guc->contexts_lock, flags);

	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);

	if (!context_guc_id_invalid(ce) && !context_guc_id_stolen(ce) &&
	    !atomic_read(&ce->guc_id_ref)) {
		struct list_head *head = get_guc_id_list(guc, unpinned);

		list_add_tail(&ce->guc_id_link, head);
	}

	spin_unlock_irqrestore(&guc->contexts_lock, flags);
}

static int __guc_action_register_context(struct intel_guc *guc,
					 u32 guc_id,
					 u32 offset,
					 bool loop)
{
	u32 action[] = {
		INTEL_GUC_ACTION_REGISTER_CONTEXT,
		guc_id,
		offset,
	};

	return guc_submission_send_busy_loop(guc, action, ARRAY_SIZE(action),
					     0, loop);
}

static int register_context(struct intel_context *ce, bool loop)
{
	struct intel_guc *guc = ce_to_guc(ce);
	u32 offset = intel_guc_ggtt_offset(guc, guc->lrc_desc_pool) +
		ce->guc_id * sizeof(struct guc_lrc_desc);
	int ret;

	trace_intel_context_register(ce);

	ret = __guc_action_register_context(guc, ce->guc_id, offset, loop);
	set_context_registered(ce);
	return ret;
}

static int __guc_action_deregister_context(struct intel_guc *guc,
					   u32 guc_id,
					   bool loop)
{
	u32 action[] = {
		INTEL_GUC_ACTION_DEREGISTER_CONTEXT,
		guc_id,
	};

	return guc_submission_send_busy_loop(guc, action, ARRAY_SIZE(action),
					     G2H_LEN_DW_DEREGISTER_CONTEXT,
					     loop);
}

static int deregister_context(struct intel_context *ce, u32 guc_id, bool loop)
{
	struct intel_guc *guc = ce_to_guc(ce);

	trace_intel_context_deregister(ce);

	return __guc_action_deregister_context(guc, guc_id, loop);
}

static intel_engine_mask_t adjust_engine_mask(u8 class, intel_engine_mask_t mask)
{
	switch (class) {
	case RENDER_CLASS:
		return mask >> RCS0;
	case VIDEO_ENHANCEMENT_CLASS:
		return mask >> VECS0;
	case VIDEO_DECODE_CLASS:
		return mask >> VCS0;
	case COPY_ENGINE_CLASS:
		return mask >> BCS0;
	default:
		MISSING_CASE(class);
		return 0;
	}
}

static void guc_context_policy_init(struct intel_engine_cs *engine,
				    struct guc_lrc_desc *desc)
{
	desc->policy_flags = 0;

	if (engine->flags & I915_ENGINE_WANT_FORCED_PREEMPTION)
		desc->policy_flags |= CONTEXT_POLICY_FLAG_PREEMPT_TO_IDLE;

	/* NB: For both of these, zero means disabled. */
	desc->execution_quantum = engine->props.timeslice_duration_ms * 1000;
	desc->preemption_timeout = engine->props.preempt_timeout_ms * 1000;
}

static inline u8 map_i915_prio_to_guc_prio(int prio);

static int guc_lrc_desc_pin(struct intel_context *ce, bool loop)
{
	struct intel_engine_cs *engine = ce->engine;
	struct intel_runtime_pm *runtime_pm = engine->uncore->rpm;
	struct intel_guc *guc = &engine->gt->uc.guc;
	u32 desc_idx = ce->guc_id;
	struct guc_lrc_desc *desc;
	const struct i915_gem_context *ctx;
	int prio = I915_CONTEXT_DEFAULT_PRIORITY;
	bool context_registered;
	intel_wakeref_t wakeref;
	int ret = 0;

	GEM_BUG_ON(!engine->mask);
	GEM_BUG_ON(context_guc_id_invalid(ce));

	/*
	 * Ensure LRC + CT vmas are is same region as write barrier is done
	 * based on CT vma region.
	 */
	GEM_BUG_ON(i915_gem_object_is_lmem(guc->ct.vma->obj) !=
		   i915_gem_object_is_lmem(ce->ring->vma->obj));

	context_registered = lrc_desc_registered(guc, desc_idx);

	rcu_read_lock();
	ctx = rcu_dereference(ce->gem_context);
	if (ctx)
		prio = ctx->sched.priority;
	rcu_read_unlock();

	reset_lrc_desc(guc, desc_idx);
	set_lrc_desc_registered(guc, desc_idx, ce);

	desc = __get_lrc_desc(guc, desc_idx);
	desc->engine_class = engine_class_to_guc_class(engine->class);
	desc->engine_submit_mask = adjust_engine_mask(engine->class,
						      engine->mask);
	desc->hw_context_desc = ce->lrc.lrca;
	ce->guc_prio = map_i915_prio_to_guc_prio(prio);
	desc->priority = ce->guc_prio;
	desc->context_flags = CONTEXT_REGISTRATION_FLAG_KMD;
	guc_context_policy_init(engine, desc);
	init_sched_state(ce);

	/*
	 * The context_lookup xarray is used to determine if the hardware
	 * context is currently registered. There are two cases in which it
	 * could be registered either the guc_id has been stolen from from
	 * another context or the lrc descriptor address of this context has
	 * changed. In either case the context needs to be deregistered with the
	 * GuC before registering this context.
	 */
	if (context_registered) {
		trace_intel_context_steal_guc_id(ce);
		if (!loop) {
			set_context_wait_for_deregister_to_register(ce);
			set_context_block_tasklet(ce);
			intel_context_get(ce);
		} else {
			bool disabled;
			unsigned long flags;

			/* Seal race with Reset */
			spin_lock_irqsave(&ce->guc_state.lock, flags);
			disabled = submission_disabled(guc);
			if (likely(!disabled)) {
				set_context_wait_for_deregister_to_register(ce);
				intel_context_get(ce);
			}
			spin_unlock_irqrestore(&ce->guc_state.lock, flags);
			if (unlikely(disabled)) {
				reset_lrc_desc(guc, desc_idx);
				return 0;	/* Will get registered later */
			}
		}

		/*
		 * If stealing the guc_id, this ce has the same guc_id as the
		 * context whose guc_id was stolen.
		 */
		with_intel_runtime_pm(runtime_pm, wakeref)
			ret = deregister_context(ce, ce->guc_id, loop);
		if (unlikely(ret == -EBUSY)) {
			clr_context_wait_for_deregister_to_register(ce);
			clr_context_block_tasklet(ce);
			intel_context_put(ce);
		} else if (!loop && !ret) {
			/*
			 * A context de-registration has been issued from within
			 * the tasklet. Need to block until it complete.
			 */
			return -EINPROGRESS;
		} else if (unlikely(ret == -ENODEV))
			ret = 0;	/* Will get registered later */
	} else {
		with_intel_runtime_pm(runtime_pm, wakeref)
			ret = register_context(ce, loop);
		if (unlikely(ret == -EBUSY))
			reset_lrc_desc(guc, desc_idx);
		else if (unlikely(ret == -ENODEV))
			ret = 0;	/* Will get registered later */
	}

	return ret;
}

static int __guc_context_pre_pin(struct intel_context *ce,
				 struct intel_engine_cs *engine,
				 struct i915_gem_ww_ctx *ww,
				 void **vaddr)
{
	return lrc_pre_pin(ce, engine, ww, vaddr);
}

static int __guc_context_pin(struct intel_context *ce,
			     struct intel_engine_cs *engine,
			     void *vaddr)
{
	if (i915_ggtt_offset(ce->state) !=
	    (ce->lrc.lrca & CTX_GTT_ADDRESS_MASK))
		set_bit(CONTEXT_LRCA_DIRTY, &ce->flags);

	/*
	 * GuC context gets pinned in guc_request_alloc. See that function for
	 * explaination of why.
	 */

	return lrc_pin(ce, engine, vaddr);
}

static int guc_context_pre_pin(struct intel_context *ce,
			       struct i915_gem_ww_ctx *ww,
			       void **vaddr)
{
	return __guc_context_pre_pin(ce, ce->engine, ww, vaddr);
}

static int guc_context_pin(struct intel_context *ce, void *vaddr)
{
	return __guc_context_pin(ce, ce->engine, vaddr);
}

static void guc_context_unpin(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);

	GEM_BUG_ON(context_enabled(ce));

	unpin_guc_id(guc, ce, true);
	lrc_unpin(ce);
}

static void guc_context_post_unpin(struct intel_context *ce)
{
	lrc_post_unpin(ce);
}

static void __guc_context_sched_enable(struct intel_guc *guc,
				       struct intel_context *ce)
{
	u32 action[] = {
		INTEL_GUC_ACTION_SCHED_CONTEXT_MODE_SET,
		ce->guc_id,
		GUC_CONTEXT_ENABLE
	};

	trace_intel_context_sched_enable(ce);

	guc_submission_send_busy_loop(guc, action, ARRAY_SIZE(action),
				      G2H_LEN_DW_SCHED_CONTEXT_MODE_SET, true);
}

static void __guc_context_sched_disable(struct intel_guc *guc,
					struct intel_context *ce,
					u16 guc_id)
{
	u32 action[] = {
		INTEL_GUC_ACTION_SCHED_CONTEXT_MODE_SET,
		guc_id,	/* ce->guc_id not stable */
		GUC_CONTEXT_DISABLE
	};

	GEM_BUG_ON(guc_id == GUC_INVALID_LRC_ID);

	trace_intel_context_sched_disable(ce);

	guc_submission_send_busy_loop(guc, action, ARRAY_SIZE(action),
				      G2H_LEN_DW_SCHED_CONTEXT_MODE_SET, true);
}

static void guc_blocked_fence_complete(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);

	if (!i915_sw_fence_done(&ce->guc_blocked))
		i915_sw_fence_complete(&ce->guc_blocked);
}

static void guc_blocked_fence_reinit(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);
	GEM_BUG_ON(!i915_sw_fence_done(&ce->guc_blocked));
	i915_sw_fence_fini(&ce->guc_blocked);
	i915_sw_fence_reinit(&ce->guc_blocked);
	i915_sw_fence_await(&ce->guc_blocked);
	i915_sw_fence_commit(&ce->guc_blocked);
}

static u16 prep_context_pending_disable(struct intel_context *ce)
{
	lockdep_assert_held(&ce->guc_state.lock);

	set_context_pending_disable(ce);
	clr_context_enabled(ce);
	guc_blocked_fence_reinit(ce);
	intel_context_get(ce);

	return ce->guc_id;
}

static struct i915_sw_fence *guc_context_block(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);
	struct i915_sched_engine *sched_engine = ce->engine->sched_engine;
	unsigned long flags;
	struct intel_runtime_pm *runtime_pm = &ce->engine->gt->i915->runtime_pm;
	intel_wakeref_t wakeref;
	u16 guc_id;
	bool enabled;

	spin_lock_irqsave(&sched_engine->lock, flags);
	incr_context_blocked(ce);
	spin_unlock_irqrestore(&sched_engine->lock, flags);

	spin_lock_irqsave(&ce->guc_state.lock, flags);
	enabled = context_enabled(ce);
	if (unlikely(!enabled || submission_disabled(guc))) {
		if (enabled)
			clr_context_enabled(ce);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);
		return &ce->guc_blocked;
	}

	/*
	 * We add +2 here as the schedule disable complete CTB handler calls
	 * intel_context_sched_disable_unpin (-2 to pin_count).
	 */
	atomic_add(2, &ce->pin_count);

	guc_id = prep_context_pending_disable(ce);
	spin_unlock_irqrestore(&ce->guc_state.lock, flags);

	with_intel_runtime_pm(runtime_pm, wakeref)
		__guc_context_sched_disable(guc, ce, guc_id);

	return &ce->guc_blocked;
}

static void guc_context_unblock(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);
	struct i915_sched_engine *sched_engine = ce->engine->sched_engine;
	unsigned long flags;
	struct intel_runtime_pm *runtime_pm = &ce->engine->gt->i915->runtime_pm;
	intel_wakeref_t wakeref;

	GEM_BUG_ON(context_enabled(ce));

	if (unlikely(context_blocked(ce) > 1)) {
		spin_lock_irqsave(&sched_engine->lock, flags);
		if (likely(context_blocked(ce) > 1))
			goto decrement;
		spin_unlock_irqrestore(&sched_engine->lock, flags);
	}

	spin_lock_irqsave(&ce->guc_state.lock, flags);
	if (unlikely(submission_disabled(guc) ||
		     !intel_context_is_pinned(ce) ||
		     context_pending_disable(ce) ||
		     context_blocked(ce) > 1)) {
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);
		goto out;
	}

	set_context_pending_enable(ce);
	set_context_enabled(ce);
	intel_context_get(ce);
	spin_unlock_irqrestore(&ce->guc_state.lock, flags);

	with_intel_runtime_pm(runtime_pm, wakeref)
		__guc_context_sched_enable(guc, ce);

out:
	spin_lock_irqsave(&sched_engine->lock, flags);
decrement:
	decr_context_blocked(ce);
	spin_unlock_irqrestore(&sched_engine->lock, flags);
}

static void guc_context_cancel_request(struct intel_context *ce,
				       struct i915_request *rq)
{
	if (i915_sw_fence_signaled(&rq->submit)) {
		struct i915_sw_fence *fence = guc_context_block(ce);

		i915_sw_fence_wait(fence);
		if (!i915_request_completed(rq)) {
			__i915_request_skip(rq);
			guc_reset_state(ce, intel_ring_wrap(ce->ring, rq->head),
					true);
		}
		guc_context_unblock(ce);
	}
}

static void __guc_context_set_preemption_timeout(struct intel_guc *guc,
						 u16 guc_id,
						 u32 preemption_timeout)
{
	u32 action [] = {
		INTEL_GUC_ACTION_SET_CONTEXT_PREEMPTION_TIMEOUT,
		guc_id,
		preemption_timeout
	};

	intel_guc_send_busy_loop(guc, action, ARRAY_SIZE(action), 0, true);
}

static void guc_context_ban(struct intel_context *ce, struct i915_request *rq)
{
	struct intel_guc *guc = ce_to_guc(ce);
	struct intel_runtime_pm *runtime_pm =
		&ce->engine->gt->i915->runtime_pm;
	intel_wakeref_t wakeref;
	unsigned long flags;

	gse_flush_submissions(ce_to_gse(ce));

	spin_lock_irqsave(&ce->guc_state.lock, flags);
	set_context_banned(ce);

	if (submission_disabled(guc) || (!context_enabled(ce) &&
	    !context_pending_disable(ce))) {
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		guc_cancel_context_requests(ce);
		intel_engine_signal_breadcrumbs(ce->engine);
	} else if (!context_pending_disable(ce)) {
		u16 guc_id;

		/*
		 * We add +2 here as the schedule disable complete CTB handler
		 * calls intel_context_sched_disable_unpin (-2 to pin_count).
		 */
		atomic_add(2, &ce->pin_count);

		guc_id = prep_context_pending_disable(ce);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		/*
		 * In addition to disabling scheduling, set the preemption
		 * timeout to the minimum value (1 us) so the banned context
		 * gets kicked off the HW ASAP.
		 */
		with_intel_runtime_pm(runtime_pm, wakeref) {
			__guc_context_set_preemption_timeout(guc, guc_id, 1);
			__guc_context_sched_disable(guc, ce, guc_id);
		}
	} else {
		if (!context_guc_id_invalid(ce))
			with_intel_runtime_pm(runtime_pm, wakeref)
				__guc_context_set_preemption_timeout(guc,
								     ce->guc_id,
								     1);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);
	}
}

static void guc_context_sched_disable(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);
	unsigned long flags;
	struct intel_runtime_pm *runtime_pm = &ce->engine->gt->i915->runtime_pm;
	intel_wakeref_t wakeref;
	u16 guc_id;
	bool enabled;

	if (submission_disabled(guc) || context_guc_id_invalid(ce) ||
	    !lrc_desc_registered(guc, ce->guc_id)) {
		clr_context_enabled(ce);
		goto unpin;
	}

	if (!context_enabled(ce))
		goto unpin;

	spin_lock_irqsave(&ce->guc_state.lock, flags);

	/*
	 * We have to check if the context has been disabled by another thread.
	 * We also have to check if the context has been pinned again as another
	 * pin operation is allowed to pass this function. Checking the pin
	 * count, within ce->guc_state.lock, synchronizes this function with
	 * guc_request_alloc ensuring a request doesn't slip through the
	 * 'context_pending_disable' fence. Checking within the spin lock (can't
	 * sleep) ensures another process doesn't pin this context and generate
	 * a request before we set the 'context_pending_disable' flag here.
	 */
	enabled = context_enabled(ce);
	if (unlikely(!enabled || submission_disabled(guc))) {
		if (enabled)
			clr_context_enabled(ce);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);
		goto unpin;
	}
	if (unlikely(atomic_add_unless(&ce->pin_count, -2, 2))) {
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);
		return;
	}
	guc_id = prep_context_pending_disable(ce);

	spin_unlock_irqrestore(&ce->guc_state.lock, flags);

	with_intel_runtime_pm(runtime_pm, wakeref)
		__guc_context_sched_disable(ce_to_guc(ce), ce, guc_id);

	return;
unpin:
	intel_context_sched_disable_unpin(ce);
}

static inline void guc_lrc_desc_unpin(struct intel_context *ce)
{
	struct intel_guc *guc = ce_to_guc(ce);

	GEM_BUG_ON(!lrc_desc_registered(guc, ce->guc_id));
	GEM_BUG_ON(ce != __get_context(guc, ce->guc_id));
	GEM_BUG_ON(context_enabled(ce));

	clr_context_registered(ce);
	deregister_context(ce, ce->guc_id, true);
}

static void __guc_context_destroy(struct intel_context *ce)
{
	GEM_BUG_ON(ce->guc_prio_count[GUC_CLIENT_PRIORITY_KMD_HIGH] ||
		   ce->guc_prio_count[GUC_CLIENT_PRIORITY_HIGH] ||
		   ce->guc_prio_count[GUC_CLIENT_PRIORITY_KMD_NORMAL] ||
		   ce->guc_prio_count[GUC_CLIENT_PRIORITY_NORMAL]);

	lrc_fini(ce);
	intel_context_fini(ce);

	if (intel_engine_is_virtual(ce->engine)) {
		struct guc_virtual_engine *ve =
			container_of(ce, typeof(*ve), context);

		if (ve->base.breadcrumbs)
			intel_breadcrumbs_put(ve->base.breadcrumbs);

		kfree(ve);
	} else {
		intel_context_free(ce);
	}
}

static void guc_context_destroy(struct kref *kref)
{
	struct intel_context *ce = container_of(kref, typeof(*ce), ref);
	struct intel_runtime_pm *runtime_pm = ce->engine->uncore->rpm;
	struct intel_guc *guc = ce_to_guc(ce);
	intel_wakeref_t wakeref;
	unsigned long flags;
	bool disabled;

	GEM_BUG_ON(context_guc_id_stolen(ce));

	/*
	 * If the guc_id is invalid this context has been stolen and we can free
	 * it immediately. Also can be freed immediately if the context is not
	 * registered with the GuC or the GuC is in the middle of a reset.
	 */
	if (context_guc_id_invalid(ce)) {
		__guc_context_destroy(ce);
		return;
	} else if (submission_disabled(guc) ||
		   !lrc_desc_registered(guc, ce->guc_id)) {
		release_guc_id(guc, ce);
		__guc_context_destroy(ce);
		return;
	}

	/*
	 * We have to acquire the context spinlock and check guc_id again, if it
	 * is valid it hasn't been stolen and needs to be deregistered. We
	 * delete this context from the list of unpinned guc_ids available to
	 * steal to seal a race with guc_lrc_desc_pin(). When the G2H CTB
	 * returns indicating this context has been deregistered the guc_id is
	 * returned to the pool of available guc_ids.
	 */
	spin_lock_irqsave(&guc->contexts_lock, flags);
	if (context_guc_id_invalid(ce)) {
		spin_unlock_irqrestore(&guc->contexts_lock, flags);
		__guc_context_destroy(ce);
		return;
	}

	if (!list_empty(&ce->guc_id_link))
		list_del_init(&ce->guc_id_link);
	spin_unlock_irqrestore(&guc->contexts_lock, flags);

	/* Seal race with Reset */
	spin_lock_irqsave(&ce->guc_state.lock, flags);
	disabled = submission_disabled(guc);
	if (likely(!disabled))
		set_context_destroyed(ce);
	spin_unlock_irqrestore(&ce->guc_state.lock, flags);
	if (unlikely(disabled)) {
		release_guc_id(guc, ce);
		__guc_context_destroy(ce);
		return;
	}

	/*
	 * We defer GuC context deregistration until the context is destroyed
	 * in order to save on CTBs. With this optimization ideally we only need
	 * 1 CTB to register the context during the first pin and 1 CTB to
	 * deregister the context when the context is destroyed. Without this
	 * optimization, a CTB would be needed every pin & unpin.
	 *
	 * XXX: Need to acqiure the runtime wakeref as this can be triggered
	 * from context_free_worker when runtime wakeref is not held.
	 * guc_lrc_desc_unpin requires the runtime as a GuC register is written
	 * in H2G CTB to deregister the context. A future patch may defer this
	 * H2G CTB if the runtime wakeref is zero.
	 */
	with_intel_runtime_pm(runtime_pm, wakeref)
		guc_lrc_desc_unpin(ce);
}

static int guc_context_alloc(struct intel_context *ce)
{
	return lrc_alloc(ce, ce->engine);
}

static void guc_context_set_prio(struct intel_guc *guc,
				 struct intel_context *ce,
				 u8 prio)
{
	u32 action[] = {
		INTEL_GUC_ACTION_SET_CONTEXT_PRIORITY,
		ce->guc_id,
		prio,
	};

	GEM_BUG_ON(prio < GUC_CLIENT_PRIORITY_KMD_HIGH ||
		   prio > GUC_CLIENT_PRIORITY_NORMAL);

	if (ce->guc_prio == prio || submission_disabled(guc) ||
	    !context_registered(ce))
		return;

	guc_submission_send_busy_loop(guc, action, ARRAY_SIZE(action), 0, true);

	ce->guc_prio = prio;
	trace_intel_context_set_prio(ce);
}

static inline u8 map_i915_prio_to_guc_prio(int prio)
{
	if (prio == I915_PRIORITY_NORMAL)
		return GUC_CLIENT_PRIORITY_KMD_NORMAL;
	else if (prio < I915_PRIORITY_NORMAL)
		return GUC_CLIENT_PRIORITY_NORMAL;
	else if (prio != I915_PRIORITY_BARRIER)
		return GUC_CLIENT_PRIORITY_HIGH;
	else
		return GUC_CLIENT_PRIORITY_KMD_HIGH;
}

static inline void add_context_inflight_prio(struct intel_context *ce,
					     u8 guc_prio)
{
	lockdep_assert_held(&ce->guc_active.lock);
	GEM_BUG_ON(guc_prio >= ARRAY_SIZE(ce->guc_prio_count));

	++ce->guc_prio_count[guc_prio];

	/* Overflow protection */
	GEM_WARN_ON(!ce->guc_prio_count[guc_prio]);
}

static inline void sub_context_inflight_prio(struct intel_context *ce,
					     u8 guc_prio)
{
	lockdep_assert_held(&ce->guc_active.lock);
	GEM_BUG_ON(guc_prio >= ARRAY_SIZE(ce->guc_prio_count));

	/* Underflow protection */
	GEM_WARN_ON(!ce->guc_prio_count[guc_prio]);

	--ce->guc_prio_count[guc_prio];
}

static inline void update_context_prio(struct intel_context *ce)
{
	struct intel_guc *guc = &ce->engine->gt->uc.guc;
	int i;

	lockdep_assert_held(&ce->guc_active.lock);

	for (i = 0; i < ARRAY_SIZE(ce->guc_prio_count); ++i) {
		if (ce->guc_prio_count[i]) {
			guc_context_set_prio(guc, ce, i);
			break;
		}
	}
}

static inline bool new_guc_prio_higher(u8 old_guc_prio, u8 new_guc_prio)
{
	/* Lower value is higher priority */
	return new_guc_prio < old_guc_prio;
}

static void add_to_context(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;
	u8 new_guc_prio = map_i915_prio_to_guc_prio(rq_prio(rq));

	GEM_BUG_ON(rq->guc_prio == GUC_PRIO_FINI);

	spin_lock(&ce->guc_active.lock);
	list_move_tail(&rq->sched.link, &ce->guc_active.requests);

	if (unlikely(request_has_no_guc_id(rq)))
		++ce->guc_num_rq_submit_no_id;

	if (rq->guc_prio == GUC_PRIO_INIT) {
		rq->guc_prio = new_guc_prio;
		add_context_inflight_prio(ce, rq->guc_prio);
	} else if (new_guc_prio_higher(rq->guc_prio, new_guc_prio)) {
		sub_context_inflight_prio(ce, rq->guc_prio);
		rq->guc_prio = new_guc_prio;
		add_context_inflight_prio(ce, rq->guc_prio);
	}
	update_context_prio(ce);

	spin_unlock(&ce->guc_active.lock);
}

static void guc_prio_fini(struct i915_request *rq, struct intel_context *ce)
{
	if (rq->guc_prio != GUC_PRIO_INIT &&
	    rq->guc_prio != GUC_PRIO_FINI) {
		sub_context_inflight_prio(ce, rq->guc_prio);
		update_context_prio(ce);
	}
	rq->guc_prio = GUC_PRIO_FINI;
}

static void remove_from_context(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;

	spin_lock_irq(&ce->guc_active.lock);

	list_del_init(&rq->sched.link);
	clear_bit(I915_FENCE_FLAG_PQUEUE, &rq->fence.flags);

	/* Prevent further __await_execution() registering a cb, then flush */
	set_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags);

	guc_prio_fini(rq, ce);

	spin_unlock_irq(&ce->guc_active.lock);

	if (likely(!request_has_no_guc_id(rq)))
		atomic_dec(&ce->guc_id_ref);
	else
		--ce_to_gse(rq->context)->total_num_rq_with_no_guc_id;
	unpin_guc_id(ce_to_guc(ce), ce, false);

	i915_request_notify_execute_cb_imm(rq);
}

static const struct intel_context_ops guc_context_ops = {
	.alloc = guc_context_alloc,

	.pre_pin = guc_context_pre_pin,
	.pin = guc_context_pin,
	.unpin = guc_context_unpin,
	.post_unpin = guc_context_post_unpin,

	.ban = guc_context_ban,

	.cancel_request = guc_context_cancel_request,

	.enter = intel_context_enter_engine,
	.exit = intel_context_exit_engine,

	.sched_disable = guc_context_sched_disable,

	.reset = lrc_reset,
	.destroy = guc_context_destroy,

	.create_virtual = guc_create_virtual,
};

static void __guc_signal_context_fence(struct intel_context *ce)
{
	struct i915_request *rq;

	lockdep_assert_held(&ce->guc_state.lock);

	if (!list_empty(&ce->guc_state.fences))
		trace_intel_context_fence_release(ce);

	list_for_each_entry(rq, &ce->guc_state.fences, guc_fence_link)
		i915_sw_fence_complete(&rq->submit);

	INIT_LIST_HEAD(&ce->guc_state.fences);
}

static void guc_signal_context_fence(struct intel_context *ce)
{
	unsigned long flags;

	spin_lock_irqsave(&ce->guc_state.lock, flags);
	clr_context_wait_for_deregister_to_register(ce);
	__guc_signal_context_fence(ce);
	spin_unlock_irqrestore(&ce->guc_state.lock, flags);
}

static void invalidate_guc_id_sched_disable(struct intel_context *ce)
{
	set_context_guc_id_invalid(ce);
	wmb();
	clr_context_guc_id_stolen(ce);
}

static void retire_worker_sched_disable(struct guc_submit_engine *gse,
					struct intel_context *ce)
{
	struct intel_guc *guc = gse->sched_engine.private_data;
	unsigned long flags;
	bool disabled;

	gse->stalled_context = NULL;
	spin_lock_irqsave(&ce->guc_state.lock, flags);
	disabled = submission_disabled(guc);
	if (!disabled && !context_pending_disable(ce) && context_enabled(ce)) {
		/*
		 * Still enabled, issue schedule disable + configure state so
		 * when G2H returns tasklet is kicked.
		 */

		struct intel_runtime_pm *runtime_pm =
			&ce->engine->gt->i915->runtime_pm;
		intel_wakeref_t wakeref;
		u16 guc_id;

		/*
		 * We add +2 here as the schedule disable complete CTB handler
		 * calls intel_context_sched_disable_unpin (-2 to pin_count).
		 */
		GEM_BUG_ON(!atomic_read(&ce->pin_count));
		atomic_add(2, &ce->pin_count);

		set_context_block_tasklet(ce);
		guc_id = prep_context_pending_disable(ce);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		with_intel_runtime_pm(runtime_pm, wakeref)
			__guc_context_sched_disable(guc, ce, guc_id);

		invalidate_guc_id_sched_disable(ce);
	} else if (!disabled && context_pending_disable(ce)) {
		/*
		 * Schedule disable in flight, set bit to kick tasklet in G2H
		 * handler and call it a day.
		 */

		set_context_block_tasklet(ce);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		invalidate_guc_id_sched_disable(ce);
	} else {
		/* Schedule disable is done, kick tasklet */

		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		invalidate_guc_id_sched_disable(ce);

		gse->submission_stall_reason = STALL_REGISTER_CONTEXT;
		clr_tasklet_blocked(gse);

		kick_tasklet(gse);
	}

	intel_context_put(ce);
}

static bool context_needs_lrc_desc_pin(struct intel_context *ce, bool new_guc_id)
{
	return (new_guc_id || test_bit(CONTEXT_LRCA_DIRTY, &ce->flags) ||
		!lrc_desc_registered(ce_to_guc(ce), ce->guc_id)) &&
		!submission_disabled(ce_to_guc(ce));
}

static int tasklet_pin_guc_id(struct guc_submit_engine *gse,
			      struct i915_request *rq)
{
	struct intel_context *ce = rq->context;
	int ret = 0;

	lockdep_assert_held(&gse->sched_engine.lock);
	GEM_BUG_ON(!ce->guc_num_rq_submit_no_id);

	if (atomic_add_unless(&ce->guc_id_ref, ce->guc_num_rq_submit_no_id, 0))
		goto out;

	ret = pin_guc_id(gse->sched_engine.private_data, ce, true);
	if (unlikely(ret < 0)) {
		/*
		 * No guc_ids available, disable the tasklet and kick the retire
		 * workqueue hopefully freeing up some guc_ids.
		 */
		gse->stalled_rq = rq;
		gse->submission_stall_reason = STALL_GUC_ID_WORKQUEUE;
		return ret;
	}

	if (ce->guc_num_rq_submit_no_id - 1 > 0)
		atomic_add(ce->guc_num_rq_submit_no_id - 1,
			   &ce->guc_id_ref);

	if (context_needs_lrc_desc_pin(ce, !!ret))
		set_context_needs_register(ce);

	if (ret == NEW_GUC_ID_ENABLED) {
		gse->stalled_rq = rq;
		gse->submission_stall_reason = STALL_SCHED_DISABLE;
	}

	clear_bit(CONTEXT_LRCA_DIRTY, &ce->flags);
out:
	gse->total_num_rq_with_no_guc_id -= ce->guc_num_rq_submit_no_id;
	GEM_BUG_ON(gse->total_num_rq_with_no_guc_id < 0);

	list_for_each_entry_reverse(rq, &ce->guc_active.requests, sched.link)
		if (request_has_no_guc_id(rq)) {
			--ce->guc_num_rq_submit_no_id;
			clear_bit(I915_FENCE_FLAG_GUC_ID_NOT_PINNED,
				  &rq->fence.flags);
		} else if (!ce->guc_num_rq_submit_no_id) {
			break;
		}

	GEM_BUG_ON(ce->guc_num_rq_submit_no_id);

	/*
	 * When NEW_GUC_ID_ENABLED is returned it means we are stealing a guc_id
	 * from a context that has scheduling enabled. We have to disable
	 * scheduling before deregistering the context and it isn't safe to do
	 * in the tasklet because of lock inversion (ce->guc_state.lock must be
	 * acquired before gse->sched_engine.lock). To work around this
	 * we do the schedule disable in retire workqueue and block the tasklet
	 * until the schedule done G2H returns. Returning non-zero here kicks
	 * the workqueue.
	 */
	return (ret == NEW_GUC_ID_ENABLED) ? ret : 0;
}

static int guc_request_alloc(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;
	struct intel_guc *guc = ce_to_guc(ce);
	struct guc_submit_engine *gse = ce_to_gse(ce);
	unsigned long flags;
	int ret;

	GEM_BUG_ON(!intel_context_is_pinned(rq->context));

	/*
	 * guc_ids are exhausted or a heuristic is met indicating too many
	 * guc_ids are waiting on requests with submission dependencies (not
	 * ready to submit). Don't allocate one here, defer to submission in the
	 * tasklet.
	 */
	if (test_and_update_guc_ids_exhausted(gse) ||
	    too_many_guc_ids_not_ready(gse, ce)) {
		set_bit(I915_FENCE_FLAG_GUC_ID_NOT_PINNED, &rq->fence.flags);
		goto out;
	}

	/*
	 * Flush enough space to reduce the likelihood of waiting after
	 * we start building the request - in which case we will just
	 * have to repeat work.
	 */
	rq->reserved_space += GUC_REQUEST_SIZE;

	/*
	 * Note that after this point, we have committed to using
	 * this request as it is being used to both track the
	 * state of engine initialisation and liveness of the
	 * golden renderstate above. Think twice before you try
	 * to cancel/unwind this request now.
	 */

	/* Unconditionally invalidate GPU caches and TLBs. */
	ret = rq->engine->emit_flush(rq, EMIT_INVALIDATE);
	if (ret)
		return ret;

	rq->reserved_space -= GUC_REQUEST_SIZE;

	/*
	 * Call pin_guc_id here rather than in the pinning step as with
	 * dma_resv, contexts can be repeatedly pinned / unpinned trashing the
	 * guc_ids and creating horrible race conditions. This is especially bad
	 * when guc_ids are being stolen due to over subscription. By the time
	 * this function is reached, it is guaranteed that the guc_id will be
	 * persistent until the generated request is retired. Thus, sealing these
	 * race conditions.
	 *
	 * There is no need for a lock here as the timeline mutex ensures at
	 * most one context can be executing this code path at once. The
	 * guc_id_ref is incremented once for every request in flight and
	 * decremented on each retire. When it is zero, a lock around the
	 * increment (in pin_guc_id) is needed to seal a race with unpin_guc_id.
	 */
	if (atomic_add_unless(&ce->guc_id_ref, 1, 0))
		goto out;

	ret = pin_guc_id(guc, ce, false);	/* > 0 indicates new guc_id */
	if (unlikely(ret == -EAGAIN)) {
		/*
		 * No guc_ids available, so we force this submission and all
		 * future submissions to be serialized in the tasklet, sharing
		 * the guc_ids on a per submission basis to ensure (more) fair
		 * scheduling of submissions. Once the tasklet is flushed of
		 * submissions we return to allocating guc_ids in this function.
		 */
		set_bit(I915_FENCE_FLAG_GUC_ID_NOT_PINNED, &rq->fence.flags);
		set_and_update_guc_ids_exhausted(gse);
		incr_num_rq_not_ready(ce);

		return 0;
	} else if (unlikely(ret < 0)) {
		return ret;
	}

	GEM_BUG_ON(ret == NEW_GUC_ID_ENABLED);

	if (context_needs_lrc_desc_pin(ce, !!ret)) {
		ret = guc_lrc_desc_pin(ce, true);
		if (unlikely(ret)) {	/* unwind */
			if (ret == -EPIPE) {
				disable_submission(guc);
				goto out;	/* GPU will be reset */
			}
			atomic_dec(&ce->guc_id_ref);
			unpin_guc_id(guc, ce, true);
			return ret;
		}
	}

	clear_bit(CONTEXT_LRCA_DIRTY, &ce->flags);

out:
	incr_num_rq_not_ready(ce);

	/*
	 * We block all requests on this context if a G2H is pending for a
	 * schedule disable or context deregistration as the GuC will fail a
	 * schedule enable or context registration if either G2H is pending
	 * respectfully. Once a G2H returns, the fence is released that is
	 * blocking these requests (see guc_signal_context_fence).
	 *
	 * We can safely check the below fields outside of the lock as it isn't
	 * possible for these fields to transition from being clear to set but
	 * converse is possible, hence the need for the check within the lock.
	 */
	if (likely(!context_wait_for_deregister_to_register(ce) &&
		   !context_pending_disable(ce)))
		return 0;

	spin_lock_irqsave(&ce->guc_state.lock, flags);
	if (context_wait_for_deregister_to_register(ce) ||
	    context_pending_disable(ce)) {
		i915_sw_fence_await(&rq->submit);

		list_add_tail(&rq->guc_fence_link, &ce->guc_state.fences);
	}
	spin_unlock_irqrestore(&ce->guc_state.lock, flags);

	return 0;
}

static int guc_virtual_context_pre_pin(struct intel_context *ce,
				       struct i915_gem_ww_ctx *ww,
				       void **vaddr)
{
	struct intel_engine_cs *engine = guc_virtual_get_sibling(ce->engine, 0);

	return __guc_context_pre_pin(ce, engine, ww, vaddr);
}

static int guc_virtual_context_pin(struct intel_context *ce, void *vaddr)
{
	struct intel_engine_cs *engine = guc_virtual_get_sibling(ce->engine, 0);

	return __guc_context_pin(ce, engine, vaddr);
}

static void guc_virtual_context_enter(struct intel_context *ce)
{
	intel_engine_mask_t tmp, mask = ce->engine->mask;
	struct intel_engine_cs *engine;

	for_each_engine_masked(engine, ce->engine->gt, mask, tmp)
		intel_engine_pm_get(engine);

	intel_timeline_enter(ce->timeline);
}

static void guc_virtual_context_exit(struct intel_context *ce)
{
	intel_engine_mask_t tmp, mask = ce->engine->mask;
	struct intel_engine_cs *engine;

	for_each_engine_masked(engine, ce->engine->gt, mask, tmp)
		intel_engine_pm_put(engine);

	intel_timeline_exit(ce->timeline);
}

static int guc_virtual_context_alloc(struct intel_context *ce)
{
	struct intel_engine_cs *engine = guc_virtual_get_sibling(ce->engine, 0);

	return lrc_alloc(ce, engine);
}

static const struct intel_context_ops virtual_guc_context_ops = {
	.alloc = guc_virtual_context_alloc,

	.pre_pin = guc_virtual_context_pre_pin,
	.pin = guc_virtual_context_pin,
	.unpin = guc_context_unpin,
	.post_unpin = guc_context_post_unpin,

	.ban = guc_context_ban,

	.cancel_request = guc_context_cancel_request,

	.enter = guc_virtual_context_enter,
	.exit = guc_virtual_context_exit,

	.sched_disable = guc_context_sched_disable,

	.destroy = guc_context_destroy,

	.get_sibling = guc_virtual_get_sibling,
};

static bool
guc_irq_enable_breadcrumbs(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *sibling;
	intel_engine_mask_t tmp, mask = b->engine_mask;
	bool result = false;

	for_each_engine_masked(sibling, b->irq_engine->gt, mask, tmp)
		result |= intel_engine_irq_enable(sibling);

	return result;
}

static void
guc_irq_disable_breadcrumbs(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *sibling;
	intel_engine_mask_t tmp, mask = b->engine_mask;

	for_each_engine_masked(sibling, b->irq_engine->gt, mask, tmp)
		intel_engine_irq_disable(sibling);
}

static void guc_init_breadcrumbs(struct intel_engine_cs *engine)
{
	int i;

       /*
        * In GuC submission mode we do not know which physical engine a request
        * will be scheduled on, this creates a problem because the breadcrumb
        * interrupt is per physical engine. To work around this we attach
        * requests and direct all breadcrumb interrupts to the first instance
        * of an engine per class. In addition all breadcrumb interrupts are
	* enabled / disabled across an engine class in unison.
        */
	for (i = 0; i < MAX_ENGINE_INSTANCE; ++i) {
		struct intel_engine_cs *sibling =
			engine->gt->engine_class[engine->class][i];

		if (sibling) {
			if (engine->breadcrumbs != sibling->breadcrumbs) {
				intel_breadcrumbs_put(engine->breadcrumbs);
				engine->breadcrumbs =
					intel_breadcrumbs_get(sibling->breadcrumbs);
			}
			break;
		}
	}

	if (engine->breadcrumbs) {
		engine->breadcrumbs->engine_mask |= engine->mask;
		engine->breadcrumbs->irq_enable = guc_irq_enable_breadcrumbs;
		engine->breadcrumbs->irq_disable = guc_irq_disable_breadcrumbs;
	}
}

static void guc_bump_inflight_request_prio(struct i915_request *rq,
					   int prio)
{
	struct intel_context *ce = rq->context;
	u8 new_guc_prio = map_i915_prio_to_guc_prio(prio);

	/* Short circuit function */
	if (prio < I915_PRIORITY_NORMAL ||
	    (rq->guc_prio == GUC_PRIO_FINI) ||
	    (rq->guc_prio != GUC_PRIO_INIT &&
	     !new_guc_prio_higher(rq->guc_prio, new_guc_prio)))
		return;

	spin_lock(&ce->guc_active.lock);
	if (rq->guc_prio != GUC_PRIO_FINI) {
		if (rq->guc_prio != GUC_PRIO_INIT)
			sub_context_inflight_prio(ce, rq->guc_prio);
		rq->guc_prio = new_guc_prio;
		add_context_inflight_prio(ce, rq->guc_prio);
		update_context_prio(ce);
	}
	spin_unlock(&ce->guc_active.lock);
}

static void guc_retire_inflight_request_prio(struct i915_request *rq)
{
	struct intel_context *ce = rq->context;

	spin_lock(&ce->guc_active.lock);
	guc_prio_fini(rq, ce);
	spin_unlock(&ce->guc_active.lock);
}

static void sanitize_hwsp(struct intel_engine_cs *engine)
{
	struct intel_timeline *tl;

	list_for_each_entry(tl, &engine->status_page.timelines, engine_link)
		intel_timeline_reset_seqno(tl);
}

static void guc_sanitize(struct intel_engine_cs *engine)
{
	/*
	 * Poison residual state on resume, in case the suspend didn't!
	 *
	 * We have to assume that across suspend/resume (or other loss
	 * of control) that the contents of our pinned buffers has been
	 * lost, replaced by garbage. Since this doesn't always happen,
	 * let's poison such state so that we more quickly spot when
	 * we falsely assume it has been preserved.
	 */
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		memset(engine->status_page.addr, POISON_INUSE, PAGE_SIZE);

	/*
	 * The kernel_context HWSP is stored in the status_page. As above,
	 * that may be lost on resume/initialisation, and so we need to
	 * reset the value in the HWSP.
	 */
	sanitize_hwsp(engine);

	/* And scrub the dirty cachelines for the HWSP */
	clflush_cache_range(engine->status_page.addr, PAGE_SIZE);
}

static void setup_hwsp(struct intel_engine_cs *engine)
{
	intel_engine_set_hwsp_writemask(engine, ~0u); /* HWSTAM */

	ENGINE_WRITE_FW(engine,
			RING_HWS_PGA,
			i915_ggtt_offset(engine->status_page.vma));
}

static void start_engine(struct intel_engine_cs *engine)
{
	ENGINE_WRITE_FW(engine,
			RING_MODE_GEN7,
			_MASKED_BIT_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE));

	ENGINE_WRITE_FW(engine, RING_MI_MODE, _MASKED_BIT_DISABLE(STOP_RING));
	ENGINE_POSTING_READ(engine, RING_MI_MODE);
}

static int guc_resume(struct intel_engine_cs *engine)
{
	assert_forcewakes_active(engine->uncore, FORCEWAKE_ALL);

	intel_mocs_init_engine(engine);

	intel_breadcrumbs_reset(engine->breadcrumbs);

	setup_hwsp(engine);
	start_engine(engine);

	return 0;
}

static bool guc_sched_engine_disabled(struct i915_sched_engine *sched_engine)
{
	return !sched_engine->tasklet.callback;
}

static void guc_set_default_submission(struct intel_engine_cs *engine)
{
	engine->submit_request = guc_submit_request;
}

static inline void guc_kernel_context_pin(struct intel_guc *guc,
					  struct intel_context *ce)
{
	if (context_guc_id_invalid(ce))
		pin_guc_id(guc, ce, false);
	guc_lrc_desc_pin(ce, true);
}

static inline void guc_init_lrc_mapping(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* make sure all descriptors are clean... */
	xa_destroy(&guc->context_lookup);

	/*
	 * Some contexts might have been pinned before we enabled GuC
	 * submission, so we need to add them to the GuC bookeeping.
	 * Also, after a reset the of GuC we want to make sure that the
	 * information shared with GuC is properly reset. The kernel LRCs are
	 * not attached to the gem_context, so they need to be added separately.
	 *
	 * Note: we purposely do not check the return of guc_lrc_desc_pin,
	 * because that function can only fail if a reset is just starting. This
	 * is at the end of reset so presumably another reset isn't happening
	 * and even it did this code would be run again.
	 */

	for_each_engine(engine, gt, id)
		if (engine->kernel_context)
			guc_kernel_context_pin(guc, engine->kernel_context);
}

static void guc_release(struct intel_engine_cs *engine)
{
	engine->sanitize = NULL; /* no longer in control, nothing to sanitize */

	intel_engine_cleanup_common(engine);
	lrc_fini_wa_ctx(engine);
}

static void virtual_guc_bump_serial(struct intel_engine_cs *engine)
{
	struct intel_engine_cs *e;
	intel_engine_mask_t tmp, mask = engine->mask;

	for_each_engine_masked(e, engine->gt, mask, tmp)
		e->serial++;
}

static void guc_default_vfuncs(struct intel_engine_cs *engine)
{
	/* Default vfuncs which can be overridden by each engine. */

	engine->resume = guc_resume;

	engine->cops = &guc_context_ops;
	engine->request_alloc = guc_request_alloc;
	engine->add_active_request = add_to_context;
	engine->remove_active_request = remove_from_context;

	engine->sched_engine->schedule = i915_schedule;

	engine->reset.prepare = guc_reset_nop;
	engine->reset.rewind = guc_rewind_nop;
	engine->reset.cancel = guc_reset_nop;
	engine->reset.finish = guc_reset_nop;

	engine->emit_flush = gen8_emit_flush_xcs;
	engine->emit_init_breadcrumb = gen8_emit_init_breadcrumb;
	engine->emit_fini_breadcrumb = gen8_emit_fini_breadcrumb_xcs;
	if (GRAPHICS_VER(engine->i915) >= 12) {
		engine->emit_fini_breadcrumb = gen12_emit_fini_breadcrumb_xcs;
		engine->emit_flush = gen12_emit_flush_xcs;
	}
	engine->set_default_submission = guc_set_default_submission;

	engine->flags |= I915_ENGINE_HAS_PREEMPTION;
	engine->flags |= I915_ENGINE_HAS_TIMESLICES;

	/*
	 * TODO: GuC supports timeslicing and semaphores as well, but they're
	 * handled by the firmware so some minor tweaks are required before
	 * enabling.
	 *
	 * engine->flags |= I915_ENGINE_HAS_SEMAPHORES;
	 */

	engine->emit_bb_start = gen8_emit_bb_start;
}

static void rcs_submission_override(struct intel_engine_cs *engine)
{
	switch (GRAPHICS_VER(engine->i915)) {
	case 12:
		engine->emit_flush = gen12_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen12_emit_fini_breadcrumb_rcs;
		break;
	case 11:
		engine->emit_flush = gen11_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen11_emit_fini_breadcrumb_rcs;
		break;
	default:
		engine->emit_flush = gen8_emit_flush_rcs;
		engine->emit_fini_breadcrumb = gen8_emit_fini_breadcrumb_rcs;
		break;
	}
}

static inline void guc_default_irqs(struct intel_engine_cs *engine)
{
	engine->irq_keep_mask = GT_RENDER_USER_INTERRUPT;
	intel_engine_set_irq_handler(engine, cs_irq_handler);
}

static void guc_sched_engine_destroy(struct kref *kref)
{
	struct i915_sched_engine *sched_engine =
		container_of(kref, typeof(*sched_engine), ref);
	struct guc_submit_engine *gse =
		container_of(sched_engine, typeof(*gse), sched_engine);
	struct intel_guc *guc = gse->sched_engine.private_data;

	guc->gse[gse->id] = NULL;
	tasklet_kill(&sched_engine->tasklet); /* flush the callback */
	kfree(gse);
}

static void guc_submit_engine_init(struct intel_guc *guc,
				   struct guc_submit_engine *gse,
				   int id)
{
	struct i915_sched_engine *sched_engine = &gse->sched_engine;

	i915_sched_engine_init(sched_engine, ENGINE_VIRTUAL);
	INIT_WORK(&gse->retire_worker, retire_worker_func);
	tasklet_setup(&sched_engine->tasklet, gse_submission_tasklet);
	sched_engine->schedule = i915_schedule;
	sched_engine->disabled = guc_sched_engine_disabled;
	sched_engine->destroy = guc_sched_engine_destroy;
	sched_engine->bump_inflight_request_prio =
		guc_bump_inflight_request_prio;
	sched_engine->retire_inflight_request_prio =
		guc_retire_inflight_request_prio;
	sched_engine->private_data = guc;
	gse->id = id;
}

int intel_guc_submission_setup(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct intel_guc *guc = &engine->gt->uc.guc;
	struct i915_sched_engine *sched_engine;
	int ret, i;

	/*
	 * The setup relies on several assumptions (e.g. irqs always enabled)
	 * that are only valid on gen11+
	 */
	GEM_BUG_ON(GRAPHICS_VER(i915) < 11);

	if (!guc->gse[0]) {
		for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i) {
			guc->gse[i] = kzalloc(sizeof(*guc->gse[i]), GFP_KERNEL);
			if (!guc->gse[i]) {
				ret = -ENOMEM;
				goto put_sched_engine;
			}
			guc_submit_engine_init(guc, guc->gse[i], i);
		}
	}

	sched_engine = guc_to_sched_engine(guc, GUC_SUBMIT_ENGINE_SINGLE_LRC);
	i915_sched_engine_put(engine->sched_engine);
	engine->sched_engine = i915_sched_engine_get(sched_engine);

	guc_default_vfuncs(engine);
	guc_default_irqs(engine);
	guc_init_breadcrumbs(engine);

	if (engine->class == RENDER_CLASS)
		rcs_submission_override(engine);

	lrc_init_wa_ctx(engine);

	/* Finally, take ownership and responsibility for cleanup! */
	engine->sanitize = guc_sanitize;
	engine->release = guc_release;

	return 0;

put_sched_engine:
	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i) {
		struct i915_sched_engine *sched_engine =
			guc_to_sched_engine(guc, i);

		if (sched_engine)
			i915_sched_engine_put(sched_engine);
	}
	return ret;
}

void intel_guc_submission_enable(struct intel_guc *guc)
{
	guc_init_lrc_mapping(guc);
}

void intel_guc_submission_disable(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);

	GEM_BUG_ON(gt->awake); /* GT should be parked first */

	/* Note: By the time we're here, GuC may have already been reset */
}

static bool __guc_submission_supported(struct intel_guc *guc)
{
	/* GuC submission is unavailable for pre-Gen11 */
	return intel_guc_is_supported(guc) &&
	       GRAPHICS_VER(guc_to_gt(guc)->i915) >= 11;
}

static bool __guc_submission_selected(struct intel_guc *guc)
{
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (!intel_guc_submission_is_supported(guc))
		return false;

	return i915->params.enable_guc & ENABLE_GUC_SUBMISSION;
}

void intel_guc_submission_init_early(struct intel_guc *guc)
{
	guc->max_guc_ids = guc->num_guc_ids = GUC_MAX_LRC_DESCRIPTORS;
	guc->submission_supported = __guc_submission_supported(guc);
	guc->submission_selected = __guc_submission_selected(guc);
}

static inline struct intel_context *
g2h_context_lookup(struct intel_guc *guc, u32 desc_idx)
{
	struct intel_context *ce;

	if (unlikely(desc_idx >= guc->max_guc_ids)) {
		drm_err(&guc_to_gt(guc)->i915->drm,
			"Invalid desc_idx %u, max %u",
			desc_idx, guc->max_guc_ids);
		return NULL;
	}

	ce = __get_context(guc, desc_idx);
	if (unlikely(!ce)) {
		drm_err(&guc_to_gt(guc)->i915->drm,
			"Context is NULL, desc_idx %u", desc_idx);
		return NULL;
	}

	return ce;
}

static void decr_outstanding_submission_g2h(struct intel_guc *guc)
{
	if (atomic_dec_and_test(&guc->outstanding_submission_g2h))
		wake_up_all(&guc->ct.wq);
}

int intel_guc_deregister_done_process_msg(struct intel_guc *guc,
					  const u32 *msg,
					  u32 len)
{
	struct intel_context *ce;
	u32 desc_idx = msg[0];

	if (unlikely(len < 1)) {
		drm_err(&guc_to_gt(guc)->i915->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	ce = g2h_context_lookup(guc, desc_idx);
	if (unlikely(!ce))
		return -EPROTO;

	trace_intel_context_deregister_done(ce);

	if (context_wait_for_deregister_to_register(ce)) {
		struct intel_runtime_pm *runtime_pm =
			&ce->engine->gt->i915->runtime_pm;
		intel_wakeref_t wakeref;

		/*
		 * Previous owner of this guc_id has been deregistered, now safe
		 * register this context.
		 */
		with_intel_runtime_pm(runtime_pm, wakeref)
			register_context(ce, true);
		guc_signal_context_fence(ce);
		if (context_block_tasklet(ce)) {
			struct guc_submit_engine *gse = ce_to_gse(ce);

			GEM_BUG_ON(gse->submission_stall_reason !=
				   STALL_DEREGISTER_CONTEXT);

			clr_context_block_tasklet(ce);
			gse->submission_stall_reason = STALL_MOVE_LRC_TAIL;
			clr_tasklet_blocked(gse);

			kick_tasklet(gse);
		}
		intel_context_put(ce);
	} else if (context_destroyed(ce)) {
		/* Context has been destroyed */
		release_guc_id(guc, ce);
		__guc_context_destroy(ce);
	}

	decr_outstanding_submission_g2h(guc);

	return 0;
}

int intel_guc_sched_done_process_msg(struct intel_guc *guc,
				     const u32 *msg,
				     u32 len)
{
	struct intel_context *ce;
	unsigned long flags;
	u32 desc_idx = msg[0];

	if (unlikely(len < 2)) {
		drm_err(&guc_to_gt(guc)->i915->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	ce = g2h_context_lookup(guc, desc_idx);
	if (unlikely(!ce))
		return -EPROTO;

	if (unlikely(context_destroyed(ce) ||
		     (!context_pending_enable(ce) &&
		     !context_pending_disable(ce)))) {
		drm_err(&guc_to_gt(guc)->i915->drm,
			"Bad context sched_state 0x%x, 0x%x, desc_idx %u",
			atomic_read(&ce->guc_sched_state_no_lock),
			ce->guc_state.sched_state, desc_idx);
		return -EPROTO;
	}

	trace_intel_context_sched_done(ce);

	if (context_pending_enable(ce)) {
		clr_context_pending_enable(ce);
	} else if (context_pending_disable(ce)) {
		bool banned;

		/*
		 * Unpin must be done before __guc_signal_context_fence,
		 * otherwise a race exists between the requests getting
		 * submitted + retired before this unpin completes resulting in
		 * the pin_count going to zero and the context still being
		 * enabled.
		 */
		intel_context_sched_disable_unpin(ce);

		spin_lock_irqsave(&ce->guc_state.lock, flags);
		banned = context_banned(ce);
		clr_context_banned(ce);
		clr_context_pending_disable(ce);
		__guc_signal_context_fence(ce);
		guc_blocked_fence_complete(ce);
		spin_unlock_irqrestore(&ce->guc_state.lock, flags);

		if (context_block_tasklet(ce)) {
			struct guc_submit_engine *gse = ce_to_gse(ce);

			clr_context_block_tasklet(ce);
			gse->submission_stall_reason = STALL_REGISTER_CONTEXT;
			clr_tasklet_blocked(gse);

			kick_tasklet(gse);
		}

		if (banned) {
			guc_cancel_context_requests(ce);
			intel_engine_signal_breadcrumbs(ce->engine);
		}
	}

	decr_outstanding_submission_g2h(guc);
	intel_context_put(ce);

	return 0;
}

static void capture_error_state(struct intel_guc *guc,
				struct intel_context *ce)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
	struct intel_engine_cs *engine = __context_to_physical_engine(ce);
	intel_wakeref_t wakeref;

	intel_engine_set_hung_context(engine, ce);
	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		i915_capture_error_state(gt, engine->mask);
	atomic_inc(&i915->gpu_error.reset_engine_count[engine->uabi_class]);
}

static void guc_context_replay(struct intel_context *ce)
{
	__guc_reset_context(ce, true);
	kick_tasklet(ce_to_gse(ce));
}

static void guc_handle_context_reset(struct intel_guc *guc,
				     struct intel_context *ce)
{
	trace_intel_context_reset(ce);

	if (likely(!intel_context_is_banned(ce))) {
		capture_error_state(guc, ce);
		guc_context_replay(ce);
	}
}

int intel_guc_context_reset_process_msg(struct intel_guc *guc,
					const u32 *msg, u32 len)
{
	struct intel_context *ce;
	int desc_idx;

	if (unlikely(len != 1)) {
		drm_err(&guc_to_gt(guc)->i915->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	desc_idx = msg[0];
	ce = g2h_context_lookup(guc, desc_idx);
	if (unlikely(!ce))
		return -EPROTO;

	guc_handle_context_reset(guc, ce);

	return 0;
}

static struct intel_engine_cs *
guc_lookup_engine(struct intel_guc *guc, u8 guc_class, u8 instance)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u8 engine_class = guc_class_to_engine_class(guc_class);

	/* Class index is checked in class converter */
	GEM_BUG_ON(instance > MAX_ENGINE_INSTANCE);

	return gt->engine_class[engine_class][instance];
}

int intel_guc_engine_failure_process_msg(struct intel_guc *guc,
					 const u32 *msg, u32 len)
{
	struct intel_engine_cs *engine;
	u8 guc_class, instance;
	u32 reason;

	if (unlikely(len != 3)) {
		drm_err(&guc_to_gt(guc)->i915->drm, "Invalid length %u", len);
		return -EPROTO;
	}

	guc_class = msg[0];
	instance = msg[1];
	reason = msg[2];

	engine = guc_lookup_engine(guc, guc_class, instance);
	if (unlikely(!engine)) {
		drm_err(&guc_to_gt(guc)->i915->drm,
			"Invalid engine %d:%d", guc_class, instance);
		return -EPROTO;
	}

	intel_gt_handle_error(guc_to_gt(guc), engine->mask,
			      I915_ERROR_CAPTURE,
			      "GuC failed to reset %s (reason=0x%08x)\n",
			      engine->name, reason);

	return 0;
}

void intel_guc_find_hung_context(struct intel_engine_cs *engine)
{
	struct intel_guc *guc = &engine->gt->uc.guc;
	struct intel_context *ce;
	struct i915_request *rq;
	unsigned long index;

	/* Reset called during driver load? GuC not yet initialised! */
	if (unlikely(!guc_submission_initialized(guc)))
		return;

	xa_for_each(&guc->context_lookup, index, ce) {
		if (!intel_context_is_pinned(ce))
			continue;

		if (intel_engine_is_virtual(ce->engine)) {
			if (!(ce->engine->mask & engine->mask))
				continue;
		} else {
			if (ce->engine != engine)
				continue;
		}

		list_for_each_entry(rq, &ce->guc_active.requests, sched.link) {
			if (i915_test_request_state(rq) != I915_REQUEST_ACTIVE)
				continue;

			intel_engine_set_hung_context(engine, ce);

			/* Can only cope with one hang at a time... */
			return;
		}
	}
}

void intel_guc_dump_active_requests(struct intel_engine_cs *engine,
				    struct i915_request *hung_rq,
				    struct drm_printer *m)
{
	struct intel_guc *guc = &engine->gt->uc.guc;
	struct intel_context *ce;
	unsigned long index;
	unsigned long flags;

	/* Reset called during driver load? GuC not yet initialised! */
	if (unlikely(!guc_submission_initialized(guc)))
		return;

	xa_for_each(&guc->context_lookup, index, ce) {
		if (!intel_context_is_pinned(ce))
			continue;

		if (intel_engine_is_virtual(ce->engine)) {
			if (!(ce->engine->mask & engine->mask))
				continue;
		} else {
			if (ce->engine != engine)
				continue;
		}

		spin_lock_irqsave(&ce->guc_active.lock, flags);
		intel_engine_dump_active_requests(&ce->guc_active.requests,
						  hung_rq, m);
		spin_unlock_irqrestore(&ce->guc_active.lock, flags);
	}
}

static void gse_log_submission_info(struct guc_submit_engine *gse,
				    struct drm_printer *p, int id)
{
	struct i915_sched_engine *sched_engine = &gse->sched_engine;
	struct rb_node *rb;
	unsigned long flags;

	if (!sched_engine)
		return;

	drm_printf(p, "GSE[%d] tasklet count: %u\n", id,
		   atomic_read(&sched_engine->tasklet.count));
	drm_printf(p, "GSE[%d] submit flags: 0x%04lx\n", id, gse->flags);
	drm_printf(p, "GSE[%d] total number request without guc_id: %d\n",
		   id, gse->total_num_rq_with_no_guc_id);
	drm_printf(p, "GSE[%d] Number GuC IDs not ready: %d\n",
		   id, atomic_read(&gse->num_guc_ids_not_ready));
	drm_printf(p, "GSE[%d] stall reason: %d\n",
		   id, gse->submission_stall_reason);
	drm_printf(p, "GSE[%d] stalled request: %s\n",
		   id, yesno(gse->stalled_rq));
	drm_printf(p, "GSE[%d] stalled context: %s\n\n",
		   id, yesno(gse->stalled_context));

	spin_lock_irqsave(&sched_engine->lock, flags);
	drm_printf(p, "Requests in GSE[%d] submit tasklet:\n", id);
	for (rb = rb_first_cached(&sched_engine->queue); rb; rb = rb_next(rb)) {
		struct i915_priolist *pl = to_priolist(rb);
		struct i915_request *rq;

		priolist_for_each_request(rq, pl)
			drm_printf(p, "guc_id=%u, seqno=%llu\n",
				   rq->context->guc_id,
				   rq->fence.seqno);
	}
	spin_unlock_irqrestore(&sched_engine->lock, flags);
	drm_printf(p, "\n");
}

static inline void guc_log_context_priority(struct drm_printer *p,
					    struct intel_context *ce)
{
	int i;

	drm_printf(p, "\t\tPriority: %d\n",
		   ce->guc_prio);
	drm_printf(p, "\t\tNumber Requests (lower index == higher priority)\n");
	for (i = GUC_CLIENT_PRIORITY_KMD_HIGH;
	     i < GUC_CLIENT_PRIORITY_NUM; ++i) {
		drm_printf(p, "\t\tNumber requests in priority band[%d]: %d\n",
			   i, ce->guc_prio_count[i]);
	}
	drm_printf(p, "\n");
}

void intel_guc_submission_print_info(struct intel_guc *guc,
				     struct drm_printer *p)
{
	int i;

	drm_printf(p, "GuC Number Outstanding Submission G2H: %u\n",
		   atomic_read(&guc->outstanding_submission_g2h));
	drm_printf(p, "GuC Number GuC IDs: %d\n", guc->num_guc_ids);
	drm_printf(p, "GuC Max Number GuC IDs: %d\n\n", guc->max_guc_ids);

	for (i = 0; i < GUC_SUBMIT_ENGINE_MAX; ++i)
		gse_log_submission_info(guc->gse[i], p, i);
}

void intel_guc_submission_print_context_info(struct intel_guc *guc,
					     struct drm_printer *p)
{
	struct intel_context *ce;
	unsigned long index;
	xa_for_each(&guc->context_lookup, index, ce) {
		drm_printf(p, "GuC lrc descriptor %u:\n", ce->guc_id);
		drm_printf(p, "\tHW Context Desc: 0x%08x\n", ce->lrc.lrca);
		drm_printf(p, "\t\tLRC Head: Internal %u, Memory %u\n",
			   ce->ring->head,
			   ce->lrc_reg_state[CTX_RING_HEAD]);
		drm_printf(p, "\t\tLRC Tail: Internal %u, Memory %u\n",
			   ce->ring->tail,
			   ce->lrc_reg_state[CTX_RING_TAIL]);
		drm_printf(p, "\t\tContext Pin Count: %u\n",
			   atomic_read(&ce->pin_count));
		drm_printf(p, "\t\tGuC ID Ref Count: %u\n",
			   atomic_read(&ce->guc_id_ref));
		drm_printf(p, "\t\tNumber Requests Not Ready: %u\n",
			   atomic_read(&ce->guc_num_rq_not_ready));
		drm_printf(p, "\t\tSchedule State: 0x%x, 0x%x\n\n",
			   ce->guc_state.sched_state,
			   atomic_read(&ce->guc_sched_state_no_lock));

		guc_log_context_priority(p, ce);
	}
}

static struct intel_context *
guc_create_virtual(struct intel_engine_cs **siblings, unsigned int count)
{
	struct guc_virtual_engine *ve;
	struct intel_guc *guc;
	struct i915_sched_engine *sched_engine;
	unsigned int n;
	int err;

	ve = kzalloc(sizeof(*ve), GFP_KERNEL);
	if (!ve)
		return ERR_PTR(-ENOMEM);

	guc = &siblings[0]->gt->uc.guc;
	sched_engine = guc_to_sched_engine(guc, GUC_SUBMIT_ENGINE_SINGLE_LRC);

	ve->base.i915 = siblings[0]->i915;
	ve->base.gt = siblings[0]->gt;
	ve->base.uncore = siblings[0]->uncore;
	ve->base.id = -1;

	ve->base.uabi_class = I915_ENGINE_CLASS_INVALID;
	ve->base.instance = I915_ENGINE_CLASS_INVALID_VIRTUAL;
	ve->base.uabi_instance = I915_ENGINE_CLASS_INVALID_VIRTUAL;
	ve->base.saturated = ALL_ENGINES;

	snprintf(ve->base.name, sizeof(ve->base.name), "virtual");

	ve->base.sched_engine = i915_sched_engine_get(sched_engine);

	ve->base.cops = &virtual_guc_context_ops;
	ve->base.request_alloc = guc_request_alloc;
	ve->base.bump_serial = virtual_guc_bump_serial;

	ve->base.submit_request = guc_submit_request;

	ve->base.flags = I915_ENGINE_IS_VIRTUAL;

	intel_context_init(&ve->context, &ve->base);

	for (n = 0; n < count; n++) {
		struct intel_engine_cs *sibling = siblings[n];

		GEM_BUG_ON(!is_power_of_2(sibling->mask));
		if (sibling->mask & ve->base.mask) {
			DRM_DEBUG("duplicate %s entry in load balancer\n",
				  sibling->name);
			err = -EINVAL;
			goto err_put;
		}

		ve->base.mask |= sibling->mask;

		if (n != 0 && ve->base.class != sibling->class) {
			DRM_DEBUG("invalid mixing of engine class, sibling %d, already %d\n",
				  sibling->class, ve->base.class);
			err = -EINVAL;
			goto err_put;
		} else if (n == 0) {
			ve->base.class = sibling->class;
			ve->base.uabi_class = sibling->uabi_class;
			snprintf(ve->base.name, sizeof(ve->base.name),
				 "v%dx%d", ve->base.class, count);
			ve->base.context_size = sibling->context_size;

			ve->base.add_active_request =
				sibling->add_active_request;
			ve->base.remove_active_request =
				sibling->remove_active_request;
			ve->base.emit_bb_start = sibling->emit_bb_start;
			ve->base.emit_flush = sibling->emit_flush;
			ve->base.emit_init_breadcrumb =
				sibling->emit_init_breadcrumb;
			ve->base.emit_fini_breadcrumb =
				sibling->emit_fini_breadcrumb;
			ve->base.emit_fini_breadcrumb_dw =
				sibling->emit_fini_breadcrumb_dw;
			ve->base.breadcrumbs =
				intel_breadcrumbs_get(sibling->breadcrumbs);

			ve->base.flags |= sibling->flags;

			ve->base.props.timeslice_duration_ms =
				sibling->props.timeslice_duration_ms;
			ve->base.props.preempt_timeout_ms =
				sibling->props.preempt_timeout_ms;
		}
	}

	return &ve->context;

err_put:
	intel_context_put(&ve->context);
	return ERR_PTR(err);
}



bool intel_guc_virtual_engine_has_heartbeat(const struct intel_engine_cs *ve)
{
	struct intel_engine_cs *engine;
	intel_engine_mask_t tmp, mask = ve->mask;

	for_each_engine_masked(engine, ve->gt, mask, tmp)
		if (READ_ONCE(engine->props.heartbeat_interval_ms))
			return true;

	return false;
}
