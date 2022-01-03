// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2007 IBM Corporation
 *
 *  Author: Cedric Le Goater <clg@fr.ibm.com>
 */

#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/sysctl.h>

#include <linux/stat.h>
#include <linux/capability.h>
#include <linux/slab.h>

static int msg_max_limit_min = MIN_MSGMAX;
static int msg_max_limit_max = HARD_MSGMAX;

static int msg_maxsize_limit_min = MIN_MSGSIZEMAX;
static int msg_maxsize_limit_max = HARD_MSGSIZEMAX;

static struct ctl_table mq_sysctls[] = {
	{
		.procname	= "queues_max",
		.data		= &init_ipc_ns.mq_queues_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "msg_max",
		.data		= &init_ipc_ns.mq_msg_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_max",
		.data		= &init_ipc_ns.mq_msgsize_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{
		.procname	= "msg_default",
		.data		= &init_ipc_ns.mq_msg_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_default",
		.data		= &init_ipc_ns.mq_msgsize_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{}
};

static struct ctl_table_set *
set_lookup(struct ctl_table_root *root)
{
	return &current->nsproxy->ipc_ns->set;
}

static int set_is_seen(struct ctl_table_set *set)
{
	return &current->nsproxy->ipc_ns->set == set;
}

static int set_permissions(struct ctl_table_header *head, struct ctl_table *table)
{
	struct ipc_namespace *ns = container_of(head->set, struct ipc_namespace, set);
	int mode;

	/* Allow users with CAP_SYS_RESOURCE unrestrained access */
	if (ns_capable(ns->user_ns, CAP_SYS_RESOURCE))
		mode = (table->mode & S_IRWXU) >> 6;
	else
	/* Allow all others at most read-only access */
		mode = table->mode & S_IROTH;
	return (mode << 6) | (mode << 3) | mode;
}

static struct ctl_table_root set_root = {
	.lookup = set_lookup,
	.permissions = set_permissions,
};

bool setup_mq_sysctls(struct ipc_namespace *ns)
{
#ifdef CONFIG_POSIX_MQUEUE_SYSCTL
	struct ctl_table *tbl;

	setup_sysctl_set(&ns->set, &set_root, set_is_seen);

	tbl = kmemdup(mq_sysctls, sizeof(mq_sysctls), GFP_KERNEL);
	if (tbl) {
		int i;

		for (i = 0; i < ARRAY_SIZE(mq_sysctls); i++) {
			if (tbl[i].data == &init_ipc_ns.mq_queues_max)
				tbl[i].data = &ns->mq_queues_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msg_max)
				tbl[i].data = &ns->mq_msg_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msgsize_max)
				tbl[i].data = &ns->mq_msgsize_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msg_default)
				tbl[i].data = &ns->mq_msg_default;

			else if (tbl[i].data == &init_ipc_ns.mq_msgsize_default)
				tbl[i].data = &ns->mq_msgsize_default;
			else
				tbl[i].data = NULL;
		}

		ns->sysctls = __register_sysctl_table(&ns->set, "fs/mqueue", tbl);
	}
	if (!ns->sysctls) {
		kfree(tbl);
		retire_sysctl_set(&ns->set);
		return false;
	}
#endif
	return true;
}

void retire_mq_sysctls(struct ipc_namespace *ns)
{
#ifdef CONFIG_POSIX_MQUEUE_SYSCTL
	struct ctl_table *tbl;

	tbl = ns->sysctls->ctl_table_arg;
	unregister_sysctl_table(ns->sysctls);
	retire_sysctl_set(&ns->set);
	kfree(tbl);
#endif
}
