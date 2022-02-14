/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime reactor interface.
 *
 * A runtime monitor can cause a reaction to the detection of an
 * exception on the model's execution. By default, the monitors have
 * tracing reactions, printing the monitor output via tracepoints.
 * But other reactions can be added (on-demand) via this interface.
 *
 * == Registering reactors ==
 *
 * The struct rv_reactor defines a callback function to be executed
 * in case of a model exception happens. The callback function
 * receives a message to be (optionally) printed before executing
 * the reaction.
 *
 * A RV reactor is registered via:
 *   int rv_register_reactor(struct rv_reactor *reactor)
 * And unregistered via:
 *   int rv_unregister_reactor(struct rv_reactor *reactor)
 *
 * These functions are exported to modules, enabling reactors to be
 * dynamically loaded.
 *
 * == User interface ==
 *
 * The user interface resembles the kernel tracing interface and
 * presents these files:
 *
 *  "available_reactors"
 *    - List the available reactors, one per line.
 *
 *    For example:
 *    [root@f32 rv]# cat available_reactors
 *    nop
 *    panic
 *    printk
 *
 *  "reacting_on"
 *    - It is an on/off general switch for reactors, disabling
 *    all reactions.
 *
 *  "monitors/MONITOR/reactors"
 *    - List available reactors, with the select reaction for the given
 *    MONITOR inside []. The defaul one is the nop (no operation)
 *    reactor.
 *    - Writing the name of an reactor enables it to the given
 *    MONITOR.
 *
 *    For example:
 *    [root@f32 rv]# cat monitors/wip/reactors
 *    [nop]
 *    panic
 *    printk
 *    [root@f32 rv]# echo panic > monitors/wip/reactors
 *    [root@f32 rv]# cat monitors/wip/reactors
 *    nop
 *    [panic]
 *    printk
 *
 * Copyright (C) 2019-2022 Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <linux/slab.h>

#include "rv.h"

bool __read_mostly reacting_on = false;
EXPORT_SYMBOL_GPL(reacting_on);

/*
 * Interface for the reactor register.
 */
LIST_HEAD(rv_reactors_list);

struct rv_reactor_def *get_reactor_rdef_by_name(char *name)
{
	struct rv_reactor_def *r;
	list_for_each_entry(r, &rv_reactors_list, list) {
		if (strcmp(name, r->reactor->name) == 0)
			return r;
	}

	return NULL;
}

/*
 * Available reactors seq functions.
 */
static int reactors_show(struct seq_file *m, void *p)
{
	struct rv_reactor_def *rea_def = p;
	seq_printf(m, "%s\n", rea_def->reactor->name);
	return 0;
}

static void reactors_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&rv_interface_lock);
}

static void *reactors_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&rv_interface_lock);
	return seq_list_start(&rv_reactors_list, *pos);
}

static void *reactors_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &rv_reactors_list, pos);
}


/*
 * available reactors seq definition.
 */
static const struct seq_operations available_reactors_seq_ops = {
	.start	= reactors_start,
	.next	= reactors_next,
	.stop	= reactors_stop,
	.show	= reactors_show
};

/*
 * available_reactors interface.
 */
static int available_reactors_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &available_reactors_seq_ops);
};

