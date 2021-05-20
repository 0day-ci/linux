/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _KERNEL_SCHED_UMCG_H
#define _KERNEL_SCHED_UMCG_H

#ifdef CONFIG_UMCG

#include <linux/sched.h>
#include <linux/umcg.h>

struct umcg_group {
	struct list_head list;
	u32 group_id;     /* Never changes. */
	u32 api_version;  /* Never changes. */
	u64 flags;        /* Never changes. */

	spinlock_t lock;

	/*
	 * One of the counters below is always zero. The non-zero counter
	 * indicates the number of elements in @waiters below.
	 */
	int nr_waiting_workers;
	int nr_waiting_pollers;

	/*
	 * The list below either contains UNBLOCKED workers waiting
	 * for the userspace to poll or run them if nr_waiting_workers > 0,
	 *  or polling servers waiting for unblocked workers if
	 *  nr_waiting_pollers > 0.
	 */
	struct list_head waiters;

	int nr_tasks;  /* The total number of tasks registered. */

	struct rcu_head rcu;
};

enum umcg_task_type {
	UMCG_TT_CORE	= 1,
	UMCG_TT_SERVER	= 2,
	UMCG_TT_WORKER	= 3
};

struct umcg_task_data {
	/* umcg_task != NULL. Never changes. */
	struct umcg_task __user		*umcg_task;

	/* The task that owns this umcg_task_data. Never changes. */
	struct task_struct		*self;

	/* Core task, server, or worker. Never changes. */
	enum umcg_task_type		task_type;

	/*
	 * The API version used to register this task. If this is a
	 * worker or a server, must equal group->api_version.
	 *
	 * Never changes.
	 */
	u32 api_version;

	/* NULL for core API tasks. Never changes. */
	struct umcg_group		*group;

	/*
	 * If this is a server task, points to its assigned worker, if any;
	 * if this is a worker task, points to its assigned server, if any.
	 *
	 * Protected by alloc_lock of the task owning this struct.
	 *
	 * Always either NULL, or the server and the worker point to each other.
	 * Locking order: first lock the server, then the worker.
	 *
	 * Either the worker or the server should be the current task when
	 * this field is changed, with the exception of sys_umcg_swap.
	 */
	struct task_struct __rcu	*peer;

	/* Used in umcg_group.waiters. */
	struct list_head		list;

	/* Used by curr in umcg_on_block/wake to prevent nesting/recursion. */
	bool				in_workqueue;

	/*
	 * Used by wait/wake routines to handle races. Written only by current.
	 */
	bool				in_wait;
};

void umcg_on_block(void);
void umcg_on_wake(void);

#endif  /* CONFIG_UMCG */
#endif  /* _KERNEL_SCHED_UMCG_H */
