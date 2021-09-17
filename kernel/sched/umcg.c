// SPDX-License-Identifier: GPL-2.0-only

/*
 * User Managed Concurrency Groups (UMCG).
 *
 * See Documentation/userspace-api/umcg.[txt|rst] for detals.
 */

#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/umcg.h>

#include "sched.h"
#include "umcg_uaccess.h"

/**
 * umcg_pin_pages: pin pages containing struct umcg_task of this worker
 *                 and its server.
 *
 * The pages are pinned when the worker exits to the userspace and unpinned
 * when the worker is in sched_submit_work(), i.e. when the worker is
 * about to be removed from its runqueue. Thus at most NR_CPUS UMCG pages
 * are pinned at any one time across the whole system.
 */
static int umcg_pin_pages(u32 server_tid)
{
	struct umcg_task __user *worker_ut = current->umcg_task;
	struct umcg_task __user *server_ut = NULL;
	struct task_struct *tsk;

	rcu_read_lock();
	tsk = find_task_by_vpid(server_tid);
	if (tsk)
		server_ut = READ_ONCE(tsk->umcg_task);
	rcu_read_unlock();

	if (!server_ut)
		return -EINVAL;

	if (READ_ONCE(current->mm) != READ_ONCE(tsk->mm))
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
 * sys_umcg_ctl: (un)register the current task as a UMCG task.
 * @flags:       ORed values from enum umcg_ctl_flag; see below;
 * @self:        a pointer to struct umcg_task that describes this
 *               task and governs the behavior of sys_umcg_wait if
 *               registering; must be NULL if unregistering.
 *
 * @flags & UMCG_CTL_REGISTER: register a UMCG task:
 *         UMCG workers:
 *              - self->state must be UMCG_TASK_IDLE
 *              - @flags & UMCG_CTL_WORKER
 *         UMCG servers:
 *              - self->state must be UMCG_TASK_RUNNING
 *              - !(@flags & UMCG_CTL_WORKER)
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

	/* Register the current task as a UMCG task. */
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
		if (ut.state != UMCG_TASK_BLOCKED)
			return -EINVAL;

		WRITE_ONCE(current->umcg_task, self);
		current->flags |= PF_UMCG_WORKER;

		set_tsk_need_resched(current);
		return 0;
	}

	/* This is a server task. */
	if (ut.state != UMCG_TASK_RUNNING)
		return -EINVAL;

	WRITE_ONCE(current->umcg_task, self);
	return 0;
}

/**
 * handle_timedout_worker - make sure the worker is added to idle_workers
 *                          upon a "clean" timeout.
 */
