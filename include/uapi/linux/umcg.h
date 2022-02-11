#ifndef _UAPI_LINUX_UMCG_H
#define _UAPI_LINUX_UMCG_H

#include <linux/types.h>

/*
 * UMCG: User Managed Concurrency Groups.
 *
 * Syscalls, documented below and implemented in kernel/sched/umcg.c:
 *      sys_umcg_wait()  - wait/wake/context-switch;
 *      sys_umcg_kick()  - prod a UMCG task.
 *
 * UMCG workers have the following internal states:
 *
 *      .-----------------------.
 *      |                       |
 *      |                       v
 *   RUNNING --> BLOCKED --> RUNNABLE
 *      ^                       |
 *      |                       |
 *      .-----------------------.
 *
 *  RUNNING -> BLOCKED  transition happens when the worker blocks in the
 *                      kernel in I/O, pagefault, futex, etc.
 *                      UMCG_WORKER_BLOCK event will be delivered
 *                      to the worker's server
 *
 *  RUNNING -> RUNNABLE transition happens when the worker calls
 *                      sys_umcg_wait() (UMCG_WORKER_WAIT event) or
 *                      when the worker is preempted via sys_umcg_kick()
 *                      (UMCG_WORKER_PREEMPT event)
 *
 *  RUNNABLE -> RUNNING transition happens when the worker is "scheduled"
 *                      by a server via sys_umcg_wait() (no events are
 *                      delivered to the server in this case)
 *
 * Note that umcg_kick() can race with the worker calling a blocking
 * syscall; in this case the worker enters BLOCKED state, and both
 * BLOCK and PREEMPT events are delivered to the server.
 *
 * So the high-level usage pattern is like this:
 * servers:
 *  // server loop
 *     bool start = true;
 *     struct umcg_worker_event *events = malloc(...);
 *
 *     while (!stop) {
 *         pid_t next_worker = 0;
 *
 *         int ret = sys_umcg_wait(start ? UMCG_NEW_SERVER : 0, 0 ,
 *                                 0, events, event_sz);
 *         start = false;
 *
 *         if (ret > 0)
 *             next_worker = scheduler_process_events(events, ret);
 *         if (next_worker)
 *             ret = sys_umcg_wait(0, next_worker, 0, events, event_sz);
 *     }
 *
 * Workers will start by calling
 *     sys_umcg_wait(UMCG_NEW_WORKER, 0, 0, NULL, worker_id);
 * and then potentially yielding by calling
 *     sys_umcg_wait(0, 0, 0, NULL, 0);
 * or cooperatively context-switching by calling
 *     sys_umcg_wait(0, next_worker_tid, 0, NULL, 0).
 *
 * See below for more details.
 */

/**
 * enum umcg_event_type - types of worker events delivered to UMCG servers
 * @UMCG_WORKER_BLOCK:      the worker blocked in kernel in any way
 *                          (e.g. I/O, pagefault, futex, etc.) other than
 *                          in sys_umcg_wait()
 * @UMCG_WORKER_WAKE:       the worker blocking operation, previously
 *                          indicated by @UMCG_WORKER_BLOCK, has
 *                          completed, and the worker can now be "scheduled"
 * @UMCG_WORKER_PREEMPT:    the worker has been preempted via umcg_kick
 *                          note: can race with BLOCK, i.e. a running
 *                          worker generate a combined BLOCK | PREEMPT
 *                          event
 * @UMCG_WORKER_WAIT:       the worker blocked in kernel by calling
 *                          sys_umcg_wait()
 * @UMCG_WORKER_EXIT:       the worker thread exited or unregistered
 *
 */
enum umcg_event_type {
	UMCG_WORKER_BLOCK	= 0x0001,
	UMCG_WORKER_WAKE	= 0x0002,
	UMCG_WORKER_PREEMPT	= 0x0004,
	UMCG_WORKER_WAIT	= 0x0008,
	UMCG_WORKER_EXIT	= 0x0010,
};

/**
 * struct umcg_worker_event - indicates one or more worker state transitions.
 * @worker_id:          the ID of the worker (se sys_umcg_wait())
 * @worker_event_type:  ORed values from umcg_event_type
 * @counter:            a monotonically increasing wraparound counter,
 *                      per worker,of events delivered to the userspace;
 *                      if the event represents several distinct events (ORed), the
 *                      counter will reflect that number (e.g. if
 *                      @worker_event_type == BLOCK | WAKE, the counter
 *                      will increment by 2).
 *
 * Worker events are delivered to UMCG servers upon their return from
 * sys_umcg_wait().
 */
struct umcg_worker_event {
	u64	worker_id;
	u32	worker_event_type;
	u32	counter;
	/* maybe instead of @counter there should be a @timestamp or two? */
} __attribute__((packed, aligned(64)));

