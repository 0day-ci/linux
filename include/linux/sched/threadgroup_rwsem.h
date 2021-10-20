/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_THREADGROUP_RWSEM_H
#define _LINUX_SCHED_THREADGROUP_RWSEM_H

#ifdef CONFIG_THREADGROUP_RWSEM
/* including before task_struct definition causes dependency loop */
#include <linux/percpu-rwsem.h>

extern struct percpu_rw_semaphore threadgroup_rwsem;

/**
 * threadgroup_change_begin - mark the beginning of changes to a threadgroup
 * @tsk: task causing the changes
 *
 * All operations which modify a threadgroup - a new thread joining the group,
 * death of a member thread (the assertion of PF_EXITING) and exec(2)
 * dethreading the process and replacing the leader - read-locks
 * threadgroup_rwsem so that write-locking stabilizes thread groups.
 */
static inline void threadgroup_change_begin(struct task_struct *tsk)
{
	percpu_down_read(&threadgroup_rwsem);
}

/**
 * threadgroup_change_end - mark the end of changes to a threadgroup
 * @tsk: task causing the changes
 *
 * See threadgroup_change_begin().
 */
static inline void threadgroup_change_end(struct task_struct *tsk)
{
	percpu_up_read(&threadgroup_rwsem);
}
#else
static inline void threadgroup_change_begin(struct task_struct *tsk)
{
	might_sleep();
}

static inline void threadgroup_change_end(struct task_struct *tsk)
{
}
#endif

#endif