static int handle_timedout_worker(struct umcg_task __user *self)
{
	u32 prev_state, next_state;
	int ret;

	if (get_user(prev_state, &self->state))
		return -EFAULT;

	if ((prev_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_IDLE) {
		/* TODO: should we care here about TF_LOCKED or TF_PREEMPTED? */

		next_state = prev_state & ~UMCG_TASK_STATE_MASK;
		next_state |= UMCG_TASK_BLOCKED;

		ret = cmpxchg_user_32(&self->state, &prev_state, next_state);
		if (ret)
			return ret;

		return -ETIMEDOUT;
	}

	return 0;  /* Not really timed out. */
}

/**
 * umcg_idle_loop - sleep until the current task becomes RUNNING or a timeout
 * @abs_timeout - absolute timeout in nanoseconds; zero => no timeout
 *
 * The function marks the current task as INTERRUPTIBLE and calls
 * schedule(). It returns when either the timeout expires or
 * the UMCG state of the task becomes RUNNING.
 *
 * Note: because UMCG workers should not be running WITHOUT attached servers,
 *       and because servers should not be running WITH attached workers,
 *       the function returns only on fatal signal pending and ignores/flushes
 *       all other signals.
 */
static int umcg_idle_loop(u64 abs_timeout)
{
	int ret;
	struct hrtimer_sleeper timeout;
	struct umcg_task __user *self = current->umcg_task;

	if (abs_timeout) {
		hrtimer_init_sleeper_on_stack(&timeout, CLOCK_REALTIME,
				HRTIMER_MODE_ABS);

		hrtimer_set_expires_range_ns(&timeout.timer, (s64)abs_timeout,
				current->timer_slack_ns);
	}

	while (true) {
		u32 umcg_state;

		set_current_state(TASK_INTERRUPTIBLE);

		smp_mb();  /* Order with set_current_state() above. */
		ret = -EFAULT;
		if (get_user(umcg_state, &self->state)) {
			set_current_state(TASK_RUNNING);
			goto out;
		}

		ret = 0;
		if ((umcg_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_RUNNING) {
			set_current_state(TASK_RUNNING);
			goto out;
		}

		if (abs_timeout)
			hrtimer_sleeper_start_expires(&timeout, HRTIMER_MODE_ABS);

		if (!abs_timeout || timeout.task) {
			/*
			 * Clear PF_UMCG_WORKER to elide workqueue handlers.
			 */
			const bool worker = current->flags & PF_UMCG_WORKER;

			if (worker)
				current->flags &= ~PF_UMCG_WORKER;

			/*
			 * Note: freezable_schedule() here is not appropriate
			 * as umcg_idle_loop can be called from rwsem locking
			 * context (via workqueue handlers), which may
			 * trigger a lockdep warning for mmap_lock.
			 */
			schedule();

			if (worker)
				current->flags |= PF_UMCG_WORKER;
		}
		__set_current_state(TASK_RUNNING);

		/*
		 * Check for timeout before checking the state, as workers
		 * are not going to return from schedule() unless
		 * they are RUNNING.
		 */
		ret = -ETIMEDOUT;
		if (abs_timeout && !timeout.task)
			goto out;

		ret = -EFAULT;
		if (get_user(umcg_state, &self->state))
			goto out;

		ret = 0;
		if ((umcg_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_RUNNING)
			goto out;

		ret = -EINTR;
		if (fatal_signal_pending(current))
			goto out;

		if (signal_pending(current))
			flush_signals(current);
	}

out:
	if (abs_timeout) {
		hrtimer_cancel(&timeout.timer);
		destroy_hrtimer_on_stack(&timeout.timer);
	}

	/* Workers must go through workqueue handlers upon wakeup. */
	if (current->flags & PF_UMCG_WORKER) {
		if (ret == -ETIMEDOUT)
			ret = handle_timedout_worker(self);

		set_tsk_need_resched(current);
	}

	return ret;
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
	if (!next || !(READ_ONCE(next->umcg_task))) {
		rcu_read_unlock();
		return -ESRCH;
	}

	/* Note: next does not necessarily share mm with current. */

	try_to_wake_up(next, TASK_NORMAL, wake_flags);  /* Result ignored. */
	rcu_read_unlock();

	return 0;
}

/*
 * At the moment, umcg_do_context_switch simply wakes up @next with
 * WF_CURRENT_CPU and puts the current task to sleep. May be called cross-mm.
 *
 * In the future an optimization will be added to adjust runtime accounting
 * so that from the kernel scheduling perspective the two tasks are
 * essentially treated as one. In addition, the context switch may be performed
 * right here on the fast path, instead of going through the wake/wait pair.
 */
static int umcg_do_context_switch(u32 next_tid, u64 abs_timeout)
{
	struct task_struct *next;

	rcu_read_lock();
	next = find_task_by_vpid(next_tid);
	if (!next) {
		rcu_read_unlock();
		return -ESRCH;
	}

	/* Note: next does not necessarily share mm with current. */

	/* TODO: instead of wake + sleep, do a context switch. */
	try_to_wake_up(next, TASK_NORMAL, WF_CURRENT_CPU);  /* Result ignored. */
	rcu_read_unlock();

	return umcg_idle_loop(abs_timeout);
}

/**
 * sys_umcg_wait: put the current task to sleep and/or wake another task.
 * @flags:        zero or a value from enum umcg_wait_flag.
 * @abs_timeout:  when to wake the task, in nanoseconds; zero for no timeout.
 *
 * @self->state must be UMCG_TASK_IDLE (where @self is current->umcg_task)
 * if !(@flags & UMCG_WAIT_WAKE_ONLY).
 *
 * If @self->next_tid is not zero, it must point to an IDLE UMCG task.
 * The userspace must have changed its state from IDLE to RUNNING
 * before calling sys_umcg_wait() in the current task. This "next"
 * task will be woken (context-switched-to on the fast path) when the
 * current task is put to sleep.
 *
 * See Documentation/userspace-api/umcg.[txt|rst] for detals.
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
		u32 umcg_state;

		if (get_user(umcg_state, &self->state))
			return -EFAULT;

		if ((umcg_state & UMCG_TF_LOCKED) && cmpxchg_user_32(
					&self->state, &umcg_state,
					umcg_state & ~UMCG_TF_LOCKED))
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

/* Returns true on success, false on _any_ error. */
static bool mark_server_running(u32 server_tid, bool may_sleep)
{
	struct umcg_task __user *ut_server = NULL;
	u32 state = UMCG_TASK_IDLE;
	struct task_struct *tsk;

	rcu_read_lock();
	tsk = find_task_by_vpid(server_tid);
	if (tsk)
		ut_server = READ_ONCE(tsk->umcg_task);
	rcu_read_unlock();

	if (!ut_server)
		return false;

	if (READ_ONCE(current->mm) != READ_ONCE(tsk->mm))
		return false;

	if (may_sleep)
		return !cmpxchg_user_32(&ut_server->state, &state, UMCG_TASK_RUNNING);

	return !cmpxchg_user_32_nosleep(&ut_server->state, &state, UMCG_TASK_RUNNING);
}

/*
 * Called by sched_submit_work() for UMCG workers from within preempt_disable()
 * context. In the common case, the worker's state changes RUNNING => BLOCKED,
 * and its server's state changes IDLE => RUNNING, and the server is ttwu-ed.
 *
 * Under some conditions (e.g. the worker is "locked", see
 * /Documentation/userspace-api/umcg.[txt|rst] for more details), the
 * function does nothing.
 */
static void __umcg_wq_worker_sleeping(struct task_struct *tsk)
{
	struct umcg_task __user *ut_worker = tsk->umcg_task;
	u32 prev_state, next_state, server_tid;
	bool preempted = false;
	int ret;

	if (WARN_ONCE((tsk != current) || !ut_worker, "Invalid umcg worker"))
		return;

	/* Sometimes "locked" workers run without servers. */
	if (unlikely(!tsk->pinned_umcg_server_page))
		return;

	smp_mb();  /* The userspace may change the state concurrently. */
	if (get_user_nosleep(prev_state, &ut_worker->state))
		goto die;  /* EFAULT */

	if (prev_state & UMCG_TF_LOCKED)
		return;

	if ((prev_state & UMCG_TASK_STATE_MASK) != UMCG_TASK_RUNNING)
		return;  /* the worker is in umcg_wait */

retry_once:
	next_state = prev_state & ~UMCG_TASK_STATE_MASK;
	next_state |= UMCG_TASK_BLOCKED;
	preempted = prev_state & UMCG_TF_PREEMPTED;

	ret = cmpxchg_user_32_nosleep(&ut_worker->state, &prev_state, next_state);
	if (ret == -EAGAIN) {
		if (preempted)
			goto die;  /* Preemption can only happen once. */

		if (prev_state != (UMCG_TASK_RUNNING | UMCG_TF_PREEMPTED))
			goto die;  /* Only preemption can happen. */

		preempted = true;
		goto retry_once;
	}
	if (ret)
		goto die;  /* EFAULT */

	if (get_user_nosleep(server_tid, &ut_worker->next_tid))
		goto die;  /* EFAULT */

	if (!server_tid)
		return;  /* Waking a waiting worker leads here. */

	/* The idle server's wait may timeout. */
	/* TODO: make a smarter context switch below when available. */
	if (mark_server_running(server_tid, false))
		umcg_ttwu(server_tid, WF_CURRENT_CPU);

	return;

die:
	pr_warn("umcg_wq_worker_sleeping: killing task %d\n", current->pid);
	force_sig(SIGKILL);
}

/* Called from sched_submit_work() with preempt_disable. */
void umcg_wq_worker_sleeping(struct task_struct *tsk)
{
	__umcg_wq_worker_sleeping(tsk);
	umcg_unpin_pages();
}

/**
 * enqueue_idle_worker - push an idle worker onto idle_workers_ptr list/stack.
 *
 * Returns true on success, false on a fatal failure.
 *
 * See Documentation/userspace-api/umcg.[txt|rst] for details.
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

	if (put_user(UMCG_IDLE_NODE_PENDING, node))
		return false;

	if (xchg_user_64(head_ptr, &first))
		return false;

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
	int ret;

	*server_tid = 0;  /* Empty result is OK. */

	if (get_user(server_tid_ptr, &ut_worker->idle_server_tid_ptr))
		return false;

	if (!server_tid_ptr)
		return false;

	tid = 0;
	ret = xchg_user_32((u32 __user *)server_tid_ptr, &tid);

	if (ret)
		return false;

	if (tid && mark_server_running(tid, true))
		*server_tid = tid;

	return true;
}

/*
 * Returns true to wait for the userspace to schedule this worker, false
 * to return to the userspace. In the common case, enqueues the worker
 * to idle_workers_ptr list and wakes the idle server (if present).
 */
static bool process_waking_worker(struct task_struct *tsk, u32 *server_tid)
{
	struct umcg_task __user *ut_worker = tsk->umcg_task;
	u32 prev_state, next_state;
	int ret = 0;

	*server_tid = 0;

	if (WARN_ONCE((tsk != current) || !ut_worker, "Invalid umcg worker"))
		return false;

	if (fatal_signal_pending(tsk))
		return false;

	smp_mb();  /* The userspace may concurrently modify the worker's state. */
	if (get_user(prev_state, &ut_worker->state))
		goto die;

	if ((prev_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_RUNNING) {
		u32 tid;

		if (prev_state & UMCG_TF_LOCKED)
			return true;  /* Wakeup: wait but don't enqueue. */

		smp_mb();  /* Order getting state and getting server_tid */

		if (get_user(tid, &ut_worker->next_tid))
			goto die;

		*server_tid = tid;

		if (prev_state & UMCG_TF_PREEMPTED) {
			if (!tid)
				goto die;  /* PREEMPTED workers must have a server. */

			/* Always enqueue preempted workers. */
			if (!mark_server_running(tid, true))
				goto die;
		} else if (tid)
			return false;  /* pass-through: RUNNING with a server. */

		/* If !PREEMPTED, the worker gets here via UMCG_WAIT_WAKE_ONLY */
	} else if (unlikely((prev_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_IDLE &&
			(prev_state & UMCG_TF_LOCKED)))
		return false;  /* The worker prepares to sleep or to unregister. */

	if ((prev_state & UMCG_TASK_STATE_MASK) == UMCG_TASK_IDLE)
		return true;  /* the worker called umcg_wait(); don't enqueue */

	next_state = prev_state & ~UMCG_TASK_STATE_MASK;
	next_state |= UMCG_TASK_IDLE;

	if (prev_state != next_state)
		ret = cmpxchg_user_32(&ut_worker->state, &prev_state, next_state);
	if (ret)
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

		if (server_tid)
			umcg_do_context_switch(server_tid, 0);
		else
			umcg_idle_loop(0);
	} while (true);

	if (server_tid && umcg_pin_pages(server_tid))
		goto die;

	if (!server_tid)  /* No server => no reason to pin pages. */
		umcg_unpin_pages();

	goto out;

die:
	pr_warn("%s: killing task %d\n", __func__, current->pid);
	force_sig(SIGKILL);
out:
	current->flags |= PF_UMCG_WORKER;
}
