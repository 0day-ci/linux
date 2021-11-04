// SPDX-License-Identifier: GPL-2.0-only

/*
 * User Managed Concurrency Groups (UMCG).
 *
 * See Documentation/userspace-api/umcg.txt for detals.
 */

#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/umcg.h>

#include "sched.h"

/**
 * get_user_nofault - get user value without sleeping.
 *
 * get_user() might sleep and therefore cannot be used in preempt-disabled
 * regions.
 */
#define get_user_nofault(out, uaddr)			\
({							\
	int ret = -EFAULT;				\
							\
	if (access_ok((uaddr), sizeof(*(uaddr)))) {	\
		pagefault_disable();			\
							\
		if (!__get_user((out), (uaddr)))	\
			ret = 0;			\
							\
		pagefault_enable();			\
	}						\
	ret;						\
})

/**
 * umcg_pin_pages: pin pages containing struct umcg_task of this worker
 *                 and its server.
 *
 * The pages are pinned when the worker exits to the userspace and unpinned
 * when the worker is in sched_submit_work(), i.e. when the worker is
 * about to be removed from its runqueue. Thus at most NR_CPUS UMCG pages
 * are pinned at any one time across the whole system.
 *
 * The pinning is needed so that going-to-sleep workers can access
 * their and their servers' userspace umcg_task structs without page faults,
 * as the code path can be executed in the context of a pagefault, with
 * mm lock held.
 */
static int umcg_pin_pages(u32 server_tid)
{
	struct umcg_task __user *worker_ut = current->umcg_task;
	struct umcg_task __user *server_ut = NULL;
	struct task_struct *tsk;

	rcu_read_lock();
	tsk = find_task_by_vpid(server_tid);
	/* Server/worker interaction is allowed only within the same mm. */
	if (tsk && current->mm == tsk->mm)
		server_ut = READ_ONCE(tsk->umcg_task);
	rcu_read_unlock();

	if (!server_ut)
		return -EINVAL;

	tsk = current;

	/* worker_ut is stable, don't need to repin */
	if (!tsk->pinned_umcg_worker_page)
		if (1 != pin_user_pages_fast((unsigned long)worker_ut, 1, 0,
					&tsk->pinned_umcg_worker_page))
			return -EFAULT;

	/* server_ut may change, need to repin */
	if (tsk->pinned_umcg_server_page) {
		unpin_user_page(tsk->pinned_umcg_server_page);
		tsk->pinned_umcg_server_page = NULL;
	}

	if (1 != pin_user_pages_fast((unsigned long)server_ut, 1, 0,
				&tsk->pinned_umcg_server_page))
		return -EFAULT;

	return 0;
}

static void umcg_unpin_pages(void)
{
	struct task_struct *tsk = current;

	if (tsk->pinned_umcg_worker_page)
		unpin_user_page(tsk->pinned_umcg_worker_page);
	if (tsk->pinned_umcg_server_page)
		unpin_user_page(tsk->pinned_umcg_server_page);

	tsk->pinned_umcg_worker_page = NULL;
	tsk->pinned_umcg_server_page = NULL;
}

static void umcg_clear_task(struct task_struct *tsk)
{
	/*
	 * This is either called for the current task, or for a newly forked
	 * task that is not yet running, so we don't need strict atomicity
	 * below.
	 */
	if (tsk->umcg_task) {
		WRITE_ONCE(tsk->umcg_task, NULL);

		/* These can be simple writes - see the commment above. */
		tsk->pinned_umcg_worker_page = NULL;
		tsk->pinned_umcg_server_page = NULL;
		tsk->flags &= ~PF_UMCG_WORKER;
	}
}

/* Called for a forked or execve-ed child. */
void umcg_clear_child(struct task_struct *tsk)
{
	umcg_clear_task(tsk);
}

/* Called both by normally (unregister) and abnormally exiting workers. */
void umcg_handle_exiting_worker(void)
{
	umcg_unpin_pages();
	umcg_clear_task(current);
}

