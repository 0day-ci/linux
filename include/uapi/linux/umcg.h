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
 * See Documentation/userspace-api/umcg.[txt|rst] for detals.
 */

/*
 * UMCG task states, the first 8 bits. The states represent the user space
 * point of view.
 */
#define UMCG_TASK_NONE			0
#define UMCG_TASK_RUNNING		1
#define UMCG_TASK_IDLE			2
#define UMCG_TASK_BLOCKED		3

/* The first byte: RUNNING, IDLE, or BLOCKED. */
#define UMCG_TASK_STATE_MASK		0xff

/* UMCG task state flags, bits 8-15 */

/*
 * UMCG_TF_LOCKED: locked by the userspace in preparation to calling umcg_wait.
 */
#define UMCG_TF_LOCKED			(1 << 8)

/*
 * UMCG_TF_PREEMPTED: the userspace indicates the worker should be preempted.
 */
#define UMCG_TF_PREEMPTED		(1 << 9)

/**
 * struct umcg_task - controls the state of UMCG tasks.
 *
 * The struct is aligned at 64 bytes to ensure that it fits into
 * a single cache line.
 */
struct umcg_task {
	/**
	 * @state: the current state of the UMCG task described by this struct.
	 *
	 * Readable/writable by both the kernel and the userspace.
	 *
	 * UMCG task state:
	 *   bits  0 -  7: task state;
	 *   bits  8 - 15: state flags;
	 *   bits 16 - 23: reserved; must be zeroes;
	 *   bits 24 - 31: for userspace use.
	 */
	uint32_t	state;			/* r/w */

	/**
	 * @next_tid: the TID of the UMCG task that should be context-switched
	 *            into in sys_umcg_wait(). Can be zero.
	 *
	 * Running UMCG workers must have next_tid set to point to IDLE
	 * UMCG servers.
	 *
	 * Read-only for the kernel, read/write for the userspace.
	 */
	uint32_t	next_tid;		/* r   */

	/**
	 * @idle_workers_ptr: a single-linked list of idle workers. Can be NULL.
	 *
	 * Readable/writable by both the kernel and the userspace: the
	 * kernel adds items to the list, the userspace removes them.
	 */
	uint64_t	idle_workers_ptr;	/* r/w */

	/**
	 * @idle_server_tid_ptr: a pointer pointing to a single idle server.
	 *                       Readonly.
	 */
	uint64_t	idle_server_tid_ptr;	/* r   */
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

/* See Documentation/userspace-api/umcg.[txt|rst].*/
#define UMCG_IDLE_NODE_PENDING (1ULL)

#endif /* _UAPI_LINUX_UMCG_H */
