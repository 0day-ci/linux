/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_UMCG_H
#define _UAPI_LINUX_UMCG_H

#include <linux/limits.h>
#include <linux/types.h>

/*
 * UMCG task states, the first 8 bits.
 */
#define UMCG_TASK_NONE			0
/* UMCG server states. */
#define UMCG_TASK_POLLING		1
#define UMCG_TASK_SERVING		2
#define UMCG_TASK_PROCESSING		3
/* UMCG worker states. */
#define UMCG_TASK_RUNNABLE		4
#define UMCG_TASK_RUNNING		5
#define UMCG_TASK_BLOCKED		6
#define UMCG_TASK_UNBLOCKED		7

/* UMCG task state flags, bits 8-15 */
#define UMCG_TF_WAKEUP_QUEUED		(1 << 8)

/*
 * Unused at the moment flags reserved for features to be introduced
 * in the near future.
 */
#define UMCG_TF_PREEMPT_DISABLED	(1 << 9)
#define UMCG_TF_PREEMPTED		(1 << 10)

#define UMCG_NOID	UINT_MAX

/**
 * struct umcg_task - controls the state of UMCG-enabled tasks.
 *
 * While at the moment only one field is present (@state), in future
 * versions additional fields will be added, e.g. for the userspace to
 * provide performance-improving hints and for the kernel to export sched
 * stats.
 *
 * The struct is aligned at 32 bytes to ensure that even with future additions
 * it fits into a single cache line.
 */
struct umcg_task {
	/**
	 * @state: the current state of the UMCG task described by this struct.
	 *
	 * UMCG task state:
	 *   bits  0 -  7: task state;
	 *   bits  8 - 15: state flags;
	 *   bits 16 - 23: reserved; must be zeroes;
	 *   bits 24 - 31: for userspace use.
	 */
	uint32_t	state;
} __attribute((packed, aligned(4 * sizeof(uint64_t))));

/**
 * enum umcg_register_flag - flags for sys_umcg_register
 * @UMCG_REGISTER_CORE_TASK:  Register a UMCG core task (not part of a group);
 * @UMCG_REGISTER_WORKER:     Register a UMCG worker task;
 * @UMCG_REGISTER_SERVER:     Register a UMCG server task.
 */
enum umcg_register_flag {
	UMCG_REGISTER_CORE_TASK		= 0,
	UMCG_REGISTER_WORKER		= 1,
	UMCG_REGISTER_SERVER		= 2
};

#endif /* _UAPI_LINUX_UMCG_H */
