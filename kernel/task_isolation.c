// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Implementation of task isolation.
 *
 * Authors:
 *   Chris Metcalf <cmetcalf@mellanox.com>
 *   Alex Belits <abelits@belits.com>
 *   Yuri Norov <ynorov@marvell.com>
 *   Marcelo Tosatti <mtosatti@redhat.com>
 */

#include <linux/sched.h>
#include <linux/task_isolation.h>
#include <linux/prctl.h>
#include <linux/slab.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/vmstat.h>

static unsigned long default_quiesce_mask;

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

static int prctl_task_isolation_feat_quiesce(unsigned long type)
{
	switch (type) {
	case 0:
		return ISOL_F_QUIESCE_VMSTATS;
	case ISOL_F_QUIESCE_DEFMASK:
		return default_quiesce_mask;
	default:
		break;
	}

	return -EINVAL;
}

static int task_isolation_get_quiesce(void)
{
	if (current->isol_info != NULL)
		return current->isol_info->quiesce_mask;

	return 0;
}

static int task_isolation_set_quiesce(unsigned long quiesce_mask)
{
	if (quiesce_mask != ISOL_F_QUIESCE_VMSTATS && quiesce_mask != 0)
		return -EINVAL;

	current->isol_info->quiesce_mask = quiesce_mask;
	return 0;
}

int prctl_task_isolation_feat(unsigned long feat, unsigned long arg3,
			      unsigned long arg4, unsigned long arg5)
{
	switch (feat) {
	case 0:
		return ISOL_F_QUIESCE;
	case ISOL_F_QUIESCE:
		return prctl_task_isolation_feat_quiesce(arg3);
	default:
		break;
	}
	return -EINVAL;
}

int prctl_task_isolation_get(unsigned long feat, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5)
{
	switch (feat) {
	case ISOL_F_QUIESCE:
		return task_isolation_get_quiesce();
	default:
		break;
	}
	return -EINVAL;
}

int prctl_task_isolation_set(unsigned long feat, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5)
{
	int ret;
	bool err_free_ctx = false;

	if (current->isol_info == NULL)
		err_free_ctx = true;

	ret = tsk_isol_alloc_context(current);
	if (ret)
		return ret;

	switch (feat) {
	case ISOL_F_QUIESCE:
		ret = task_isolation_set_quiesce(arg3);
		if (ret)
			break;
		return 0;
	default:
		break;
	}

	if (err_free_ctx)
		__tsk_isol_exit(current);
	return -EINVAL;
}

int prctl_task_isolation_ctrl_set(unsigned long feat, unsigned long arg3,
				  unsigned long arg4, unsigned long arg5)
{
	if (current->isol_info == NULL)
		return -EINVAL;

	if (feat != ISOL_F_QUIESCE && feat != 0)
		return -EINVAL;

	current->isol_info->active_mask = feat;
	return 0;
}

int prctl_task_isolation_ctrl_get(unsigned long arg2, unsigned long arg3,
				  unsigned long arg4, unsigned long arg5)
{
	if (current->isol_info == NULL)
		return 0;

	return current->isol_info->active_mask;
}

void __isolation_exit_to_user_mode_prepare(void)
{
	struct isol_info *i = current->isol_info;

	if (i->active_mask != ISOL_F_QUIESCE)
		return;

	if (i->quiesce_mask & ISOL_F_QUIESCE_VMSTATS)
		sync_vmstat();
}

struct qoptions {
	unsigned long mask;
	char *name;
};

static struct qoptions qopts[] = {
	{ISOL_F_QUIESCE_VMSTATS, "vmstat"},
};

#define QLEN (sizeof(qopts) / sizeof(struct qoptions))

static ssize_t default_quiesce_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	char *p, *s;
	unsigned long defmask = 0;

	s = (char *)buf;
	if (count == 1 && strlen(strim(s)) == 0) {
		default_quiesce_mask = 0;
		return count;
	}

	while ((p = strsep(&s, ",")) != NULL) {
		int i;
		bool found = false;

		if (!*p)
			continue;

		for (i = 0; i < QLEN; i++) {
			struct qoptions *opt = &qopts[i];

			if (strncmp(strim(p), opt->name, strlen(opt->name)) == 0) {
				defmask |= opt->mask;
				found = true;
				break;
			}
		}
		if (found == true)
			continue;
		return -EINVAL;
	}
	default_quiesce_mask = defmask;

	return count;
}

#define MAXARRLEN 100

static ssize_t default_quiesce_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	int i;
	char tbuf[MAXARRLEN] = "";

	for (i = 0; i < QLEN; i++) {
		struct qoptions *opt = &qopts[i];

		if (default_quiesce_mask & opt->mask) {
			strlcat(tbuf, opt->name, MAXARRLEN);
			strlcat(tbuf, "\n", MAXARRLEN);
		}
	}

	return sprintf(buf, "%s", tbuf);
}

static struct kobj_attribute default_quiesce_attr =
				__ATTR_RW(default_quiesce);

static ssize_t available_quiesce_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	int i;
	char tbuf[MAXARRLEN] = "";

	for (i = 0; i < QLEN; i++) {
		struct qoptions *opt = &qopts[i];

		strlcat(tbuf, opt->name, MAXARRLEN);
		strlcat(tbuf, "\n", MAXARRLEN);
	}

	return sprintf(buf, "%s", tbuf);
}

static struct kobj_attribute available_quiesce_attr =
				__ATTR_RO(available_quiesce);

static struct attribute *task_isol_attrs[] = {
	&available_quiesce_attr.attr,
	&default_quiesce_attr.attr,
	NULL,
};

static const struct attribute_group task_isol_attr_group = {
	.attrs = task_isol_attrs,
	.bin_attrs = NULL,
};

static int __init task_isol_ksysfs_init(void)
{
	int ret;
	struct kobject *task_isol_kobj;

	task_isol_kobj = kobject_create_and_add("task_isolation",
						kernel_kobj);
	if (!task_isol_kobj) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sysfs_create_group(task_isol_kobj, &task_isol_attr_group);
	if (ret)
		goto out_task_isol_kobj;

	return 0;

out_task_isol_kobj:
	kobject_put(task_isol_kobj);
out:
	return ret;
}

arch_initcall(task_isol_ksysfs_init);
