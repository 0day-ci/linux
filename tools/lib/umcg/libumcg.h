/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LIBUMCG_H
#define __LIBUMCG_H

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <linux/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>

#include <linux/umcg.h>

static int sys_umcg_api_version(uint32_t requested_api_version, uint32_t flags)
{
	return syscall(__NR_umcg_api_version, requested_api_version, flags);
}

static int sys_umcg_register_task(uint32_t api_version, uint32_t flags,
		uint32_t group_id, struct umcg_task *umcg_task)
{
	return syscall(__NR_umcg_register_task, api_version, flags, group_id,
			umcg_task);
}

static int sys_umcg_unregister_task(uint32_t flags)
{
	return syscall(__NR_umcg_unregister_task, flags);
}

static int sys_umcg_wait(uint32_t flags, const struct timespec *timeout)
{
	return syscall(__NR_umcg_wait, flags, timeout);
}

static int sys_umcg_wake(uint32_t flags, uint32_t next_tid)
{
	return syscall(__NR_umcg_wake, flags, next_tid);
}

static int sys_umcg_swap(uint32_t wake_flags, uint32_t next_tid,
		uint32_t wait_flags, const struct timespec *timeout)
{
	return syscall(__NR_umcg_swap, wake_flags, next_tid,
			wait_flags, timeout);
}

typedef intptr_t umcg_tid; /* UMCG thread ID. */

#define UMCG_NONE	(0)

/**
 * umcg_get_utid - return the UMCG ID of the current thread.
 *
 * The function always succeeds, and the returned ID is guaranteed to be
 * stable over the life of the thread (and multiple
 * umcg_register/umcg_unregister calls).
 *
 * The ID is NOT guaranteed to be unique over the life of the process.
 */
umcg_tid umcg_get_utid(void);

/**
 * umcg_set_task_tag - add an arbitrary tag to a registered UMCG task.
 *
 * Note: non-thread-safe: the user is responsible for proper memory fencing.
 */
void umcg_set_task_tag(umcg_tid utid, intptr_t tag);

/*
 * umcg_get_task_tag - get the task tag. Returns zero if none set.
 *
 * Note: non-thread-safe: the user is responsible for proper memory fencing.
 */
intptr_t umcg_get_task_tag(umcg_tid utid);

/**
 * umcg_register_core_task - register the current thread as a UMCG core task
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the thread to be used with UMCG API (guaranteed
 *                 to match the value returned by umcg_get_utid).
 */
umcg_tid umcg_register_core_task(intptr_t tag);

/**
 * umcg_unregister_task - unregister the current thread
 *
 * Return:
 * 0              - OK
 * -1             - the current thread is not a UMCG thread
 */
int umcg_unregister_task(void);

/**
 * umcg_wait - block the current thread
 * @timeout:   absolute timeout (not supported at the moment)
 *
 * Blocks the current thread, which must have been registered via umcg_register,
 * until it is waken via umcg_wake or swapped into via umcg_swap. If the current
 * thread has a wakeup queued (see umcg_wake), returns zero immediately,
 * consuming the wakeup.
 *
 * Return:
 * 0         - OK, the thread was waken;
 * -1        - did not wake normally;
 *               errno:
 *                 EINTR: interrupted
 *                 EINVAL: some other error occurred
 */
int umcg_wait(const struct timespec *timeout);

/**
 * umcg_wake - wake @next
 * @next:      ID of the thread to wake (IDs are returned by umcg_register).
 *
 * If @next is blocked via umcg_wait, or umcg_swap, wake it. If @next is
 * running, queue the wakeup, so that a future block of @next will consume
 * the wakeup but will not block.
 *
 * umcg_wake is non-blocking, but may retry a few times to make sure @next
 * has indeed woken.
 *
 * umcg_wake can queue at most one wakeup; if @next has a wakeup queued,
 * an error is returned.
 *
 * Return:
 * 0         - OK, @next has woken, or a wakeup has been queued;
 * -1        - an error occurred.
 */
int umcg_wake(umcg_tid next);

/**
 * umcg_swap - wake @next, put the current thread to sleep
 * @next:      ID of the thread to wake
 * @timeout:   absolute timeout (not supported at the moment)
 *
 * umcg_swap is semantically equivalent to
 *
 *     int ret = umcg_wake(next);
 *     if (ret)
 *             return ret;
 *     return umcg_wait(timeout);
 *
 * but may do a synchronous context switch into @next on the current CPU.
 */
int umcg_swap(umcg_tid next, const struct timespec *timeout);

#endif  /* __LIBUMCG_H */
