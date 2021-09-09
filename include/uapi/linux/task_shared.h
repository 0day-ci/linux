/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef LINUX_TASK_SHARED_H
#define LINUX_TASK_SHARED_H

/*
 * Per task user-kernel mapped structure for faster communication.
 */

/*
 * Following is the option to request struct task_schedstats shared structure,
 * in which kernel shares the task's exec time and time on run queue & number
 * of times it was scheduled to run on a cpu. Requires kernel with
 * CONFIG_SCHED_INFO enabled.
 */
#define TASK_SCHEDSTAT 1

struct task_schedstat {
	volatile u64	sum_exec_runtime;
	volatile u64	run_delay;
	volatile u64	pcount;
};
#endif