static struct file_operations available_reactors_ops = {
	.open    = available_reactors_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * Monitor reactor file.
 */
static int monitor_reactor_show(struct seq_file *m, void *p)
{
	struct rv_monitor_def *mdef = m->private;
	struct rv_reactor_def *rdef = p;

	if (mdef->rdef == rdef)
		seq_printf(m, "[%s]\n", rdef->reactor->name);
	else
		seq_printf(m, "%s\n", rdef->reactor->name);
	return 0;
}

/*
 * available reactors seq definition.
 */
static const struct seq_operations monitor_reactors_seq_ops = {
	.start	= reactors_start,
	.next	= reactors_next,
	.stop	= reactors_stop,
	.show	= monitor_reactor_show
};

static ssize_t
monitor_reactors_write(struct file *file, const char __user *user_buf,
		      size_t count, loff_t *ppos)
{
	char buff[MAX_RV_REACTOR_NAME_SIZE+1];
	struct rv_monitor_def *mdef;
	struct rv_reactor_def *rdef;
	struct seq_file *seq_f;
	int retval = -EINVAL;
	char *ptr = buff;
	int len;

	if (count < 1 || count > MAX_RV_REACTOR_NAME_SIZE+1)
		return -EINVAL;

	memset(buff, 0, sizeof(buff));

	retval = simple_write_to_buffer(buff, sizeof(buff)-1, ppos, user_buf,
					count);
	if (!retval)
		return -EFAULT;

	len = strlen(ptr);
	if (!len)
		return count;
	/*
	 * remove the \n
	 */
	ptr[len-1]='\0';

	/*
	 * See monitor_reactors_open()
	 */
	seq_f = file->private_data;
	mdef = seq_f->private;

	mutex_lock(&rv_interface_lock);

	retval = -EINVAL;

	/*
	 * nop special case: disable reacting.
	 */
	if (strcmp(ptr, "nop") == 0) {

		if (mdef->monitor->enabled)
			mdef->monitor->stop();

		mdef->rdef = get_reactor_rdef_by_name("nop");
		mdef->reacting = false;
		mdef->monitor->react = NULL;

		if (mdef->monitor->enabled)
			mdef->monitor->start();

		retval = count;
		goto unlock;
	}

	list_for_each_entry(rdef, &rv_reactors_list, list) {
		if (strcmp(ptr, rdef->reactor->name) == 0) {
			/*
			 * found!
			 */
			if (mdef->monitor->enabled)
				mdef->monitor->stop();

			mdef->rdef = rdef;
			mdef->reacting = true;
			mdef->monitor->react = rdef->reactor->react;

			if (mdef->monitor->enabled)
				mdef->monitor->start();

			retval=count;
			break;
		}
	}

unlock:
	mutex_unlock(&rv_interface_lock);

	return retval;
}

/*
 * available_reactors interface.
 */
static int monitor_reactors_open(struct inode *inode, struct file *file)
{
	/*
	 * create file "private" info is stored in the inode->i_private
	 */
	struct rv_monitor_def *mdef = inode->i_private;
	struct seq_file *seq_f;
	int ret;


	ret = seq_open(file, &monitor_reactors_seq_ops);
	if (ret < 0)
		return ret;

	/*
	 * seq_open stores the seq_file on the file->private data.
	 */
	seq_f = file->private_data;
	/*
	 * Copy the create file "private" data to the seq_file
	 * private data.
	 */
	seq_f->private = mdef;

	return 0;
};

static struct file_operations monitor_reactors_ops = {
	.open    = monitor_reactors_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
	.write = monitor_reactors_write
};

static int __rv_register_reactor(struct rv_reactor *reactor)
{
	struct rv_reactor_def *r;

	list_for_each_entry(r, &rv_reactors_list, list) {
		if (strcmp(reactor->name, r->reactor->name) == 0) {
			pr_info("Reactor %s is already registered\n",
				reactor->name);
			return -EINVAL;
		}
	}

	r = kzalloc(sizeof(struct rv_reactor_def), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->reactor = reactor;
	r->counter = 0;

	list_add_tail(&r->list, &rv_reactors_list);

	return 0;
}

/**
 * rv_register_reactor - register a rv reactor.
 * @reactor:    The rv_reactor to be registered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_register_reactor(struct rv_reactor *reactor)
{
	int retval = 0;

	if (strlen(reactor->name) >= MAX_RV_REACTOR_NAME_SIZE) {
		pr_info("Reactor %s has a name longer than %d\n",
			reactor->name, MAX_RV_MONITOR_NAME_SIZE);
		return -EINVAL;
	}

	mutex_lock(&rv_interface_lock);
	retval = __rv_register_reactor(reactor);
	mutex_unlock(&rv_interface_lock);
	return retval;
}
EXPORT_SYMBOL_GPL(rv_register_reactor);

/**
 * rv_unregister_reactor - unregister a rv reactor.
 * @reactor:    The rv_reactor to be unregistered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_unregister_reactor(struct rv_reactor *reactor)
{
	struct rv_reactor_def *ptr, *next;

	mutex_lock(&rv_interface_lock);

	list_for_each_entry_safe(ptr, next, &rv_reactors_list, list) {
		if (strcmp(reactor->name, ptr->reactor->name) == 0) {

			if (!ptr->counter) {
				list_del(&ptr->list);
			} else {
				printk("rv: the rv_reactor %s is in use by %d monitor(s)\n",
					ptr->reactor->name, ptr->counter);
				printk("rv: the rv_reactor %s cannot be removed\n",
					ptr->reactor->name);
				return -EBUSY;
			}

		}
	}

	mutex_unlock(&rv_interface_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(rv_unregister_reactor);

/*
 * reacting_on interface.
 */
static ssize_t reacting_on_read_data(struct file *filp,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buff[4];

	memset(buff, 0, sizeof(buff));

	mutex_lock(&rv_interface_lock);
	sprintf(buff, "%d\n", reacting_on);
	mutex_unlock(&rv_interface_lock);

	return simple_read_from_buffer(user_buf, count, ppos,
				       buff, strlen(buff)+1);
}

static void turn_reacting_off(void)
{
	reacting_on=false;
}

static void turn_reacting_on(void)
{
	reacting_on=true;
}

static ssize_t
reacting_on_write_data(struct file *filp, const char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	int retval;
	u64 val;

	retval = kstrtoull_from_user(user_buf, count, 10, &val);
	if (retval)
		return retval;

        retval = count;

	mutex_lock(&rv_interface_lock);

	switch (val) {
		case 0:
			turn_reacting_off();
			break;
		case 1:
			turn_reacting_on();
			break;
		default:
			retval = -EINVAL;
	}

	mutex_unlock(&rv_interface_lock);

	return retval;
}

static const struct file_operations reacting_on_fops = {
	.open   = simple_open,
	.llseek = no_llseek,
	.write  = reacting_on_write_data,
	.read   = reacting_on_read_data,
};


int reactor_create_monitor_files(struct rv_monitor_def *mdef)
{
	struct dentry *tmp;

	tmp = rv_create_file("reactors", 0400, mdef->root_d, mdef,
			     &monitor_reactors_ops);
	if (!tmp)
		return -ENOMEM;

	/*
	 * Configure as the rv_nop reactor.
	 */
	mdef->rdef = get_reactor_rdef_by_name("nop");
	mdef->reacting = false;
	return 0;
}

/*
 * None reactor register
 */
static void rv_nop_reaction(char *msg)
{
	return;
}

struct rv_reactor rv_nop = {
	.name = "nop",
	.description = "no-operation reactor: do nothing.",
	.react = rv_nop_reaction
};

/*
 * This section collects the rv/ root dir files and folders.
 */
int init_rv_reactors(struct dentry *root_dir)
{
	rv_create_file("available_reactors", 0400, root_dir, NULL,
		       &available_reactors_ops);
	rv_create_file("reacting_on", 0600, root_dir, NULL, &reacting_on_fops);

	__rv_register_reactor(&rv_nop);

	return 0;
}
