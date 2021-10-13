// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"
#include "policy.h"

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/parser.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>

/**
 * ver_to_u64: convert an internal ipe_policy_version to a u64
 * @p: Policy to extract the version from
 *
 * Bits (LSB is index 0):
 *	[48,32] -> Major
 *	[32,16] -> Minor
 *	[16, 0] -> Revision
 *
 * Return:
 * u64 version of the embedded version structure.
 */
static inline u64 ver_to_u64(const struct ipe_policy *const p)
{
	u64 r = 0;

	r = (((u64)p->parsed->version.major) << 32)
	  | (((u64)p->parsed->version.minor) << 16)
	  | ((u64)(p->parsed->version.rev));

	return r;
}

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
	struct ipe_policy *p = NULL;
	struct ipe_context *ctx = NULL;

	ctx = container_of(work, struct ipe_context, free_work);

	/* Make p->ctx no longer have any references */
	spin_lock(&ctx->lock);
	list_for_each_entry(p, &ctx->policies, next)
		rcu_assign_pointer(p->ctx, NULL);
	spin_unlock(&ctx->lock);
	synchronize_rcu();

	/*
	 * locking no longer necessary - nothing can get a reference to ctx,
	 * so list is guaranteed stable.
	 */
	list_for_each_entry(p, &ctx->policies, next)
		ipe_put_policy(p);

	securityfs_remove(ctx->policy_root);
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
	INIT_LIST_HEAD(&ctx->policies);
	refcount_set(&ctx->refcount, 1);
	spin_lock_init(&ctx->lock);

	return ctx;

err:
	ipe_put_ctx(ctx);
	return ERR_PTR(rc);
}

/**
 * remove_policy: Remove a policy from its context
 * @p: Supplies a pointer to a policy that will be removed from its context
 *
 * Decrements @p's reference by 1.
 */
void ipe_remove_policy(struct ipe_policy *p)
{
	struct ipe_context *ctx;

	ctx = ipe_get_ctx_rcu(p->ctx);
	if (!ctx)
		return;

	spin_lock(&ctx->lock);
	list_del_init(&p->next);
	rcu_assign_pointer(p->ctx, NULL);
	spin_unlock(&ctx->lock);
	synchronize_rcu();

	ipe_put_ctx(ctx);
	/* drop the reference representing the list */
	ipe_put_policy(p);
}

/**
 * ipe_add_policy: Associate @p with @ctx
 * @ctx: Supplies a pointer to the ipe_context structure to associate @p with.
 * @p: Supplies a pointer to the ipe_policy structure to associate.
 *
 * This will increase @p's reference count by one.
 *
 */
void ipe_add_policy(struct ipe_context *ctx, struct ipe_policy *p)
{
	spin_lock(&ctx->lock);
	rcu_assign_pointer(p->ctx, ctx);
	list_add_tail(&p->next, &ctx->policies);
	refcount_inc(&p->refcount);
	spin_unlock(&ctx->lock);
}

/**
 * ipe_replace_policy: Replace @old with @new in the list of policies in @ctx
 * @ctx: Supplies the context object to manipulate.
 * @old: Supplies a pointer to the ipe_policy to replace with @new
 * @new: Supplies a pointer to the ipe_policy structure to replace @old with
 */
int ipe_replace_policy(struct ipe_policy *old, struct ipe_policy *new)
{
	int rc = -EINVAL;
	struct ipe_context *ctx;
	struct ipe_policy *cursor;
	struct ipe_policy *p = NULL;

	ctx = ipe_get_ctx_rcu(old->ctx);
	if (!ctx)
		return -ENOENT;

	spin_lock(&ctx->lock);
	list_for_each_entry(cursor, &ctx->policies, next) {
		if (!strcmp(old->parsed->name, cursor->parsed->name)) {
			if (ipe_is_policy_active(old)) {
				if (ver_to_u64(old) > ver_to_u64(new))
					break;
				rcu_assign_pointer(ctx->active_policy, new);
			}
			list_replace_init(&cursor->next, &new->next);
			refcount_inc(&new->refcount);
			rcu_assign_pointer(new->ctx, old->ctx);
			p = cursor;
			rc = 0;
			break;
		}
	}
	spin_unlock(&ctx->lock);
	synchronize_rcu();

	ipe_put_policy(p);
	ipe_put_ctx(ctx);
	return rc;
}

/**
 * ipe_set_active_pol: Make @p the active policy.
 * @p: Supplies a pointer to the policy to make active.
 */
int ipe_set_active_pol(const struct ipe_policy *p)
{
	int rc = 0;
	struct ipe_policy *ap = NULL;
	struct ipe_context *ctx = NULL;

	ctx = ipe_get_ctx_rcu(p->ctx);
	if (!ctx) {
		rc = -ENOENT;
		goto out;
	}

	ap = ipe_get_policy_rcu(ctx->active_policy);
	if (ap && ver_to_u64(ap) > ver_to_u64(p)) {
		rc = -EINVAL;
		goto out;
	}

	spin_lock(&ctx->lock);
	rcu_assign_pointer(ctx->active_policy, p);
	spin_unlock(&ctx->lock);
	synchronize_rcu();

out:
	ipe_put_policy(ap);
	ipe_put_ctx(ctx);
	return rc;
}

/**
 * ipe_is_policy_active: Determine wehther @p is the active policy
 * @p: Supplies a pointer to the policy to check.
 *
 * Return:
 * true - @p is the active policy of @ctx
 * false - @p is not the active policy of @ctx
 */
bool ipe_is_policy_active(const struct ipe_policy *p)
{
	bool rv;
	struct ipe_context *ctx;

	rcu_read_lock();
	ctx = rcu_dereference(p->ctx);
	rv = !IS_ERR_OR_NULL(ctx) && rcu_access_pointer(ctx->active_policy) == p;
	rcu_read_unlock();

	return rv;
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
