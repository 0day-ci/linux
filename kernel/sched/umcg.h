/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _KERNEL_SCHED_UMCG_H
#define _KERNEL_SCHED_UMCG_H

#ifdef CONFIG_UMCG

#include <linux/sched.h>
#include <linux/umcg.h>

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

	/*
	 * Used by wait/wake routines to handle races. Written only by current.
	 */
	bool				in_wait;
};

#endif  /* CONFIG_UMCG */
#endif  /* _KERNEL_SCHED_UMCG_H */
