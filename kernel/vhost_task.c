// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Oracle Corporation
 */
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/sched/task.h>
#include <linux/sched/vhost_task.h>
#include <linux/sched/signal.h>

enum vhost_task_flags {
	VHOST_TASK_FLAGS_STOP,
};

static void vhost_task_fn(void *data)
{
	struct vhost_task *vtsk = data;
	int ret;

	ret = vtsk->fn(vtsk->data);
	complete(&vtsk->exited);
	do_exit(ret);
}

/**
 * vhost_task_stop - stop a vhost_task
 * @vtsk: vhost_task to stop
 *
 * Callers must call vhost_task_should_stop and return from their worker
 * function when it returns true;
 */
void vhost_task_stop(struct vhost_task *vtsk)
{
	pid_t pid = vtsk->task->pid;

	set_bit(VHOST_TASK_FLAGS_STOP, &vtsk->flags);
	wake_up_process(vtsk->task);
	/*
	 * Make sure vhost_task_fn is no longer accessing the vhost_task before
	 * freeing it below. If userspace crashed or exited without closing,
	 * then the vhost_task->task could already be marked dead so
	 * kernel_wait will return early.
	 */
	wait_for_completion(&vtsk->exited);
	/*
	 * If we are just closing/removing a device and the parent process is
	 * not exiting then reap the task.
	 */
	kernel_wait4(pid, NULL, __WCLONE, NULL);
	kfree(vtsk);
}
EXPORT_SYMBOL_GPL(vhost_task_stop);

/**
 * vhost_task_should_stop - should the vhost task return from the work function
 */
bool vhost_task_should_stop(struct vhost_task *vtsk)
{
	return test_bit(VHOST_TASK_FLAGS_STOP, &vtsk->flags);
}
EXPORT_SYMBOL_GPL(vhost_task_should_stop);

/**
 * vhost_task_create - create a copy of a process to be used by the kernel
 * @fn: thread stack
 * @arg: data to be passed to fn
 * @node: numa node to allocate task from
 *
 * This returns a specialized task for use by the vhost layer or NULL on
 * failure. The returned task is inactive, and the caller must fire it up
 * through vhost_task_start().
 */
struct vhost_task *vhost_task_create(int (*fn)(void *), void *arg, int node)
{
	struct kernel_clone_args args = {
		.flags		= CLONE_FS | CLONE_UNTRACED | CLONE_VM,
		.exit_signal	= 0,
		.stack		= (unsigned long)vhost_task_fn,
		.worker_flags	= USER_WORKER | USER_WORKER_NO_FILES |
				  USER_WORKER_SIG_IGN,
	};
	struct vhost_task *vtsk;
	struct task_struct *tsk;

	vtsk = kzalloc(GFP_KERNEL, sizeof(*vtsk));
	if (!vtsk)
		return ERR_PTR(-ENOMEM);

	init_completion(&vtsk->exited);
	vtsk->data = arg;
	vtsk->fn = fn;
	args.stack_size =  (unsigned long)vtsk;

	tsk = copy_process(NULL, 0, node, &args);
	if (IS_ERR(tsk)) {
		kfree(vtsk);
		return NULL;
	}

	vtsk->task = tsk;

	return vtsk;
}
EXPORT_SYMBOL_GPL(vhost_task_create);

/**
 * vhost_task_start - start a vhost_task created with vhost_task_create
 * @vtsk: vhost_task to wake up
 * @namefmt: printf-style format string for the thread name
 */
void vhost_task_start(struct vhost_task *vtsk, const char namefmt[], ...)
{
	char name[TASK_COMM_LEN];
	va_list args;

	va_start(args, namefmt);
	vsnprintf(name, sizeof(name), namefmt, args);
	set_task_comm(vtsk->task, name);
	va_end(args);

	wake_up_new_task(vtsk->task);
}
EXPORT_SYMBOL_GPL(vhost_task_start);
