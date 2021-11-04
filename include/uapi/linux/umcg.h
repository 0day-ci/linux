/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_UMCG_H
#define _UAPI_LINUX_UMCG_H

#include <linux/limits.h>
#include <linux/types.h>

/*
 * UMCG: User Managed Concurrency Groups.
 *
 * Syscalls (see kernel/sched/umcg.c):
 *      sys_umcg_ctl()  - register/unregister UMCG tasks;
 *      sys_umcg_wait() - wait/wake/context-switch.
 *
 * struct umcg_task (below): controls the state of UMCG tasks.
 *
 * See Documentation/userspace-api/umcg.txt for detals.
 */

/*
 * UMCG task states, the first 6 bits of struct umcg_task.state_ts.
 * The states represent the user space point of view.
 */
#define UMCG_TASK_NONE			0ULL
#define UMCG_TASK_RUNNING		1ULL
#define UMCG_TASK_IDLE			2ULL
#define UMCG_TASK_BLOCKED		3ULL

/* UMCG task state flags, bits 7-8 */

/*
 * UMCG_TF_LOCKED: locked by the userspace in preparation to calling umcg_wait.
 */
#define UMCG_TF_LOCKED			(1ULL << 6)

/*
 * UMCG_TF_PREEMPTED: the userspace indicates the worker should be preempted.
 */
#define UMCG_TF_PREEMPTED		(1ULL << 7)

/* The first six bits: RUNNING, IDLE, or BLOCKED. */
#define UMCG_TASK_STATE_MASK		0x3fULL

/* The full state mask: the first 18 bits. */
#define UMCG_TASK_STATE_MASK_FULL	0x3ffffULL

/*
 * The number of bits reserved for UMCG state timestamp in
 * struct umcg_task.state_ts.
 */
#define UMCG_STATE_TIMESTAMP_BITS	46

/* The number of bits truncated from UMCG state timestamp. */
#define UMCG_STATE_TIMESTAMP_GRANULARITY	4

/**
 * struct umcg_task - controls the state of UMCG tasks.
 *
 * The struct is aligned at 64 bytes to ensure that it fits into
 * a single cache line.
 */
struct umcg_task {
	/**
	 * @state_ts: the current state of the UMCG task described by
	 *            this struct, with a unique timestamp indicating
	 *            when the last state change happened.
	 *
	 * Readable/writable by both the kernel and the userspace.
	 *
	 * UMCG task state:
	 *   bits  0 -  5: task state;
	 *   bits  6 -  7: state flags;
	 *   bits  8 - 12: reserved; must be zeroes;
	 *   bits 13 - 17: for userspace use;
	 *   bits 18 - 63: timestamp (see below).
	 *
	 * Timestamp: a 46-bit CLOCK_MONOTONIC timestamp, at 16ns resolution.
	 * See Documentation/userspace-api/umcg.txt for detals.
	 */
	u64	state_ts;		/* r/w */

	/**
	 * @next_tid: the TID of the UMCG task that should be context-switched
	 *            into in sys_umcg_wait(). Can be zero.
	 *
	 * Running UMCG workers must have next_tid set to point to IDLE
	 * UMCG servers.
	 *
	 * Read-only for the kernel, read/write for the userspace.
	 */
	u32	next_tid;		/* r   */

	u32	flags;			/* Reserved; must be zero. */

	/**
	 * @idle_workers_ptr: a single-linked list of idle workers. Can be NULL.
	 *
	 * Readable/writable by both the kernel and the userspace: the
	 * kernel adds items to the list, the userspace removes them.
	 */
	u64	idle_workers_ptr;	/* r/w */

	/**
	 * @idle_server_tid_ptr: a pointer pointing to a single idle server.
	 *                       Readonly.
	 */
	u64	idle_server_tid_ptr;	/* r   */
} __attribute__((packed, aligned(8 * sizeof(__u64))));

/**
 * enum umcg_ctl_flag - flags to pass to sys_umcg_ctl
 * @UMCG_CTL_REGISTER:   register the current task as a UMCG task
 * @UMCG_CTL_UNREGISTER: unregister the current task as a UMCG task
 * @UMCG_CTL_WORKER:     register the current task as a UMCG worker
 */
enum umcg_ctl_flag {
	UMCG_CTL_REGISTER	= 0x00001,
	UMCG_CTL_UNREGISTER	= 0x00002,
	UMCG_CTL_WORKER		= 0x10000,
};

/**
 * enum umcg_wait_flag - flags to pass to sys_umcg_wait
 * @UMCG_WAIT_WAKE_ONLY:      wake @self->next_tid, don't put @self to sleep;
 * @UMCG_WAIT_WF_CURRENT_CPU: wake @self->next_tid on the current CPU
 *                            (use WF_CURRENT_CPU); @UMCG_WAIT_WAKE_ONLY
 *                            must be set.
 */
enum umcg_wait_flag {
	UMCG_WAIT_WAKE_ONLY			= 1,
	UMCG_WAIT_WF_CURRENT_CPU		= 2,
};

/* See Documentation/userspace-api/umcg.txt.*/
#define UMCG_IDLE_NODE_PENDING (1ULL)

#endif /* _UAPI_LINUX_UMCG_H */
