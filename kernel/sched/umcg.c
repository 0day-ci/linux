// SPDX-License-Identifier: GPL-2.0-only

/*
 * User Managed Concurrency Groups (UMCG).
 *
 */

#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/umcg.h>

/**
 * struct umcg_task - describes a server or a worker
 *
 * Allocated when the task registers (UMCG_NEW_WORKER/UMCG_NEW_SERVER),
 * deallocated when the worker exits or unregisters, and the last event
 * is consumed if worker, or when the server exits or unregisters (without
 * workers).
 */
struct umcg_task {
	/**
	 * @worker: is this a worker or a server.
	 */
	bool	worker;

	/**
	 * @worker_events: list of worker events. Consumed (copied out to
	 *                 sys_umcg_wait()'s @events, with all references
	 *                 NULL-ed) when the server's sys_umcg_wait() returned.
	 *
	 * The server's @worker_events is the head; the workers' @worker_events
	 * are added to their server's list when the event happens; if
	 * multiple events happen for a worker, they are ORed in @worker_event.
	 */
	struct list_head	worker_events;

	/**
	 * @workers: lists all workers belonging to the same server. The
	 *           server's @workers is the head.
	 */
	struct list_head	workers;

	/* fields below are valid only for workers */

	/**
	 * @server: points to the server this worker belongs to. Not NULL.
	 */
	struct task_struct	*server;

	/**
	 * @worker_event: contains worker event(s) to be delivered
	 *                to the worker's server
	 *
	 * @worker_event.worker_id - a constant worker id specified
	 *                           upon worker registration; never
	 *                           changes;
	 *
	 * @worker_event.worker_event_type - ORed values from
	 *                                   enum umcg_event_type;
	 *                                   cleared when the event
	 *                                   is copied out to the server's
	 *                                   @events;
	 *
	 * @worker_event.counter - incremented (wraparound) upon each new
	 *                         event (TBD: maybe have timestamps instead?)
	 */
	struct umcg_worker_event	worker_event;

	/* Maybe we will need a spin lock here. TBD. */
};

void umcg_notify_resume(void)
{
}
void umcg_execve(struct task_struct *tsk)
{
}
void umcg_handle_exit(void)
{
}
void umcg_wq_worker_sleeping(struct task_struct *tsk)
{
}
void umcg_wq_worker_running(struct task_struct *tsk)
{
}

/**
 * sys_umcg_kick: preempts a runnning UMCG worker or wakes a UMCG
 *                server that is sleeping in sys_umcg_wait().
 *
 * Returns:
 * 0		- Ok;
 * -EAGAIN	- the worker is not running or the server is not sleeping;
 * -ESRCH	- not a related UMCG task;
 * -EINVAL	- another error happened (unknown flags, etc..).
 */
SYSCALL_DEFINE2(umcg_kick, u32, flags, pid_t, tid)
{
	return -ENOSYS;
}

SYSCALL_DEFINE5(umcg_wait, u64, flags, pid_t, next_tid, u64, abs_timeout,
		struct umcg_worker_event __user *, events,
		u64, event_sz_or_worker_id)
{
	return -ENOSYS;
}
