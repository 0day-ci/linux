// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"
#include "hooks.h"

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/rcupdate.h>

/**
 * ipe_task_alloc: Assign a new context for an associated task structure.
 * @task: Supplies the task structure to assign a context to.
 * @clone_flags: unused.
 *
 * The context assigned is always the context of the current task.
 * Reference counts are dropped by ipe_task_free
 *
 * Return:
 * 0 - OK
 * -ENOMEM - Out of Memory
 */
int ipe_task_alloc(struct task_struct *task, unsigned long clone_flags)
{
	struct ipe_context __rcu **ctx = NULL;
	struct ipe_context *current_ctx = NULL;

	current_ctx = ipe_current_ctx();
	ctx = ipe_tsk_ctx(task);
	rcu_assign_pointer(*ctx, current_ctx);
	refcount_inc(&current_ctx->refcount);

	ipe_put_ctx(current_ctx);
	return 0;
}

/**
 * ipe_task_free: Drop a reference to an ipe_context associated with @task.
 *		  If there are no tasks remaining, the context is freed.
 * @task: Supplies the task to drop an ipe_context reference to.
 */
void ipe_task_free(struct task_struct *task)
{
	struct ipe_context *ctx;

	/*
	 * This reference was the initial creation, no need to increment
	 * refcount
	 */
	rcu_read_lock();
	ctx = rcu_dereference(*ipe_tsk_ctx(task));
	ipe_put_ctx(ctx);
	rcu_read_unlock();
}
