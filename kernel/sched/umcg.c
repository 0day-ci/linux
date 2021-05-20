// SPDX-License-Identifier: GPL-2.0-only

/*
 * User Managed Concurrency Groups (UMCG).
 */

#include <linux/freezer.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/umcg.h>

#include "sched.h"
#include "umcg.h"

static int __api_version(u32 requested)
{
	if (requested == 1)
		return 0;

	return 1;
}

/**
 * sys_umcg_api_version - query UMCG API versions that are supported.
 * @api_version:          Requested API version.
 * @flags:                Reserved.
 *
 * Return:
 * 0                    - the @api_version is supported;
 * > 0                  - the maximum supported version of UMCG API if
 *                        the requested version is not supported.
 * -EINVAL              - @flags is not zero.
 *
 * NOTE: the kernel may drop support for older/deprecated API versions,
 * so a return of X does not indicate that any version less than X is
 * supported.
 */
SYSCALL_DEFINE2(umcg_api_version, u32, api_version, u32, flags)
{
	if (flags)
		return -EINVAL;

	return __api_version(api_version);
}

static int get_state(struct umcg_task __user *ut, u32 *state)
{
	return get_user(*state, (u32 __user *)ut);
}

static int put_state(struct umcg_task __user *ut, u32 state)
{
	return put_user(state, (u32 __user *)ut);
}

static int register_core_task(u32 api_version, struct umcg_task __user *umcg_task)
{
	struct umcg_task_data *utd;
	u32 state;

	if (get_state(umcg_task, &state))
		return -EFAULT;

	if (state != UMCG_TASK_NONE)
		return -EINVAL;

	utd = kzalloc(sizeof(struct umcg_task_data), GFP_KERNEL);
	if (!utd)
		return -EINVAL;

	utd->self = current;
	utd->umcg_task = umcg_task;
	utd->task_type = UMCG_TT_CORE;
	utd->api_version = api_version;

	if (put_state(umcg_task, UMCG_TASK_RUNNING)) {
		kfree(utd);
		return -EFAULT;
	}

	task_lock(current);
	rcu_assign_pointer(current->umcg_task_data, utd);
	task_unlock(current);

	return 0;
}

/**
 * sys_umcg_register_task - register the current task as a UMCG task.
 * @api_version:       The expected/desired API version of the syscall.
 * @flags:             One of enum umcg_register_flag.
 * @group_id:          UMCG Group ID. UMCG_NOID for Core tasks.
 * @umcg_task:         The control struct for the current task.
 *                     umcg_task->state must be UMCG_TASK_NONE.
 *
 * Register the current task as a UMCG task. If this is a core UMCG task,
 * the syscall marks it as RUNNING and returns immediately.
 *
 * If this is a UMCG worker, the syscall marks it UNBLOCKED, and proceeds
 * with the normal UNBLOCKED worker logic.
 *
 * If this is a UMCG server, the syscall immediately returns.
 *
 * Return:
 * 0            - Ok;
 * -EOPNOTSUPP  - the API version is not supported;
 * -EINVAL      - one of the parameters is wrong;
 * -EFAULT      - failed to access @umcg_task.
 */
SYSCALL_DEFINE4(umcg_register_task, u32, api_version, u32, flags, u32, group_id,
		struct umcg_task __user *, umcg_task)
{
	if (__api_version(api_version))
		return -EOPNOTSUPP;

	if (rcu_access_pointer(current->umcg_task_data) || !umcg_task)
		return -EINVAL;

	switch (flags) {
	case UMCG_REGISTER_CORE_TASK:
		if (group_id != UMCG_NOID)
			return -EINVAL;
		return register_core_task(api_version, umcg_task);
	default:
		return -EINVAL;
	}
}

/**
 * sys_umcg_unregister_task - unregister the current task as a UMCG task.
 * @flags: reserved.
 *
 * Return:
 * 0       - Ok;
 * -EINVAL - the current task is not a UMCG task.
 */
SYSCALL_DEFINE1(umcg_unregister_task, u32, flags)
{
	struct umcg_task_data *utd;
	int ret = -EINVAL;

	rcu_read_lock();
	utd = rcu_dereference(current->umcg_task_data);

	if (!utd || flags)
		goto out;

	task_lock(current);
	rcu_assign_pointer(current->umcg_task_data, NULL);
	task_unlock(current);

	ret = 0;

out:
	rcu_read_unlock();
	if (!ret && utd) {
		synchronize_rcu();
		kfree(utd);
	}
	return ret;
}

static int do_context_switch(struct task_struct *next)
{
	struct umcg_task_data *utd = rcu_access_pointer(current->umcg_task_data);

	/*
	 * It is important to set_current_state(TASK_INTERRUPTIBLE) before
	 * waking @next, as @next may immediately try to wake current back
	 * (e.g. current is a server, @next is a worker that immediately
	 * blocks or waits), and this next wakeup must not be lost.
	 */
	set_current_state(TASK_INTERRUPTIBLE);

	WRITE_ONCE(utd->in_wait, true);

	if (!try_to_wake_up(next, TASK_NORMAL, WF_CURRENT_CPU))
		return -EAGAIN;

	freezable_schedule();

	WRITE_ONCE(utd->in_wait, false);

	if (signal_pending(current))
		return -EINTR;

	return 0;
}

