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

static int umcg_segv(int res)
{
	force_sig(SIGSEGV);
	return res;
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

static void umcg_lock_pair(struct task_struct *server,
		struct task_struct *worker)
{
	spin_lock(&server->alloc_lock);
	spin_lock_nested(&worker->alloc_lock, SINGLE_DEPTH_NESTING);
}

static void umcg_unlock_pair(struct task_struct *server,
		struct task_struct *worker)
{
	spin_unlock(&worker->alloc_lock);
	spin_unlock(&server->alloc_lock);
}

static void umcg_detach_peer(void)
{
	struct task_struct *server, *worker;
	struct umcg_task_data *utd;

	rcu_read_lock();
	task_lock(current);
	utd = rcu_dereference(current->umcg_task_data);

	if (!utd || !rcu_dereference(utd->peer)) {
		task_unlock(current);
		goto out;
	}

	switch (utd->task_type) {
	case UMCG_TT_SERVER:
		server = current;
		worker = rcu_dereference(utd->peer);
		break;

	case UMCG_TT_WORKER:
		worker = current;
		server = rcu_dereference(utd->peer);
		break;

	default:
		task_unlock(current);
		printk(KERN_WARNING "umcg_detach_peer: unexpected task type");
		umcg_segv(0);
		goto out;
	}
	task_unlock(current);

	if (!server || !worker)
		goto out;

	umcg_lock_pair(server, worker);

	utd = rcu_dereference(server->umcg_task_data);
	if (WARN_ON(!utd)) {
		umcg_segv(0);
		goto out_pair;
	}
	rcu_assign_pointer(utd->peer, NULL);

	utd = rcu_dereference(worker->umcg_task_data);
	if (WARN_ON(!utd)) {
		umcg_segv(0);
		goto out_pair;
	}
	rcu_assign_pointer(utd->peer, NULL);

out_pair:
	umcg_unlock_pair(server, worker);
out:
	rcu_read_unlock();
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
	RCU_INIT_POINTER(utd->peer, NULL);

	if (put_state(umcg_task, UMCG_TASK_RUNNING)) {
		kfree(utd);
		return -EFAULT;
	}

	task_lock(current);
	rcu_assign_pointer(current->umcg_task_data, utd);
	task_unlock(current);

	return 0;
}

static int add_task_to_group(u32 api_version, u32 group_id,
		struct umcg_task __user *umcg_task,
		enum umcg_task_type task_type, u32 new_state)
{
	struct mm_struct *mm = current->mm;
	struct umcg_task_data *utd = NULL;
	struct umcg_group *group = NULL;
	struct umcg_group *list_entry;
	int ret = -EINVAL;
	u32 state;

	if (get_state(umcg_task, &state))
		return -EFAULT;

	if (state != UMCG_TASK_NONE)
		return -EINVAL;

	if (put_state(umcg_task, new_state))
		return -EFAULT;

retry_once:
	rcu_read_lock();
	list_for_each_entry_rcu(list_entry, &mm->umcg_groups, list) {
		if (list_entry->group_id == group_id) {
			group = list_entry;
			break;
		}
	}

	if (!group || group->api_version != api_version)
		goto out_rcu;

	spin_lock(&group->lock);
	if (group->nr_tasks < 0)  /* The groups is being destroyed. */
		goto out_group;

	if (!utd) {
		utd = kzalloc(sizeof(struct umcg_task_data), GFP_NOWAIT);
		if (!utd) {
			spin_unlock(&group->lock);
			rcu_read_unlock();

			utd = kzalloc(sizeof(struct umcg_task_data), GFP_KERNEL);
			if (!utd) {
				ret = -ENOMEM;
				goto out;
			}

			goto retry_once;
		}
	}

	utd->self = current;
	utd->group = group;
	utd->umcg_task = umcg_task;
	utd->task_type = task_type;
	utd->api_version = api_version;
	RCU_INIT_POINTER(utd->peer, NULL);

	INIT_LIST_HEAD(&utd->list);
	group->nr_tasks++;

	task_lock(current);
	rcu_assign_pointer(current->umcg_task_data, utd);
	task_unlock(current);

	ret = 0;

out_group:
	spin_unlock(&group->lock);

out_rcu:
	rcu_read_unlock();
	if (ret && utd)
		kfree(utd);

out:
	if (ret)
		put_state(umcg_task, UMCG_TASK_NONE);
	else
		schedule();  /* Trigger umcg_on_wake(). */

	return ret;
}

static int register_worker(u32 api_version, u32 group_id,
		struct umcg_task __user *umcg_task)
{
	return add_task_to_group(api_version, group_id, umcg_task,
				UMCG_TT_WORKER, UMCG_TASK_UNBLOCKED);
}

static int register_server(u32 api_version, u32 group_id,
		struct umcg_task __user *umcg_task)
{
	return add_task_to_group(api_version, group_id, umcg_task,
				UMCG_TT_SERVER, UMCG_TASK_PROCESSING);
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
	case UMCG_REGISTER_WORKER:
		return register_worker(api_version, group_id, umcg_task);
	case UMCG_REGISTER_SERVER:
		return register_server(api_version, group_id, umcg_task);
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

	if (!utd->group) {
		ret = 0;
		goto out;
	}

	if (utd->task_type == UMCG_TT_WORKER) {
		struct task_struct *server = rcu_dereference(utd->peer);

		if (server) {
			umcg_detach_peer();
			if (WARN_ON(!wake_up_process(server))) {
				umcg_segv(0);
				goto out;
			}
		}
	} else {
		if (WARN_ON(utd->task_type != UMCG_TT_SERVER)) {
			umcg_segv(0);
			goto out;
		}

		umcg_detach_peer();
	}

	spin_lock(&utd->group->lock);
	task_lock(current);

	rcu_assign_pointer(current->umcg_task_data, NULL);

	--utd->group->nr_tasks;

	task_unlock(current);
	spin_unlock(&utd->group->lock);

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
	bool prev_wait_flag;  /* See comment in do_wait() below. */

	/*
	 * It is important to set_current_state(TASK_INTERRUPTIBLE) before
	 * waking @next, as @next may immediately try to wake current back
	 * (e.g. current is a server, @next is a worker that immediately
	 * blocks or waits), and this next wakeup must not be lost.
	 */
	set_current_state(TASK_INTERRUPTIBLE);

	prev_wait_flag = utd->in_wait;
	if (!prev_wait_flag)
		WRITE_ONCE(utd->in_wait, true);
	
	if (!try_to_wake_up(next, TASK_NORMAL, WF_CURRENT_CPU))
		return -EAGAIN;

	freezable_schedule();

	if (!prev_wait_flag)
		WRITE_ONCE(utd->in_wait, false);

	if (signal_pending(current))
		return -EINTR;

	/* TODO: deal with non-fatal interrupts. */
	return 0;
}

static int do_wait(void)
{
	struct umcg_task_data *utd = rcu_access_pointer(current->umcg_task_data);
	/*
	 * freezable_schedule() below can recursively call do_wait() if
	 * this is a worker that needs a server. As the wait flag is only
	 * used by the outermost wait/wake (and swap) syscalls, modify it only
	 * in the outermost do_wait() instead of using a counter.
	 *
	 * Note that the nesting level is at most two, as utd->in_workqueue
	 * is used to prevent further nesting.
	 */
	bool prev_wait_flag;

	if (!utd)
		return -EINVAL;

	prev_wait_flag = utd->in_wait;
	if (!prev_wait_flag)
		WRITE_ONCE(utd->in_wait, true);

	set_current_state(TASK_INTERRUPTIBLE);
	freezable_schedule();

	if (!prev_wait_flag)
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
 * Sleep until woken or @timeout expires.
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
	struct task_struct *server = NULL;

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

	if (utd->task_type == UMCG_TT_WORKER)
		server = rcu_dereference(utd->peer);

	rcu_read_unlock();

	if (server)
		return do_context_switch(server);

	return do_wait();
}

/**
 * sys_umcg_wake - wake @next_tid task blocked in sys_umcg_wait.
 * @flags:         Reserved.
 * @next_tid:      The ID of the task to wake.
 *
 * Wake task next identified by @next_tid. @next must be either a UMCG core
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
	struct task_struct *next, *next_peer;
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

	if (next_utd->task_type == UMCG_TT_SERVER)
		goto out;

	if (!READ_ONCE(next_utd->in_wait)) {
		ret = -EAGAIN;
		goto out;
	}

	next_peer = rcu_dereference(next_utd->peer);
	if (next_peer) {
		if (next_peer == current)
			umcg_detach_peer();
		else {
			/*
			 * Waking a worker with an assigned server is not
			 * permitted, unless the waking is done by the assigned
			 * server.
			 */
			umcg_segv(0);
			goto out;
		}
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
	if (!next_utd || next_utd->group != curr_utd->group) {
		ret = -EINVAL;
		goto out;
	}

	if (!READ_ONCE(next_utd->in_wait)) {
		ret = -EAGAIN;
		goto out;
	}

	/* Move the server from curr to next, if appropriate. */
	if (curr_utd->task_type == UMCG_TT_WORKER) {
		struct task_struct *server = rcu_dereference(curr_utd->peer);
		if (server) {
			struct umcg_task_data *server_utd =
				rcu_dereference(server->umcg_task_data);

			if (rcu_access_pointer(next_utd->peer)) {
				ret = -EAGAIN;
				goto out;
			}
			umcg_detach_peer();
			umcg_lock_pair(server, next);
			rcu_assign_pointer(server_utd->peer, next);
			rcu_assign_pointer(next_utd->peer, server);
			umcg_unlock_pair(server, next);
		}
	}

	rcu_read_unlock();

	return do_context_switch(next);

