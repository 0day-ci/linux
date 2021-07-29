// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2007 IBM Corporation
 *
 *  Author: Cedric Le Goater <clg@fr.ibm.com>
 */

#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/stat.h>

#ifdef CONFIG_PROC_SYSCTL
static void *get_mq(struct ctl_table *table)
{
	char *which = table->data;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;
	which = (which - (char *)&init_ipc_ns) + (char *)ipc_ns;
	return which;
}

static int proc_mq_dointvec(struct ctl_table *table, int write,
			    void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table mq_table;
	memcpy(&mq_table, table, sizeof(mq_table));
	mq_table.data = get_mq(table);

	return proc_dointvec(&mq_table, write, buffer, lenp, ppos);
}

static int proc_mq_dointvec_minmax(struct ctl_table *table, int write,
		void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table mq_table;
	memcpy(&mq_table, table, sizeof(mq_table));
	mq_table.data = get_mq(table);

	return proc_dointvec_minmax(&mq_table, write, buffer,
					lenp, ppos);
}
#else
#define proc_mq_dointvec NULL
#define proc_mq_dointvec_minmax NULL
#endif

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
		.proc_handler	= proc_mq_dointvec,
	},
	{
		.procname	= "msg_max",
		.data		= &init_ipc_ns.mq_msg_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_max",
		.data		= &init_ipc_ns.mq_msgsize_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{
		.procname	= "msg_default",
		.data		= &init_ipc_ns.mq_msg_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_default",
		.data		= &init_ipc_ns.mq_msgsize_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_mq_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{}
};

static int set_is_seen(struct ctl_table_set *set)
{
	return &current->nsproxy->ipc_ns->mq_set == set;
}

static struct ctl_table_set *
set_lookup(struct ctl_table_root *root)
{
	return &current->nsproxy->ipc_ns->mq_set;
}

static int set_permissions(struct ctl_table_header *head,
				struct ctl_table *table)
{
	struct ipc_namespace *ipc_ns =
		container_of(head->set, struct ipc_namespace, mq_set);
	struct user_namespace *user_ns = ipc_ns->user_ns;
	int mode;

	/* Allow users with CAP_SYS_RESOURCE unrestrained access */
	if (ns_capable(user_ns, CAP_SYS_RESOURCE))
		mode = (table->mode & S_IRWXU) >> 6;
	else
	/* Allow all others at most read-only access */
		mode = table->mode & S_IROTH;
	return (mode << 6) | (mode << 3) | mode;
}

static void set_ownership(struct ctl_table_header *head,
				struct ctl_table *table,
				kuid_t *uid, kgid_t *gid)
{
	struct ipc_namespace *ipc_ns =
		container_of(head->set, struct ipc_namespace, mq_set);
	struct user_namespace *user_ns = ipc_ns->user_ns;
	kuid_t ns_root_uid;
	kgid_t ns_root_gid;

	ns_root_uid = make_kuid(user_ns, 0);
	if (uid_valid(ns_root_uid))
		*uid = ns_root_uid;

	ns_root_gid = make_kgid(user_ns, 0);
	if (gid_valid(ns_root_gid))
		*gid = ns_root_gid;
}

static struct ctl_table_root mq_sysctl_root = {
	.lookup = set_lookup,
	.permissions = set_permissions,
	.set_ownership = set_ownership,
};

bool setup_mq_sysctls(struct ipc_namespace *ns)
{
	struct ctl_table *tbl;

	if (!mq_sysctl_table)
		return false;

	setup_sysctl_set(&ns->mq_set, &mq_sysctl_root, set_is_seen);
	tbl = kmemdup(mq_sysctls, sizeof(mq_sysctls), GFP_KERNEL);
	if (!tbl)
		goto out;

	ns->sysctls = __register_sysctl_table(&ns->mq_set, "fs/mqueue", tbl);
	if (!ns->sysctls)
		goto out1;

	return true;

out1:
	kfree(tbl);
	retire_sysctl_set(&ns->mq_set);
out:
	return false;
}

void retire_mq_sysctls(struct ipc_namespace *ns)
{
	struct ctl_table *tbl;

	if (!ns->sysctls)
		return;

	tbl = ns->sysctls->ctl_table_arg;
	unregister_sysctl_table(ns->sysctls);
	retire_sysctl_set(&ns->mq_set);
	kfree(tbl);
}

struct ctl_table_header *mq_register_sysctl_table(void)
{
	static struct ctl_table empty[1];

	/*
	 * Register the fs/mqueue directory in the default set so that
	 * registrations in the child sets work properly.
	 */
	return register_sysctl("fs/mqueue", empty);
}