static int do_wait(void)
{
	struct umcg_task_data *utd = rcu_access_pointer(current->umcg_task_data);

	if (!utd)
		return -EINVAL;

	WRITE_ONCE(utd->in_wait, true);

	set_current_state(TASK_INTERRUPTIBLE);
	freezable_schedule();

	WRITE_ONCE(utd->in_wait, false);

	if (signal_pending(current))
		return -EINTR;

	return 0;
}

/**
 * sys_umcg_wait - block the current task (if all condtions are met).
 * @flags:         Reserved.
 * @timeout:       The absolute timeout of the wait. Not supported yet.
 *                 Must be NULL.
 *
 * Sleep until woken, interrupted, or @timeout expires.
 *
 * Return:
 * 0           - Ok;
 * -EFAULT     - failed to read struct umcg_task assigned to this task
 *               via sys_umcg_register();
 * -EAGAIN     - try again;
 * -EINTR      - signal pending;
 * -EOPNOTSUPP - @timeout != NULL (not supported yet).
 * -EINVAL     - a parameter or a member of struct umcg_task has a wrong value.
 */
SYSCALL_DEFINE2(umcg_wait, u32, flags,
		const struct __kernel_timespec __user *, timeout)
{
	struct umcg_task_data *utd;

	if (flags)
		return -EINVAL;
	if (timeout)
		return -EOPNOTSUPP;

	rcu_read_lock();
	utd = rcu_dereference(current->umcg_task_data);
	if (!utd) {
		rcu_read_unlock();
		return -EINVAL;
	}

	rcu_read_unlock();

	return do_wait();
}

/**
 * sys_umcg_wake - wake @next_tid task blocked in sys_umcg_wait.
 * @flags:         Reserved.
 * @next_tid:      The ID of the task to wake.
 *
 * Wake @next identified by @next_tid. @next must be either a UMCG core
 * task or a UMCG worker task.
 *
 * Return:
 * 0           - Ok;
 * -EFAULT     - failed to read struct umcg_task assigned to next;
 * -ESRCH      - @next_tid did not identify a task;
 * -EAGAIN     - try again;
 * -EINVAL     - a parameter or a member of next->umcg_task has a wrong value.
 */
SYSCALL_DEFINE2(umcg_wake, u32, flags, u32, next_tid)
{
	struct umcg_task_data *next_utd;
	struct task_struct *next;
	int ret = -EINVAL;

	if (!next_tid)
		return -EINVAL;
	if (flags)
		return -EINVAL;

	next = find_get_task_by_vpid(next_tid);
	if (!next)
		return -ESRCH;

	rcu_read_lock();
	next_utd = rcu_dereference(next->umcg_task_data);
	if (!next_utd)
		goto out;

	if (!READ_ONCE(next_utd->in_wait)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = wake_up_process(next);
	put_task_struct(next);
	if (ret)
		ret = 0;
	else
		ret = -EAGAIN;

out:
	rcu_read_unlock();
	return ret;
}

/**
 * sys_umcg_swap - wake next, put current to sleep.
 * @wake_flags:    Reserved.
 * @next_tid:      The ID of the task to wake.
 * @wait_flags:    Reserved.
 * @timeout:       The absolute timeout of the wait. Not supported yet.
 *
 * sys_umcg_swap() is semantically equivalent to this code fragment:
 *
 *     ret = sys_umcg_wake(wake_flags, next_tid);
 *     if (ret)
 *             return ret;
 *     return sys_umcg_wait(wait_flags, timeout);
 *
 * The function attempts to wake @next on the current CPU.
 *
 * The current and the next tasks must both be either UMCG core tasks,
 * or two UMCG workers belonging to the same UMCG group. In the latter
 * case the UMCG server task that is "running" the current task will
 * be transferred to the next task.
 *
 * Return: see the code snippet above.
 */
SYSCALL_DEFINE4(umcg_swap, u32, wake_flags, u32, next_tid, u32, wait_flags,
		const struct __kernel_timespec __user *, timeout)
{
	struct umcg_task_data *curr_utd;
	struct umcg_task_data *next_utd;
	struct task_struct *next;
	int ret = -EINVAL;

	rcu_read_lock();
	curr_utd = rcu_dereference(current->umcg_task_data);

	if (!next_tid || wake_flags || wait_flags || !curr_utd)
		goto out;

	if (timeout) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	next = find_get_task_by_vpid(next_tid);
	if (!next) {
		ret = -ESRCH;
		goto out;
	}

	next_utd = rcu_dereference(next->umcg_task_data);
	if (!next_utd) {
		ret = -EINVAL;
		goto out;
	}

	if (!READ_ONCE(next_utd->in_wait)) {
		ret = -EAGAIN;
		goto out;
	}

	rcu_read_unlock();

	return do_context_switch(next);

out:
	rcu_read_unlock();
	return ret;
}