out:
	rcu_read_unlock();
	return ret;
}

/**
 * sys_umcg_create_group - create a UMCG group
 * @api_version:           Requested API version.
 * @flags:                 Reserved.
 *
 * Return:
 * >= 0                - the group ID
 * -EOPNOTSUPP         - @api_version is not supported
 * -EINVAL             - @flags is not valid
 * -ENOMEM             - not enough memory
 */
SYSCALL_DEFINE2(umcg_create_group, u32, api_version, u64, flags)
{
	int ret;
	struct umcg_group *group;
	struct umcg_group *list_entry;
	struct mm_struct *mm = current->mm;

	if (flags)
		return -EINVAL;

	if (__api_version(api_version))
		return -EOPNOTSUPP;

	group = kzalloc(sizeof(struct umcg_group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	spin_lock_init(&group->lock);
	INIT_LIST_HEAD(&group->list);
	INIT_LIST_HEAD(&group->waiters);
	group->flags = flags;
	group->api_version = api_version;

	spin_lock(&mm->umcg_lock);

	list_for_each_entry_rcu(list_entry, &mm->umcg_groups, list) {
		if (list_entry->group_id >= group->group_id)
			group->group_id = list_entry->group_id + 1;
	}

	list_add_rcu(&mm->umcg_groups, &group->list);

	ret = group->group_id;
	spin_unlock(&mm->umcg_lock);

	return ret;
}

/**
 * sys_umcg_destroy_group - destroy a UMCG group
 * @group_id: The ID of the group to destroy.
 *
 * The group must be empty, i.e. have no registered servers or workers.
 *
 * Return:
 * 0       - success;
 * -ESRCH  - group not found;
 * -EBUSY  - the group has registered workers or servers.
 */
SYSCALL_DEFINE1(umcg_destroy_group, u32, group_id)
{
	int ret = 0;
	struct umcg_group *group = NULL;
	struct umcg_group *list_entry;
	struct mm_struct *mm = current->mm;

	spin_lock(&mm->umcg_lock);
	list_for_each_entry_rcu(list_entry, &mm->umcg_groups, list) {
		if (list_entry->group_id == group_id) {
			group = list_entry;
			break;
		}
	}

	if (group == NULL) {
		ret = -ESRCH;
		goto out;
	}

	spin_lock(&group->lock);

	if (group->nr_tasks > 0) {
		ret = -EBUSY;
		spin_unlock(&group->lock);
		goto out;
	}

	/* Tell group rcu readers that the group is going to be deleted. */
	group->nr_tasks = -1;

	spin_unlock(&group->lock);

	list_del_rcu(&group->list);
	kfree_rcu(group, rcu);

out:
	spin_unlock(&mm->umcg_lock);
	return ret;
}

/**
 * sys_umcg_poll_worker - poll an UNBLOCKED worker
 * @flags: reserved;
 * @ut:    the control struct umcg_task of the polled worker.
 *
 * The current task must be a UMCG server in POLLING state; if there are
 * UNBLOCKED workers in the server's group, take the earliest queued,
 * mark the worker as RUNNABLE.and return.
 *
 * If there are no unblocked workers, the syscall waits for one to become
 * available.
 *
 * Return:
 * 0       - Ok;
 * -EINTR  - a signal was received;
 * -EINVAL - one of the parameters is wrong, or a precondition was not met.
 */
SYSCALL_DEFINE2(umcg_poll_worker, u32, flags, struct umcg_task __user **, ut)
{
	struct umcg_group *group;
	struct task_struct *worker;
	struct task_struct *server = current;
	struct umcg_task __user *result;
	struct umcg_task_data *worker_utd, *server_utd;

	if (flags)
		return -EINVAL;

	rcu_read_lock();

	server_utd = rcu_dereference(server->umcg_task_data);

	if (!server_utd || server_utd->task_type != UMCG_TT_SERVER) {
		rcu_read_unlock();
		return -EINVAL;
	}

	umcg_detach_peer();

	group = server_utd->group;

	spin_lock(&group->lock);

	if (group->nr_waiting_workers == 0) {  /* Queue the server. */
		++group->nr_waiting_pollers;
		list_add_tail(&server_utd->list, &group->waiters);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock(&group->lock);
		rcu_read_unlock();

		freezable_schedule();

		rcu_read_lock();
		server_utd = rcu_dereference(server->umcg_task_data);

		if (!list_empty(&server_utd->list)) {
			spin_lock(&group->lock);
			list_del_init(&server_utd->list);
			--group->nr_waiting_pollers;
			spin_unlock(&group->lock);
		}

		if (signal_pending(current)) {
			rcu_read_unlock();
			return -EINTR;
		}

		worker = rcu_dereference(server_utd->peer);
		if (worker) {
			worker_utd = rcu_dereference(worker->umcg_task_data);
			result = worker_utd->umcg_task;
		} else
			result = NULL;

		rcu_read_unlock();

		if (put_user(result, ut))
			return umcg_segv(-EFAULT);
		return 0;
	}

	/* Pick up the first worker. */
	worker_utd = list_first_entry(&group->waiters, struct umcg_task_data,
					list);
	list_del_init(&worker_utd->list);
	worker = worker_utd->self;
	--group->nr_waiting_workers;

	umcg_lock_pair(server, worker);
	spin_unlock(&group->lock);

	if (WARN_ON(rcu_access_pointer(server_utd->peer) ||
			rcu_access_pointer(worker_utd->peer))) {
		/* This is unexpected. */
		rcu_read_unlock();
		return umcg_segv(-EINVAL);
	}
	rcu_assign_pointer(server_utd->peer, worker);
	rcu_assign_pointer(worker_utd->peer, current);

	umcg_unlock_pair(server, worker);

	result = worker_utd->umcg_task;
	rcu_read_unlock();

	if (put_state(result, UMCG_TASK_RUNNABLE))
		return umcg_segv(-EFAULT);

	if (put_user(result, ut))
		return umcg_segv(-EFAULT);

	return 0;
}

/**
 * sys_umcg_run_worker - "run" a RUNNABLE worker as a server
 * @flags:       reserved;
 * @worker_tid:  tid of the worker to run;
 * @ut:          the control struct umcg_task of the worker that blocked
 *               during this "run".
 *
 * The worker must be in RUNNABLE state. The server (=current task)
 * wakes the worker and blocks; when the worker, or one of the workers
 * in umcg_swap chain, blocks, the server is woken and the syscall returns
 * with ut indicating the blocked worker.
 *
 * If the worker exits or unregisters itself, the syscall succeeds with
 * ut == NULL.
 *
 * Return:
 * 0       - Ok;
 * -EINTR  - a signal was received;
 * -EINVAL - one of the parameters is wrong, or a precondition was not met.
 */
SYSCALL_DEFINE3(umcg_run_worker, u32, flags, u32, worker_tid,
		struct umcg_task __user **, ut)
{
	int ret = -EINVAL;
	struct task_struct *worker;
	struct task_struct *server = current;
	struct umcg_task __user *result = NULL;
	struct umcg_task_data *worker_utd;
	struct umcg_task_data *server_utd;
	struct umcg_task __user *server_ut;
	struct umcg_task __user *worker_ut;

	if (!ut)
		return -EINVAL;

	rcu_read_lock();
	server_utd = rcu_dereference(server->umcg_task_data);

	if (!server_utd || server_utd->task_type != UMCG_TT_SERVER)
		goto out_rcu;

	if (flags)
		goto out_rcu;

	worker = find_get_task_by_vpid(worker_tid);
	if (!worker) {
		ret = -ESRCH;
		goto out_rcu;
	}

	worker_utd = rcu_dereference(worker->umcg_task_data);
	if (!worker_utd)
		goto out_rcu;

	if (!READ_ONCE(worker_utd->in_wait)) {
		ret = -EAGAIN;
		goto out_rcu;
	}

	if (server_utd->group != worker_utd->group)
		goto out_rcu;

	if (rcu_access_pointer(server_utd->peer) != worker)
		umcg_detach_peer();

	if (!rcu_access_pointer(server_utd->peer)) {
		umcg_lock_pair(server, worker);
		WARN_ON(worker_utd->peer);
		rcu_assign_pointer(server_utd->peer, worker);
		rcu_assign_pointer(worker_utd->peer, server);
		umcg_unlock_pair(server, worker);
	}

	server_ut = server_utd->umcg_task;
	worker_ut = server_utd->umcg_task;

	rcu_read_unlock();

	ret = do_context_switch(worker);
	if (ret)
		return ret;

	rcu_read_lock();
	worker = rcu_dereference(server_utd->peer);
	if (worker) {
		worker_utd = rcu_dereference(worker->umcg_task_data);
		if (worker_utd)
			result = worker_utd->umcg_task;
	}
	rcu_read_unlock();

	if (put_user(result, ut))
		return -EFAULT;
	return 0;

out_rcu:
	rcu_read_unlock();
	return ret;
}

void umcg_on_block(void)
{
	struct umcg_task_data *utd = rcu_access_pointer(current->umcg_task_data);
	struct umcg_task __user *ut;
	struct task_struct *server;
	u32 state;

	if (utd->task_type != UMCG_TT_WORKER || utd->in_workqueue)
		return;

	ut = utd->umcg_task;

	if (get_user(state, (u32 __user *)ut)) {
		if (signal_pending(current))
			return;
		umcg_segv(0);
		return;
	}

	if (state != UMCG_TASK_RUNNING)
		return;

	state = UMCG_TASK_BLOCKED;
	if (put_user(state, (u32 __user *)ut)) {
		umcg_segv(0);
		return;
	}

	rcu_read_lock();
	server = rcu_dereference(utd->peer);
	rcu_read_unlock();

	if (server)
		WARN_ON(!try_to_wake_up(server, TASK_NORMAL, WF_CURRENT_CPU));
}

/* Return true to return to the user, false to keep waiting. */
static bool process_unblocked_worker(void)
{
	struct umcg_task_data *utd;
	struct umcg_group *group;

	rcu_read_lock();

	utd = rcu_dereference(current->umcg_task_data);
	group = utd->group;

	spin_lock(&group->lock);
	if (!list_empty(&utd->list)) {
		/* This was a spurious wakeup or an interrupt, do nothing. */
		spin_unlock(&group->lock);
		rcu_read_unlock();
		do_wait();
		return false;
	}

	if (group->nr_waiting_pollers > 0) {  /* Wake a server. */
		struct task_struct *server;
		struct umcg_task_data *server_utd = list_first_entry(
				&group->waiters, struct umcg_task_data, list);

		list_del_init(&server_utd->list);
		server = server_utd->self;
		--group->nr_waiting_pollers;

		umcg_lock_pair(server, current);
		spin_unlock(&group->lock);

		if (WARN_ON(server_utd->peer || utd->peer)) {
			umcg_segv(0);
			return true;
		}
		rcu_assign_pointer(server_utd->peer, current);
		rcu_assign_pointer(utd->peer, server);

		umcg_unlock_pair(server, current);
		rcu_read_unlock();

		if (put_state(utd->umcg_task, UMCG_TASK_RUNNABLE)) {
			umcg_segv(0);
			return true;
		}

		do_context_switch(server);
		return false;
	}

	/* Add to the queue. */
	++group->nr_waiting_workers;
	list_add_tail(&utd->list, &group->waiters);
	spin_unlock(&group->lock);
	rcu_read_unlock();

	do_wait();

	smp_rmb();
	if (!list_empty(&utd->list)) {
		spin_lock(&group->lock);
		list_del_init(&utd->list);
		--group->nr_waiting_workers;
		spin_unlock(&group->lock);
	}

	return false;
}

void umcg_on_wake(void)
{
	struct umcg_task_data *utd;
	struct umcg_task __user *ut;
	bool should_break = false;

	/* current->umcg_task_data is modified only from current. */
	utd = rcu_access_pointer(current->umcg_task_data);
	if (utd->task_type != UMCG_TT_WORKER || utd->in_workqueue)
		return;

	do {
		u32 state;

		if (fatal_signal_pending(current))
			return;

		if (signal_pending(current))
			return;

		ut = utd->umcg_task;

		if (get_state(ut, &state)) {
			if (signal_pending(current))
				return;
			goto segv;
		}

		if (state == UMCG_TASK_RUNNING && rcu_access_pointer(utd->peer))
			return;

		if (state == UMCG_TASK_BLOCKED || state == UMCG_TASK_RUNNING) {
			state = UMCG_TASK_UNBLOCKED;
			if (put_state(ut, state))
				goto segv;
		} else if (state != UMCG_TASK_UNBLOCKED) {
			goto segv;
		}

		utd->in_workqueue = true;
		should_break = process_unblocked_worker();
		utd->in_workqueue = false;
		if (should_break)
			return;

	} while (!should_break);

segv:
	umcg_segv(0);
}
