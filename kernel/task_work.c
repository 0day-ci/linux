// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/tracehook.h>

static struct callback_head work_exited; /* all we need is ->next == NULL */

/**
 * task_work_add_nonotify - ask the @task to execute @work->func()
 * @task: the task which should run the callback
 * @work: the callback to run
 * @notify: how to notify the targeted task
 *
 * Queue @work for task_work_run() below.  If the targeted task is exiting, then
 * an error is returned and the work item is not queued. It's up to the caller
 * to arrange for an alternative mechanism in that case.
 *
 * The caller needs to notify @task to make sure @work is actually run.
 *
 * Note: there is no ordering guarantee on works queued here. The task_work
 * list is LIFO.
 *
 * RETURNS:
 * 0 if succeeds or -ESRCH.
 */
int task_work_add_nonotify(struct task_struct *task, struct callback_head *work)
{
	struct callback_head *head;

	/* record the work call stack in order to print it in KASAN reports */
	kasan_record_aux_stack(work);

	do {
		head = READ_ONCE(task->task_works);
		if (unlikely(head == &work_exited))
			return -ESRCH;
		work->next = head;
	} while (cmpxchg(&task->task_works, head, work) != head);

	return 0;
}

/**
 * task_work_add - ask the @task to execute @work->func()
 * @task: the task which should run the callback
 * @work: the callback to run
 * @notify: how to notify the targeted task
 *
 * Queue @work using task_work_add_nonotify() and notify the task to actually
 * run it when the task exits the kernel and returns to user mode, or before
 * entering guest mode.
 *
 * RETURNS:
 * 0 if succeeds or -ESRCH.
 */
int task_work_add(struct task_struct *task, struct callback_head *work)
{
	int ret;

	ret = task_work_add_nonotify(task, work);
	if (!ret)
		set_notify_resume(task);
	return ret;
}

/**
 * task_work_cancel_match - cancel a pending work added by task_work_add()
 * @task: the task which should execute the work
 * @match: match function to call
 *
 * RETURNS:
 * The found work or NULL if not found.
 */
struct callback_head *
task_work_cancel_match(struct task_struct *task,
		       bool (*match)(struct callback_head *, void *data),
		       void *data)
{
	struct callback_head **pprev = &task->task_works;
	struct callback_head *work;
	unsigned long flags;

	if (likely(!task->task_works))
		return NULL;
	/*
	 * If cmpxchg() fails we continue without updating pprev.
	 * Either we raced with task_work_add() which added the
	 * new entry before this work, we will find it again. Or
	 * we raced with task_work_run(), *pprev == NULL/exited.
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	while ((work = READ_ONCE(*pprev))) {
		if (!match(work, data))
			pprev = &work->next;
		else if (cmpxchg(pprev, work, work->next) == work)
			break;
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	return work;
}

static bool task_work_func_match(struct callback_head *cb, void *data)
{
	return cb->func == data;
}

/**
 * task_work_cancel - cancel a pending work added by task_work_add()
 * @task: the task which should execute the work
 * @func: identifies the work to remove
 *
 * Find the last queued pending work with ->func == @func and remove
 * it from queue.
 *
 * RETURNS:
 * The found work or NULL if not found.
 */
struct callback_head *
task_work_cancel(struct task_struct *task, task_work_func_t func)
{
	return task_work_cancel_match(task, task_work_func_match, func);
}

/**
 * task_work_run - execute the works added by task_work_add()
 *
 * Flush the pending works. Should be used by the core kernel code.
 * Called before the task returns to the user-mode or stops, or when
 * it exits. In the latter case task_work_add() can no longer add the
 * new work after task_work_run() returns.
 */
void task_work_run(void)
{
	struct task_struct *task = current;
	struct callback_head *work, *head, *next;

	for (;;) {
		/*
		 * work->func() can do task_work_add(), do not set
		 * work_exited unless the list is empty.
		 */
		do {
			head = NULL;
			work = READ_ONCE(task->task_works);
			if (!work) {
				if (task->flags & PF_EXITING)
					head = &work_exited;
				else
					break;
			}
		} while (cmpxchg(&task->task_works, work, head) != work);

		if (!work)
			break;
		/*
		 * Synchronize with task_work_cancel(). It can not remove
		 * the first entry == work, cmpxchg(task_works) must fail.
		 * But it can remove another entry from the ->next list.
		 */
		raw_spin_lock_irq(&task->pi_lock);
		raw_spin_unlock_irq(&task->pi_lock);

		do {
			next = work->next;
			work->func(work);
			work = next;
			cond_resched();
		} while (work);
	}
}