/**
 * umcg_update_state: atomically update umcg_task.state_ts, set new timestamp.
 * @state_ts   - points to the state_ts member of struct umcg_task to update;
 * @expected   - the expected value of state_ts, including the timestamp;
 * @desired    - the desired value of state_ts, state part only;
 * @may_fault  - whether to use normal or _nofault cmpxchg.
 *
 * The function is basically cmpxchg(state_ts, expected, desired), with extra
 * code to set the timestamp in @desired.
 */
static int umcg_update_state(u64 __user *state_ts, u64 *expected, u64 desired,
				bool may_fault)
{
	u64 curr_ts = (*expected) >> (64 - UMCG_STATE_TIMESTAMP_BITS);
	u64 next_ts = ktime_get_ns() >> UMCG_STATE_TIMESTAMP_GRANULARITY;

	/* Cut higher order bits. */
	next_ts &= UMCG_TASK_STATE_MASK_FULL;

	if (next_ts == curr_ts)
		++next_ts;

	/* Remove an old timestamp, if any. */
	desired &= UMCG_TASK_STATE_MASK_FULL;

	/* Set the new timestamp. */
	desired |= (next_ts << (64 - UMCG_STATE_TIMESTAMP_BITS));

	if (may_fault)
		return cmpxchg_user_64(state_ts, expected, desired);

	return cmpxchg_user_64_nofault(state_ts, expected, desired);
}

/**
 * sys_umcg_ctl: (un)register the current task as a UMCG task.
 * @flags:       ORed values from enum umcg_ctl_flag; see below;
 * @self:        a pointer to struct umcg_task that describes this
 *               task and governs the behavior of sys_umcg_wait if
 *               registering; must be NULL if unregistering.
 *
 * @flags & UMCG_CTL_REGISTER: register a UMCG task:
 *         UMCG workers:
 *              - @flags & UMCG_CTL_WORKER
 *              - self->state must be UMCG_TASK_BLOCKED
 *         UMCG servers:
 *              - !(@flags & UMCG_CTL_WORKER)
 *              - self->state must be UMCG_TASK_RUNNING
 *
 *         All tasks:
 *              - self->next_tid must be zero
 *
 *         If the conditions above are met, sys_umcg_ctl() immediately returns
 *         if the registered task is a server; a worker will be added to
 *         idle_workers_ptr, and the worker put to sleep; an idle server
 *         from idle_server_tid_ptr will be woken, if present.
 *
 * @flags == UMCG_CTL_UNREGISTER: unregister a UMCG task. If the current task
 *           is a UMCG worker, the userspace is responsible for waking its
 *           server (before or after calling sys_umcg_ctl).
 *
 * Return:
 * 0                - success
 * -EFAULT          - failed to read @self
 * -EINVAL          - some other error occurred
 */
SYSCALL_DEFINE2(umcg_ctl, u32, flags, struct umcg_task __user *, self)
{
	struct umcg_task ut;

	if (flags == UMCG_CTL_UNREGISTER) {
		if (self || !current->umcg_task)
			return -EINVAL;

		if (current->flags & PF_UMCG_WORKER)
			umcg_handle_exiting_worker();
		else
			umcg_clear_task(current);

		return 0;
	}

	if (!(flags & UMCG_CTL_REGISTER))
		return -EINVAL;

	flags &= ~UMCG_CTL_REGISTER;
	if (flags && flags != UMCG_CTL_WORKER)
		return -EINVAL;

	if (current->umcg_task || !self)
		return -EINVAL;

	if (copy_from_user(&ut, self, sizeof(ut)))
		return -EFAULT;

	if (ut.next_tid)
		return -EINVAL;

	if (flags == UMCG_CTL_WORKER) {
		if ((ut.state_ts & UMCG_TASK_STATE_MASK_FULL) != UMCG_TASK_BLOCKED)
			return -EINVAL;

		WRITE_ONCE(current->umcg_task, self);
		current->flags |= PF_UMCG_WORKER;

		/* Trigger umcg_handle_resuming_worker() */
		set_tsk_thread_flag(current, TIF_NOTIFY_RESUME);
	} else {
		if ((ut.state_ts & UMCG_TASK_STATE_MASK_FULL) != UMCG_TASK_RUNNING)
			return -EINVAL;

		WRITE_ONCE(current->umcg_task, self);
	}

	return 0;
}

/**
 * handle_timedout_worker - make sure the worker is added to idle_workers
 *                          upon a "clean" timeout.
 */
