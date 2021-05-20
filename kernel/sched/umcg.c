// SPDX-License-Identifier: GPL-2.0-only

/*
 * User Managed Concurrency Groups (UMCG).
 */

#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/umcg.h>

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
	return -ENOSYS;
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
	return -ENOSYS;
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
	return -ENOSYS;
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
	return -ENOSYS;
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
	return -ENOSYS;
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
	return -ENOSYS;
}
