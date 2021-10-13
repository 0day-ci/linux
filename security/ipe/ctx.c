// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/parser.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>

/**
 * ipe_current_ctx: Helper to retrieve the ipe_context for the current task.
 *
 * Return:
 *	See ipe_get_ctx_rcu
 */
struct ipe_context *ipe_current_ctx(void)
{
	return ipe_get_ctx_rcu(*ipe_tsk_ctx(current));
}

/**
 * ipe_tsk_ctx: Retrieve the RCU-protected address of the task
 *		that contains the ipe_context.
 * @tsk: Task to retrieve the address from.
 *
 * Callers need to use the rcu* family functions to interact with
 * the ipe_context, or ipe_get_ctx_rcu.
 *
 * Return:
 *	Valid Address to a location containing an RCU-protected ipe_context.
 */
struct ipe_context __rcu **ipe_tsk_ctx(struct task_struct *tsk)
{
	return tsk->security + ipe_blobs.lbs_task;
}

/**
 * ipe_get_ctx_rcu: Retrieve the underlying ipe_context in an rcu protected
 *		    address space.
 * @ctx: Context to dereference.
 *
 * This function will increment the reference count of the dereferenced
 * ctx, ensuring that it is valid outside of the rcu_read_lock.
 *
 * However, if a context has a reference count of 0 (and thus being)
 * freed, this API will return NULL.
 *
 * Return:
 *	!NULL - Valid context
 *	NULL - the dereferenced context will not exist outside after the
 *	       next grace period.
 */
struct ipe_context *ipe_get_ctx_rcu(struct ipe_context __rcu *ctx)
{
	struct ipe_context *rv = NULL;

	rcu_read_lock();

	rv = rcu_dereference(ctx);
	if (!rv || !refcount_inc_not_zero(&rv->refcount))
		rv = NULL;

	rcu_read_unlock();

	return rv;
}

/**
 * free_ctx_work: Worker function to deallocate a context structure.
 * @work: work_struct passed to schedule_work
 */
static void free_ctx_work(struct work_struct *const work)
{
	struct ipe_context *ctx = NULL;

	ctx = container_of(work, struct ipe_context, free_work);

	kfree(ctx);
}

/**
 * create_ctx: Allocate a context structure.
 *
 * The reference count at this point will be set to 1.
 *
 * Return:
 * !IS_ERR - OK
 * ERR_PTR(-ENOMEM) - Lack of memory.
 */
static struct ipe_context *create_ctx(void)
{
	int rc = 0;
	struct ipe_context *ctx = NULL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto err;
	}

	INIT_WORK(&ctx->free_work, free_ctx_work);
	refcount_set(&ctx->refcount, 1);
	spin_lock_init(&ctx->lock);

	return ctx;

err:
	ipe_put_ctx(ctx);
	return ERR_PTR(rc);
}

/**
 * ipe_put_ns: Decrement the reference of an ipe_context structure,
 *	       scheduling a free as necessary.s
 * @ctx: Structure to free
 *
 * This function no-ops on error and null values for @ctx, and the
 * deallocation will only occur if the refcount is 0.
 */
void ipe_put_ctx(struct ipe_context *ctx)
{
	if (IS_ERR_OR_NULL(ctx) || !refcount_dec_and_test(&ctx->refcount))
		return;

	schedule_work(&ctx->free_work);
}

/**
 * ipe_init_ctx: Initialize the init context.
 *
 * This is called at LSM init, and marks the kernel init process
 * with a context. All processes descendent from kernel
 * init will inherit this context.
 *
 * Return:
 * 0 - OK
 * -ENOMEM: Not enough memory to allocate the init context.
 */
int __init ipe_init_ctx(void)
{
	int rc = 0;
	struct ipe_context *lns = NULL;

	lns = create_ctx();
	if (IS_ERR(lns)) {
		rc = PTR_ERR(lns);
		goto err;
	}

	rcu_assign_pointer(*ipe_tsk_ctx(current), lns);

	return 0;
err:
	ipe_put_ctx(lns);
	return rc;
}