static int handle_timedout_worker(struct umcg_task __user *self)
{
	u64 curr_state, next_state;
	int ret;

	if (get_user(curr_state, &self->state_ts))
		return -EFAULT;

	if ((curr_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_IDLE) {
		/* TODO: should we care here about TF_LOCKED or TF_PREEMPTED? */

		next_state = curr_state & ~UMCG_TASK_STATE_MASK;
		next_state |= UMCG_TASK_BLOCKED;

		ret = umcg_update_state(&self->state_ts, &curr_state, next_state, true);
		if (ret)
			return ret;

		return -ETIMEDOUT;
	}

	return 0;  /* Not really timed out. */
}

/*
 * umcg_should_idle - return true if tasks with @state should block in
 *                    imcg_idle_loop().
 */
static bool umcg_should_idle(u64 state)
{
	switch (state & UMCG_TASK_STATE_MASK) {
	case UMCG_TASK_RUNNING:
		return state & UMCG_TF_LOCKED;
	case UMCG_TASK_IDLE:
		return !(state & UMCG_TF_LOCKED);
	case UMCG_TASK_BLOCKED:
		return false;
	default:
		WARN_ONCE(true, "unknown UMCG task state");
		return false;
	}
}

/**
 * umcg_idle_loop - sleep until !umcg_should_idle() or a timeout expires
 * @abs_timeout - absolute timeout in nanoseconds; zero => no timeout
 *
 * The function marks the current task as INTERRUPTIBLE and calls
 * freezable_schedule().
 *
 * Note: because UMCG workers should not be running WITHOUT attached servers,
 *       and because servers should not be running WITH attached workers,
 *       the function returns only on fatal signal pending and ignores/flushes
 *       all other signals.
 */
static int umcg_idle_loop(u64 abs_timeout)
{
	int ret;
	struct page *pinned_page = NULL;
	struct hrtimer_sleeper timeout;
	struct umcg_task __user *self = current->umcg_task;
	const bool worker = current->flags & PF_UMCG_WORKER;

	/* Clear PF_UMCG_WORKER to elide workqueue handlers. */
	if (worker)
		current->flags &= ~PF_UMCG_WORKER;

	if (abs_timeout) {
		hrtimer_init_sleeper_on_stack(&timeout, CLOCK_REALTIME,
				HRTIMER_MODE_ABS);

		hrtimer_set_expires_range_ns(&timeout.timer, (s64)abs_timeout,
				current->timer_slack_ns);
	}

	while (true) {
		u64 umcg_state;

		/*
		 * We need to read from userspace _after_ the task is marked
		 * TASK_INTERRUPTIBLE, to properly handle concurrent wakeups;
		 * but faulting is not allowed; so we try a fast no-fault read,
		 * and if it fails, pin the page temporarily.
		 */
retry_once:
		set_current_state(TASK_INTERRUPTIBLE);

		/* Order set_current_state above with get_user below. */
		smp_mb();
		ret = -EFAULT;
		if (get_user_nofault(umcg_state, &self->state_ts)) {
			set_current_state(TASK_RUNNING);

			if (pinned_page)
				goto out;
			else if (1 != pin_user_pages_fast((unsigned long)self,
						1, 0, &pinned_page))
					goto out;

			goto retry_once;
		}

		if (pinned_page) {
			unpin_user_page(pinned_page);
			pinned_page = NULL;
		}

		ret = 0;
		if (!umcg_should_idle(umcg_state)) {
			set_current_state(TASK_RUNNING);
			goto out;
		}

		if (abs_timeout)
			hrtimer_sleeper_start_expires(&timeout, HRTIMER_MODE_ABS);

		if (!abs_timeout || timeout.task)
			freezable_schedule();

		__set_current_state(TASK_RUNNING);

		/*
		 * Check for timeout before checking the state, as workers
		 * are not going to return from freezable_schedule() unless
		 * they are RUNNING.
		 */
		ret = -ETIMEDOUT;
		if (abs_timeout && !timeout.task)
			goto out;

		/* Order set_current_state above with get_user below. */
		smp_mb();
		ret = -EFAULT;
		if (get_user(umcg_state, &self->state_ts))
			goto out;

		ret = 0;
		if (!umcg_should_idle(umcg_state))
			goto out;

		ret = -EINTR;
		if (fatal_signal_pending(current))
			goto out;

		if (signal_pending(current))
			flush_signals(current);
	}

out:
	if (pinned_page) {
		unpin_user_page(pinned_page);
		pinned_page = NULL;
	}
	if (abs_timeout) {
		hrtimer_cancel(&timeout.timer);
		destroy_hrtimer_on_stack(&timeout.timer);
	}
	if (worker) {
		current->flags |= PF_UMCG_WORKER;

		if (ret == -ETIMEDOUT)
			ret = handle_timedout_worker(self);

		/* Workers must go through workqueue handlers upon wakeup. */
		set_tsk_thread_flag(current, TIF_NOTIFY_RESUME);
	}
	return ret;
}

/**
 * umcg_wakeup_allowed - check whether @current can wake @tsk.
 *
 * Currently a placeholder that allows wakeups within a single process
 * only (same mm). In the future the requirement will be relaxed (securely).
 */
static bool umcg_wakeup_allowed(struct task_struct *tsk)
{
	WARN_ON_ONCE(!rcu_read_lock_held());

	if (tsk->mm && tsk->mm == current->mm && READ_ONCE(tsk->umcg_task))
		return true;

	return false;
}

/*
 * Try to wake up. May be called with preempt_disable set. May be called
 * cross-process.
 *
 * Note: umcg_ttwu succeeds even if ttwu fails: see wait/wake state
 *       ordering logic.
 */
static int umcg_ttwu(u32 next_tid, int wake_flags)
{
	struct task_struct *next;

	rcu_read_lock();
	next = find_task_by_vpid(next_tid);
	if (!next || !umcg_wakeup_allowed(next)) {
		rcu_read_unlock();
		return -ESRCH;
	}

	/* The result of ttwu below is ignored. */
	try_to_wake_up(next, TASK_NORMAL, wake_flags);
	rcu_read_unlock();

	return 0;
}

/*
 * At the moment, umcg_do_context_switch simply wakes up @next with
 * WF_CURRENT_CPU and puts the current task to sleep.
 *
 * In the future an optimization will be added to adjust runtime accounting
 * so that from the kernel scheduling perspective the two tasks are
 * essentially treated as one. In addition, the context switch may be performed
 * right here on the fast path, instead of going through the wake/wait pair.
 */
static int umcg_do_context_switch(u32 next_tid, u64 abs_timeout)
{
	int ret;

	ret = umcg_ttwu(next_tid, WF_CURRENT_CPU);
	if (ret)
		return ret;

	return umcg_idle_loop(abs_timeout);
}

/**
 * sys_umcg_wait: put the current task to sleep and/or wake another task.
 * @flags:        zero or a value from enum umcg_wait_flag.
 * @abs_timeout:  when to wake the task, in nanoseconds; zero for no timeout.
 *
 * @self->state_ts must be UMCG_TASK_IDLE (where @self is current->umcg_task)
 * if !(@flags & UMCG_WAIT_WAKE_ONLY) (also see umcg_idle_loop and
 * umcg_should_idle above).
 *
 * If @self->next_tid is not zero, it must point to an IDLE UMCG task.
 * The userspace must have changed its state from IDLE to RUNNING
 * before calling sys_umcg_wait() in the current task. This "next"
 * task will be woken (context-switched-to on the fast path) when the
 * current task is put to sleep.
 *
 * See Documentation/userspace-api/umcg.txt for detals.
 *
 * Return:
 * 0             - OK;
 * -ETIMEDOUT    - the timeout expired;
 * -EFAULT       - failed accessing struct umcg_task __user of the current
 *                 task;
 * -ESRCH        - the task to wake not found or not a UMCG task;
 * -EINVAL       - another error happened (e.g. bad @flags, or the current
 *                 task is not a UMCG task, etc.)
 */
SYSCALL_DEFINE2(umcg_wait, u32, flags, u64, abs_timeout)
{
	struct umcg_task __user *self = current->umcg_task;
	u32 next_tid;

	if (!self)
		return -EINVAL;

	if (get_user(next_tid, &self->next_tid))
		return -EFAULT;

	if (flags & UMCG_WAIT_WAKE_ONLY) {
		if (!next_tid || abs_timeout)
			return -EINVAL;

		flags &= ~UMCG_WAIT_WAKE_ONLY;
		if (flags & ~UMCG_WAIT_WF_CURRENT_CPU)
			return -EINVAL;

		return umcg_ttwu(next_tid, flags & UMCG_WAIT_WF_CURRENT_CPU ?
					WF_CURRENT_CPU : 0);
	}

	/* Unlock the worker, if locked. */
	if (current->flags & PF_UMCG_WORKER) {
		u64 umcg_state;

		if (get_user(umcg_state, &self->state_ts))
			return -EFAULT;

		if ((umcg_state & UMCG_TF_LOCKED) && umcg_update_state(
					&self->state_ts, &umcg_state,
					umcg_state & ~UMCG_TF_LOCKED, true))
			return -EFAULT;
	}

	if (next_tid)
		return umcg_do_context_switch(next_tid, abs_timeout);

	return umcg_idle_loop(abs_timeout);
}

/*
 * NOTE: all code below is called from workqueue submit/update, or
 *       syscall exit to usermode loop, so all errors result in the
 *       termination of the current task (via SIGKILL).
 */

/*
 * Wake idle server: find the task, change its state IDLE=>RUNNING, ttwu.
 */
static int umcg_wake_idle_server_nofault(u32 server_tid)
{
	struct umcg_task __user *ut_server = NULL;
	struct task_struct *tsk;
	int ret = -EINVAL;
	u64 state;

	rcu_read_lock();

	tsk = find_task_by_vpid(server_tid);
	/* Server/worker interaction is allowed only within the same mm. */
	if (tsk && current->mm == tsk->mm)
		ut_server = READ_ONCE(tsk->umcg_task);

	if (!ut_server)
		goto out_rcu;

	ret = -EFAULT;
	if (get_user_nofault(state, &ut_server->state_ts))
		goto out_rcu;

	ret = -EAGAIN;
	if ((state & UMCG_TASK_STATE_MASK) != UMCG_TASK_IDLE)
		goto out_rcu;

	ret = umcg_update_state(&ut_server->state_ts, &state,
			(state & ~UMCG_TASK_STATE_MASK) | UMCG_TASK_RUNNING,
			false);

	if (ret)
		goto out_rcu;

	try_to_wake_up(tsk, TASK_NORMAL, WF_CURRENT_CPU);

out_rcu:
	rcu_read_unlock();
	return ret;
}

/*
 * Wake idle server: find the task, change its state IDLE=>RUNNING, ttwu.
 */
static int umcg_wake_idle_server_may_fault(u32 server_tid)
{
	struct umcg_task __user *ut_server = NULL;
	struct task_struct *tsk;
	int ret = -EINVAL;
	u64 state;

	rcu_read_lock();
	tsk = find_task_by_vpid(server_tid);
	if (tsk && current->mm == tsk->mm)
		ut_server = READ_ONCE(tsk->umcg_task);
	rcu_read_unlock();

	if (!ut_server)
		return -EINVAL;

	if (get_user(state, &ut_server->state_ts))
		return -EFAULT;

	if ((state & UMCG_TASK_STATE_MASK) != UMCG_TASK_IDLE)
		return -EAGAIN;

	ret = umcg_update_state(&ut_server->state_ts, &state,
			(state & ~UMCG_TASK_STATE_MASK) | UMCG_TASK_RUNNING,
			true);
	if (ret)
		return ret;

	/*
	 * umcg_ttwu will call find_task_by_vpid again; but we cannot
	 * elide this, as we cannot do get_user() from an rcu-locked
	 * code block.
	 */
	return umcg_ttwu(server_tid, WF_CURRENT_CPU);
}

/*
 * Wake idle server: find the task, change its state IDLE=>RUNNING, ttwu.
 */
static int umcg_wake_idle_server(u32 server_tid, bool may_fault)
{
	int ret = umcg_wake_idle_server_nofault(server_tid);

	if (!ret)
		return 0;

	if (!may_fault || ret != -EFAULT)
		return ret;

	return umcg_wake_idle_server_may_fault(server_tid);
}

/*
 * Called in sched_submit_work() context for UMCG workers. In the common case,
 * the worker's state changes RUNNING => BLOCKED, and its server's state
 * changes IDLE => RUNNING, and the server is ttwu-ed.
 *
 * Under some conditions (e.g. the worker is "locked", see
 * /Documentation/userspace-api/umcg.txt for more details), the
 * function does nothing.
 *
 * The function is called with preempt disabled to make sure the retry_once
 * logic below works correctly.
 */
static void process_sleeping_worker(struct task_struct *tsk, u32 *server_tid)
{
	struct umcg_task __user *ut_worker = tsk->umcg_task;
	u64 curr_state, next_state;
	bool retried = false;
	u32 tid;
	int ret;

	*server_tid = 0;

	if (WARN_ONCE((tsk != current) || !ut_worker, "Invalid UMCG worker."))
		return;

	/* If the worker has no server, do nothing. */
	if (unlikely(!tsk->pinned_umcg_server_page))
		return;

	if (get_user_nofault(curr_state, &ut_worker->state_ts))
		goto die;

	/*
	 * The userspace is allowed to concurrently change a RUNNING worker's
	 * state only once in a "short" period of time, so we retry state
	 * change at most once. As this retry block is within a
	 * preempt_disable region, "short" is truly short here.
	 *
	 * See Documentation/userspace-api/umcg.txt for details.
	 */
retry_once:
	if (curr_state & UMCG_TF_LOCKED)
		return;

	if (WARN_ONCE((curr_state & UMCG_TASK_STATE_MASK) != UMCG_TASK_RUNNING,
			"Unexpected UMCG worker state."))
		goto die;

	next_state = curr_state & ~UMCG_TASK_STATE_MASK;
	next_state |= UMCG_TASK_BLOCKED;

	ret = umcg_update_state(&ut_worker->state_ts, &curr_state, next_state, false);
	if (ret == -EAGAIN) {
		if (retried)
			goto die;

		retried = true;
		goto retry_once;
	}
	if (ret)
		goto die;

	smp_mb();  /* Order state read/write above and getting next_tid below. */
	if (get_user_nofault(tid, &ut_worker->next_tid))
		goto die;

	*server_tid = tid;
	return;

die:
	pr_warn("%s: killing task %d\n", __func__, current->pid);
	force_sig(SIGKILL);
}

/* Called from sched_submit_work(). Must not fault/sleep. */
void umcg_wq_worker_sleeping(struct task_struct *tsk)
{
	u32 server_tid;

	/*
	 * Disable preemption so that retry_once in process_sleeping_worker
	 * works properly.
	 */
	preempt_disable();
	process_sleeping_worker(tsk, &server_tid);
	preempt_enable();

	if (server_tid) {
		int ret = umcg_wake_idle_server_nofault(server_tid);

		if (ret && ret != -EAGAIN)
			goto die;
	}

	goto out;

die:
	pr_warn("%s: killing task %d\n", __func__, current->pid);
	force_sig(SIGKILL);
out:
	umcg_unpin_pages();
}

/**
 * enqueue_idle_worker - push an idle worker onto idle_workers_ptr list/stack.
 *
 * Returns true on success, false on a fatal failure.
 *
 * See Documentation/userspace-api/umcg.txt for details.
 */
static bool enqueue_idle_worker(struct umcg_task __user *ut_worker)
{
	u64 __user *node = &ut_worker->idle_workers_ptr;
	u64 __user *head_ptr;
	u64 first = (u64)node;
	u64 head;

	if (get_user(head, node) || !head)
		return false;

	head_ptr = (u64 __user *)head;

	/* Mark the worker as pending. */
	if (put_user(UMCG_IDLE_NODE_PENDING, node))
		return false;

	/* Make the head point to the worker. */
	if (xchg_user_64(head_ptr, &first))
		return false;

	/* Make the worker point to the previous head. */
	if (put_user(first, node))
		return false;

	return true;
}

/**
 * get_idle_server - retrieve an idle server, if present.
 *
 * Returns true on success, false on a fatal failure.
 */
static bool get_idle_server(struct umcg_task __user *ut_worker, u32 *server_tid)
{
	u64 server_tid_ptr;
	u32 tid;

	/* Empty result is OK. */
	*server_tid = 0;

	if (get_user(server_tid_ptr, &ut_worker->idle_server_tid_ptr))
		return false;

	if (!server_tid_ptr)
		return false;

	tid = 0;
	if (xchg_user_32((u32 __user *)server_tid_ptr, &tid))
		return false;

	*server_tid = tid;
	return true;
}

/*
 * Returns true to wait for the userspace to schedule this worker, false
 * to return to the userspace.
 *
 * In the common case, a BLOCKED worker is marked IDLE and enqueued
 * to idle_workers_ptr list. The idle server is woken (if present).
 *
 * If a RUNNING worker is preempted, this function will trigger, in which
 * case the worker is moved to IDLE state and its server is woken.
 *
 * Sets @server_tid to point to the server to be woken if the worker
 * is going to sleep; sets @server_tid to point to the server assigned
 * to this RUNNING worker if the worker is to return to the userspace.
 */
static bool process_waking_worker(struct task_struct *tsk, u32 *server_tid)
{
	struct umcg_task __user *ut_worker = tsk->umcg_task;
	u64 curr_state, next_state;

	*server_tid = 0;

	if (WARN_ONCE((tsk != current) || !ut_worker, "Invalid umcg worker"))
		return false;

	if (fatal_signal_pending(tsk))
		return false;

	if (get_user(curr_state, &ut_worker->state_ts))
		goto die;

	if ((curr_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_RUNNING) {
		u32 tid;

		/* Wakeup: wait but don't enqueue. */
		if (curr_state & UMCG_TF_LOCKED)
			return true;

		smp_mb();  /* Order getting state and getting server_tid */
		if (get_user(tid, &ut_worker->next_tid))
			goto die;

		if (!tid)
			/* RUNNING workers must have servers. */
			goto die;

		*server_tid = tid;

		/* pass-through: RUNNING with a server. */
		if (!(curr_state & UMCG_TF_PREEMPTED))
			return false;

		/*
		 * Fallthrough to mark the worker IDLE: the worker is
		 * PREEMPTED.
		 */
	} else if (unlikely((curr_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_IDLE &&
			(curr_state & UMCG_TF_LOCKED)))
		/* The worker prepares to sleep or to unregister. */
		return false;

	if (unlikely((curr_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_IDLE))
		goto die;

	next_state = curr_state & ~UMCG_TASK_STATE_MASK;
	next_state |= UMCG_TASK_IDLE;

	if (umcg_update_state(&ut_worker->state_ts, &curr_state,
			next_state, true))
		goto die;

	if (!enqueue_idle_worker(ut_worker))
		goto die;

	smp_mb();  /* Order enqueuing the worker with getting the server. */
	if (!(*server_tid) && !get_idle_server(ut_worker, server_tid))
		goto die;

	return true;

die:
	pr_warn("umcg_process_waking_worker: killing task %d\n", current->pid);
	force_sig(SIGKILL);
	return false;
}

/*
 * Called from sched_update_worker(): defer all work until later, as
 * sched_update_worker() may be called with in-kernel locks held.
 */
void umcg_wq_worker_running(struct task_struct *tsk)
{
	set_tsk_thread_flag(tsk, TIF_NOTIFY_RESUME);
}

/* Called via TIF_NOTIFY_RESUME flag from exit_to_user_mode_loop. */
void umcg_handle_resuming_worker(void)
{
	u32 server_tid;

	/* Avoid recursion by removing PF_UMCG_WORKER */
	current->flags &= ~PF_UMCG_WORKER;

	do {
		bool should_wait;

		should_wait = process_waking_worker(current, &server_tid);
		if (!should_wait)
			break;

		if (server_tid) {
			int ret = umcg_wake_idle_server(server_tid, true);

			if (ret && ret != -EAGAIN)
				goto die;
		}

		umcg_idle_loop(0);
	} while (true);

	if (!server_tid)
		/* No server => no reason to pin pages. */
		umcg_unpin_pages();
	else if (umcg_pin_pages(server_tid))
		goto die;

	goto out;

die:
	pr_warn("%s: killing task %d\n", __func__, current->pid);
	force_sig(SIGKILL);
out:
	current->flags |= PF_UMCG_WORKER;
}
