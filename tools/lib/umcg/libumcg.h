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

/*
 * UMCG: User Managed Concurrency Groups.
 *
 * LIBUMCG provides userspace UMCG API that hides some of the intricacies
 * of sys_umcg_ctl() and sys_umcg_wait() syscalls.
 *
 * Note that this API is still quite low level and is designed as
 * a toolkit for building higher-level userspace schedulers.
 *
 * See tools/lib/umcg/libumcg.txt for detals.
 */

typedef intptr_t umcg_t;   /* UMCG group ID. */
typedef intptr_t umcg_tid; /* UMCG thread ID. */

#define UMCG_NONE	(0)

/**
 * umcg_enabled - indicates whether UMCG syscalls are available.
 */
bool umcg_enabled(void);

/**
 * umcg_get_utid - return the UMCG ID of the current thread.
 *
 * The function always succeeds, and the returned ID is guaranteed to be
 * stable over the life of the thread.
 *
 * The ID is NOT guaranteed to be unique over the life of the process.
 */
umcg_tid umcg_get_utid(void);

/**
 * umcg_set_task_tag - add an arbitrary tag to a registered UMCG task.
 *
 * Note: not-thread-safe: the user is responsible for proper memory fencing.
 */
void umcg_set_task_tag(umcg_tid utid, intptr_t tag);

/**
 * umcg_get_task_tag - get the task tag. Returns zero if none set.
 *
 * Note: not-thread-safe: the user is responsible for proper memory fencing.
 */
intptr_t umcg_get_task_tag(umcg_tid utid);

/**
 * enum umcg_create_group_flag - flags to pass to umcg_create_group
 * @UMCG_GROUP_ENABLE_PREEMPTION: enable worker preemption.
 *
 * See tools/lib/libumcg.txt for detals.
 */
enum umcg_create_group_flag {
	UMCG_GROUP_ENABLE_PREEMPTION	= 1
};

/**
 * umcg_create_group - create a UMCG group
 * @flags:             a combination of values from enum umcg_create_group_flag
 *
 * See tools/lib/libumcg.txt for detals.
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
 * umcg_register_basic_task - register the current thread as a UMCG basic task
 * @tag:          An arbitrary tag to be associated with the task.
 *
 * See tools/lib/libumcg.txt for detals.
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the thread to be used with UMCG API (guaranteed
 *                 to match the value returned by umcg_get_utid).
 */
umcg_tid umcg_register_basic_task(intptr_t tag);

/**
 * umcg_register_worker - register the current thread as a UMCG worker
 * @group_id:      The ID of the UMCG group the thread should join;
 * @tag:           an arbitrary tag to be associated with the task.
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the thread to be used with UMCG API (guaranteed
 *                 to match the value returned by umcg_get_utid).
 */
umcg_tid umcg_register_worker(umcg_t group_id, intptr_t tag);

/**
 * umcg_register_server - register the current thread as a UMCG server
 * @group_id:      The ID of the UMCG group the thread should join;
 * @tag:           an arbitrary tag to be associated with the task.
 *
 * Return:
 * UMCG_NONE     - an error occurred. Check errno.
 * != UMCG_NONE  - the ID of the thread to be used with UMCG API (guaranteed
 *                 to match the value returned by umcg_get_utid).
 */
umcg_tid umcg_register_server(umcg_t group_id, intptr_t tag);

/**
 * umcg_unregister_task - unregister the current thread.
 *
 * Return:
 * 0              - OK
 * -1             - the current thread is not a UMCG thread
 */
int umcg_unregister_task(void);

/**
 * umcg_wait - block the current thread
 * @timeout:   absolute timeout in nanoseconds
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
int umcg_wait(uint64_t timeout);

/**
 * umcg_wake - wake @next; non-blocking.
 * @next:            ID of the thread to wake;
 * @wf_current_cpu:  an advisory hint indicating that the current thread
 *                   is going to block in the immediate future and that
 *                   the wakee should be woken on the current CPU;
 *
 * If @next is blocked via umcg_wait or umcg_swap, wake it if @next is
 * a server or a basic task; if @next is a worker, it will be queued
 * in the idle worker list. If @next is running, queue the wakeup,
 * so that a future block of @next will consume the wakeup and will not block.
 *
 * umcg_wake can queue at most one wakeup; if waking or queueing a wakeup
 * is not possible, umcg_wake will SPIN.
 *
 * See tools/lib/umcg/libumcg.txt for detals.
 *
 * Return:
 * 0         - OK, @next has woken, or a wakeup has been queued;
 * -1        - an error occurred.
 */
