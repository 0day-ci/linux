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

static int32_t sys_umcg_create_group(uint32_t api_version, uint32_t flags)
{
	return syscall(__NR_umcg_create_group, api_version, flags);
}

static int sys_umcg_destroy_group(int32_t group_id)
{
	return syscall(__NR_umcg_destroy_group, group_id);
}

static int sys_umcg_poll_worker(uint32_t flags, struct umcg_task **ut)
{
	return syscall(__NR_umcg_poll_worker, flags, ut);
}

static int sys_umcg_run_worker(uint32_t flags, uint32_t worker_tid,
		struct umcg_task **ut)
{
	return syscall(__NR_umcg_run_worker, flags, worker_tid, ut);
}

typedef intptr_t umcg_t;   /* UMCG group ID. */
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
 * umcg_register_worker - register the current thread as a UMCG worker
 * @group_id:      The ID of the UMCG group the thread should join.
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the thread to be used with UMCG API (guaranteed
 *                 to match the value returned by umcg_get_utid).
 */
umcg_tid umcg_register_worker(umcg_t group_id, intptr_t tag);

/**
 * umcg_register_server - register the current thread as a UMCG server
 * @group_id:      The ID of the UMCG group the thread should join.
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the thread to be used with UMCG API (guaranteed
 *                 to match the value returned by umcg_get_utid).
 */
umcg_tid umcg_register_server(umcg_t group_id, intptr_t tag);

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

/**
 * umcg_create_group - create a UMCG group
 * @flags:             Reserved.
 *
 * UMCG groups have worker and server threads.
 *
 * Worker threads are either RUNNABLE/RUNNING "on behalf" of server threads
 * (see umcg_run_worker), or are BLOCKED/UNBLOCKED. A worker thread can be
 * running only if it is attached to a server thread (interrupts can
 * complicate the matter - TBD).
 *
 * Server threads are either blocked while running worker threads or are
 * blocked waiting for available (=UNBLOCKED) workers. A server thread
 * can "run" only one worker thread.
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the group, to be used in e.g. umcg_register.
 */
umcg_t umcg_create_group(uint32_t flags);

/**
 * umcg_destroy_group - destroy a UMCG group
 * @umcg:               ID of the group to destroy
 *
 * The group must be empty (no server or worker threads).
 *
 * Return:
 * 0            - Ok
 * -1           - an error occurred. Check errno.
 *                errno == EAGAIN: the group has server or worker threads
 */
int umcg_destroy_group(umcg_t umcg);

/**
 * umcg_poll_worker - wait for the first available UNBLOCKED worker
 *
 * The current thread must be a UMCG server. If there is a list/queue of
 * waiting UNBLOCKED workers in the server's group, umcg_poll_worker
 * picks the longest waiting one; if there are no UNBLOCKED workers, the
 * current thread sleeps in the polling queue.
 *
 * Return:
 * UMCG_NONE         - an error occurred; check errno;
 * != UMCG_NONE      - a RUNNABLE worker.
 */
umcg_tid umcg_poll_worker(void);

/**
 * umcg_run_worker - run @worker as a UMCG server
 * @worker:          the ID of a RUNNABLE worker to run
 *
 * The current thread must be a UMCG "server".
 *
 * Return:
 * UMCG_NONE    - if errno == 0, the last worker the server was running
 *                unregistered itself; if errno != 0, an error occurred
 * != UMCG_NONE - the ID of the last worker the server was running before
 *                the worker was blocked or preempted.
 */
umcg_tid umcg_run_worker(umcg_tid worker);

uint32_t umcg_get_task_state(umcg_tid task);

#endif  /* __LIBUMCG_H */
