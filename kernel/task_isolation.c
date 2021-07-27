// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Implementation of task isolation.
 *
 * Authors:
 *   Chris Metcalf <cmetcalf@mellanox.com>
 *   Alex Belits <abelits@marvell.com>
 *   Yuri Norov <ynorov@marvell.com>
 *   Marcelo Tosatti <mtosatti@redhat.com>
 */

#include <linux/sched.h>
#include <linux/task_isolation.h>
#include <linux/prctl.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmstat.h>

static int tsk_isol_alloc_context(struct task_struct *task)
{
	struct isol_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (unlikely(!info))
		return -ENOMEM;

	task->isol_info = info;
	return 0;
}

void __tsk_isol_exit(struct task_struct *tsk)
{
	kfree(tsk->isol_info);
	tsk->isol_info = NULL;
}

int prctl_task_isolation_get(unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5)
{
	if (arg2 != PR_ISOL_MODE)
		return -EOPNOTSUPP;

	if (current->isol_info != NULL)
		return current->isol_info->mode;

	return PR_ISOL_MODE_NONE;
}


int prctl_task_isolation_set(unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5)
{
	int ret;

	if (arg2 != PR_ISOL_MODE)
		return -EOPNOTSUPP;

	if (arg3 != PR_ISOL_MODE_NORMAL)
		return -EINVAL;

	ret = tsk_isol_alloc_context(current);
	if (ret)
		return ret;

	current->isol_info->mode = arg3;
	return 0;
}

int prctl_task_isolation_enter(unsigned long arg2, unsigned long arg3,
			       unsigned long arg4, unsigned long arg5)
{

	if (current->isol_info == NULL)
		return -EINVAL;

	if (current->isol_info->mode != PR_ISOL_MODE_NORMAL)
		return -EINVAL;

	current->isol_info->active = 1;

	return 0;
}

int prctl_task_isolation_exit(unsigned long arg2, unsigned long arg3,
			      unsigned long arg4, unsigned long arg5)
{
	if (current->isol_info == NULL)
		return -EINVAL;

	if (current->isol_info->mode != PR_ISOL_MODE_NORMAL)
		return -EINVAL;

	current->isol_info->active = 0;

	return 0;
}

void __isolation_exit_to_user_mode_prepare(void)
{
	if (current->isol_info->mode != PR_ISOL_MODE_NORMAL)
		return;

	if (current->isol_info->active != 1)
		return;

	sync_vmstat();
}