int umcg_wake(umcg_tid next, bool wf_current_cpu);

/**
 * umcg_swap - wake @next, put the current thread to sleep
 * @next:      ID of the thread to wake
 * @timeout:   absolute timeout in ns
 *
 * umcg_swap is semantically equivalent to
 *
 *     int ret = umcg_wake(next, true);
 *     if (ret)
 *             return ret;
 *     return umcg_wait(timeout);
 *
 * but may do a synchronous context switch into @next on the current CPU.
 *
 * Note: if @next is a worker, it must be IDLE, but not in the idle worker list.
 * See tools/lib/umcg/libumcg.txt for detals.
 */
int umcg_swap(umcg_tid next, u64 timeout);

/**
 * umcg_get_idle_worker - get an idle worker, if available
 * @wait: if true, block until an idle worker becomes available
 *
 * The current thread must be a UMCG server. If there is a list/queue of
 * waiting IDLE workers in the server's group, umcg_get_idle_worker
 * picks one; if there are no IDLE workers, the current thread sleeps in
 * the idle server queue if @wait is true.
 *
 * Note: servers waiting for idle workers must NOT be woken via umcg_wake(),
 *       as this will leave them in inconsistent state.
 *
 * See tools/lib/umcg/libumcg.txt for detals.
 *
 * Return:
 * UMCG_NONE         - an error occurred; check errno;
 * != UMCG_NONE      - a RUNNABLE worker.
 */
umcg_tid umcg_get_idle_worker(bool wait);

/**
 * umcg_run_worker - run @worker as a UMCG server
 * @worker:          the ID of a RUNNABLE worker to run
 *
 * The current thread must be a UMCG "server".
 *
 * See tools/lib/umcg/libumcg.txt for detals.
 *
 * Return:
 * UMCG_NONE    - if errno == 0, the last worker the server was running
 *                unregistered itself; if errno != 0, an error occurred
 * != UMCG_NONE - the ID of the last worker the server was running before
 *                the worker was blocked or preempted.
 */
umcg_tid umcg_run_worker(umcg_tid worker);

/**
 * umcg_preempt_worker - preempt a RUNNING worker.
 * @worker:          the ID of a RUNNING worker to preempt.
 *
 * See tools/lib/umcg/libumcg.txt for detals.
 *
 * Return:
 * 0        - Ok;
 * -1       - an error occurred; check errno and `man tgkill()`. In addition
 *            to tgkill() errors, EAGAIN is also returned if the worker
 *            is not in RUNNING state (in this case tgkill() was not called).
 */
int umcg_preempt_worker(umcg_tid worker);

/**
 * umcg_get_task_state - return the UMCG state of @task, including state
 * flags, without the timestamp.
 *
 * Note that in most situations the state value can be changed at any time
 * by a concurrent thread, so this function is exposed for debugging/testing
 * purposes only.
 */
uint64_t umcg_get_task_state(umcg_tid task);

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC	1000000000L
#endif

/**
 * umcg_get_time_ns - returns the absolute current time in nanoseconds.
 *
 * The function uses CLOCK_MONOTONIC; the returned value can be used
 * to set absolute timeouts for umcg_wait() and umcg_swap().
 */
uint64_t umcg_get_time_ns(void);

/**
 * UMCG userspace-only task state flag: wakeup queued.
 *
 * see umcg_wake() above.
 */
#define UMCG_UTF_WAKEUP_QUEUED	(1ULL << 17)

/**
 * UMCG userspace-only task state flag: worker in sys_umcg_wait().
 *
 * IDLE workers can be in two substates:
 * - waiting in sys_umcg_wait(): in this case UTF_WORKER_IN_WAIT flag is set;
 * - waiting in the idle worker list: in this case the flag is not set.
 *
 * If the worker is IDLE in sys_umcg_wait, umcg_wake() clears the flag
 * and adds the worker to the idle worker list.
 *
 * If the worker is IDLE in the idle worker list, umcg_wake() sets
 * the wakeup queued flag.
 */
#define UMCG_UTF_WORKER_IN_WAIT	(1ULL << 16)

#endif  /* __LIBUMCG_H */