/**
 * enum umcg_wait_flag - flags to pass to sys_umcg_wait
 * @UMCG_NEW_WORKER:     register the current task as a UMCG worker
 * @UMCG_NEW_SERVER:     register the current task as a UMCG server
 * @UMCG_UNREGISTER:     unregister the current task as a UMCG task
 *
 *
 * @UMCG_CLOCK_REALTIME: treat @abs_timeout as realtime clock value
 * @UMCG_CLOCK_TAI:      treat @abs_timeout as TAI clock value
 *                       (default: treat @abs_timeout as MONOTONIC clock value)
 */
enum umcg_wait_flag {
	UMCG_NEW_WORKER		= 0x00001,
	UMCG_NEW_SERVER		= 0x00002,
	UMCG_UNREGISTER		= 0x00004,

	UMCG_CLOCK_REALTIME	= 0x10000,
	UMCG_CLOCK_TAI		= 0x20000,
};

/*
 * int sys_umcg_wait(u64 flags, pid_t next_tid, u64 abs_timeout,
 *                   struct umcg_worker_event __user *events,
 *                   u64 event_sz_or_worker_id);
 *
 * sys_umcg_wait() context switches, synchronously on-CPU if possible,
 *                 from the currently running thread to @next_tid; also
 *                 @events is used to deliver worker events to servers.
 *
 * @flags:         ORed values from enum umcg_wait_flag.
 *                 - UMCG_NEW_WORKER    : register the current thread
 *                                        as a new UMCG worker;
 *                 - UMCG_NEW_SERVER    : register the current thread
 *                                        as a new UMCG server;
 *                 - UMCG_UNREGISTER    : unregister the current thread
 *                                        as a UMCG task; will not block;
 *                                        all other parameters must be zeroes.
 *
 *                                        if the current thread is a worker,
 *                                        UMCG_WORKER_EXIT event will be
 *                                        delivered to its server;
 *
 *                if @abs_timeout is not zero, @flags may contain one of the
 *                UMCG_CLOCK_XXX bits to indicate which clock to use; if
 *                none of the CLOCK bits are set, the MONOTONIC clock is used;
 *
 * @next_tid: tid of the UMCG task to context switch to;
 *
 *         if the current thread is a server, @next_tid must be either
 *         that of a worker, or zero; if @next_tid is a worker, and there
 *         are no events waiting for this server, sys_umcg_wait() will
 *         context switch to the worker; if there _are_ events, sys_umcg_wait()
 *         will wake the worker and immediately return with @events
 *         populated;
 *         if the current thread is a server, and @next_tid is zero,
 *         sys_umcg_wait() will block until there are worker events for
 *         for this server to consume, or sys_umcg_kick() is called (or
 *         timeout exipired);
 *
 *         if the current thread is a worker, sys_umcg_wait() will block;
 *         if @next_tid is zero, UMCG_WORKER_WAIT event will be delivered
 *         to the worker's server; if @next_tid is a RUNNABLE worker,
 *         sys_umcg_wait() will context-switch to that worker, without
 *         any events generated;
 *
 *         Note: if a worker calls sys_umcg_wait() with @next_tid as zero,
 *               its server should be woken so that it can schedule another
 *               worker in place of the waiting worker; if the worker
 *               cooperatively context-switches into another worker,
 *               its server does not really need to do anything, so no
 *               new events are generated;
 *
 * @abs_timeout: if not zero, and the current thread is a server,
 *               sys_umcg_wait will wake; if the current thread is a worker,
 *               the worker will remain RUNNABLE, but UMCG_WORKER_WAKE
 *               event will be delivered to its server; in this case
 *               sys_umcg_wait() will return -ETIMEDOUT when the worker
 *               is eventually scheduled by a server;
 *
 * @events: a block of memory that is used to deliver worker events to
 *          their servers; must be NOT NULL if the current thread is
 *          a server; must be NULL if the current thread is a worker;
 *
 * @event_sz_or_worker_id: if the current thread is a server, indicates
 *                         the number of struct umcg_worker_event the @events
 *                         buffer can accommodate;
 *
 *                         if the current thread is a worker, must be
 *                         zero unless UMCG_NEW_WORKER flag is set,
 *                         in which case it must indicate a
 *                         userspace-provided worker ID, usually
 *                         a pointer to a TLS struct holding the worker's
 *                         userspace state;
 *
 *
 * Returns:
 * 0		- Ok;
 * >0		- the number of worker events in @events;
 * -ESRCH	- @next_tid is not a UMCG task;
 * -ETIMEDOUT	- @abs_timeout expired;
 * -EINVAL	- another error;
 */

/*
 * int sys_umcg_kick(u32 flags, pid_t tid) - preempts a running UMCG worker
 *                                           or wakes a sleeping UMCG server.
 *
 * See sys_umcg_wait() for more details.
 *
 * Returns:
 * 0		- Ok;
 * -EAGAIN	- the worker is not running or the server is not sleeping;
 * -ESRCH	- not a related UMCG task;
 * -EINVAL	- another error happened (unknown flags, etc..).
 */

#endif /* _UAPI_LINUX_UMCG_H */
