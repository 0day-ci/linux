/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is the online Runtime Verification (RV) interface.
 *
 * RV is a lightweight (yet rigorous) method that complements classical
 * exhaustive verification techniques (such as model checking and
 * theorem proving) with a more practical approach to complex systems.
 *
 * RV works by analyzing the trace of the system's actual execution,
 * comparing it against a formal specification of the system behavior.
 * RV can give precise information on the runtime behavior of the
 * monitored system while enabling the reaction for unexpected
 * events, avoiding, for example, the propagation of a failure on
 * safety-critical systems.
 *
 * The development of this interface roots in the development of the
 * paper:
 *
 * DE OLIVEIRA, Daniel Bristot; CUCINOTTA, Tommaso; DE OLIVEIRA, Romulo
 * Silva. Efficient formal verification for the Linux kernel. In:
 * International Conference on Software Engineering and Formal Methods.
 * Springer, Cham, 2019. p. 315-332.
 *
 * And:
 *
 * DE OLIVEIRA, Daniel Bristot, et al. Automata-based formal analysis
 * and verification of the real-time Linux kernel. PhD Thesis, 2020.
 *
 * == Runtime monitor interface ==
 *
 * A monitor is the central part of the runtime verification of a system.
 *
 * The monitor stands in between the formal specification of the desired
 * (or undesired) behavior, and the trace of the actual system system.
 *
 * In Linux terms, the runtime verification monitors are encapsulated
 * inside the "RV monitor" abstraction. A RV monitor includes a reference
 * model of the system, a set of instances of the monitor (per-cpu monitor,
 * per-task monitor, and so on), and the helper functions that glue the
 * monitor to the system via trace. Generally, a monitor includes some form
 * of trace output as a reaction for event parsing and exceptions,
 * as depicted bellow:
 *
 * Linux  +----- RV Monitor ----------------------------------+ Formal
 *  Realm |                                                   |  Realm
 *  +-------------------+     +----------------+     +-----------------+
 *  |   Linux kernel    |     |     Monitor    |     |     Reference   |
 *  |     Tracing       |  -> |   Instance(s)  | <-  |       Model     |
 *  | (instrumentation) |     | (verification) |     | (specification) |
 *  +-------------------+     +----------------+     +-----------------+
 *         |                          |                       |
 *         |                          V                       |
 *         |                     +----------+                 |
 *         |                     | Reaction |                 |
 *         |                     +--+--+--+-+                 |
 *         |                        |  |  |                   |
 *         |                        |  |  +-> trace output ?  |
 *         +------------------------|--|----------------------+
 *                                  |  +----> panic ?
 *                                  +-------> <user-specified>
 *
 * This file implements the interface for loading RV monitors, and
 * to control the verification session.
 *
 * == Registering monitors ==
 *
 * The struct rv_monitor defines a set of callback functions to control
 * a verification session. For instance, when a given monitor is enabled,
 * the "start" callback function is called to hook the instrumentation
 * functions to the kernel trace events. The "stop" function is called
 * when disabling the verification session.
 *
 * A RV monitor is registered via:
 *   int rv_register_monitor(struct rv_monitor *monitor);
 * And unregistered via:
 *   int rv_unregister_monitor(struct rv_monitor *monitor);
 *
 * These functions are exported to modules, enabling verification monitors
 * to be dynamically loaded.
 *
 * == User interface ==
 *
 * The user interface resembles kernel tracing interface. It presents
 * these files:
 *
 *  "available_monitors"
 *    - List the available monitors, one per line.
 *
 *    For example:
 *    [root@f32 rv]# cat available_monitors
 *    wip
 *    wwnr
 *
 *  "enabled_monitors"
 *    - Lists the enabled monitors, one per line;
 *    - Writing to it enables a given monitor;
 *    - Writing a monitor name with a '-' prefix disables it;
 *    - Truncating the file disables all enabled monitors.
 *
 *    For example:
 *    [root@f32 rv]# cat enabled_monitors
 *    [root@f32 rv]# echo wip > enabled_monitors
 *    [root@f32 rv]# echo wwnr >> enabled_monitors
 *    [root@f32 rv]# cat enabled_monitors
 *    wip
 *    wwnr
 *    [root@f32 rv]# echo -wip >> enabled_monitors
 *    [root@f32 rv]# cat enabled_monitors
 *    wwnr
 *    [root@f32 rv]# echo > enabled_monitors
 *    [root@f32 rv]# cat enabled_monitors
 *    [root@f32 rv]#
 *
 *    Note that more than one monitor can be enabled concurrently.
 *
 *  "monitoring_on"
 *    - It is an on/off general switcher for monitoring. Note
 *    that it does not disable enabled monitors, but stop the per-entity
 *    monitors of monitoring the events received from the system.
 *    It resambles the "tracing_on" switcher.
 *
 *  "monitors/"
 *    Each monitor will have its one directory inside "monitors/". There
 *    the monitor specific files will be presented.
 *    The "monitors/" directory resambles the "events" directory on
 *    tracefs.
 *
 *    For example:
 *    [root@f32 rv]# cd monitors/wip/
 *    [root@f32 wip]# ls
 *    desc  enable
 *    [root@f32 wip]# cat desc
 *    auto-generated wakeup in preemptive monitor.
 *    [root@f32 wip]# cat enable
 *    0
 *
 * Copyright (C) 2019-2022 Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "rv.h"

DEFINE_MUTEX(rv_interface_lock);
struct rv_interface rv_root;

struct dentry *get_monitors_root(void)
{
	return rv_root.monitors_dir;
}

/*
 * Monitoring on global switcher!
 */
bool __read_mostly monitoring_on = false;
EXPORT_SYMBOL_GPL(monitoring_on);

/*
 * Interface for the monitor register.
 */
LIST_HEAD(rv_monitors_list);

/*
 * This section collects the monitor/ files and folders.
 */
static ssize_t monitor_enable_read_data(struct file *filp,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct rv_monitor_def *mdef = filp->private_data;
	char buff[4];

	memset(buff, 0, sizeof(buff));

	mutex_lock(&rv_interface_lock);
	sprintf(buff, "%x\n", mdef->monitor->enabled);
	mutex_unlock(&rv_interface_lock);

	return simple_read_from_buffer(user_buf, count, ppos,
				       buff, strlen(buff)+1);
}

/*
 * Disable a given runtime monitor.
 */
void disable_monitor(struct rv_monitor_def *mdef)
{
	if (mdef->monitor->enabled) {
		mdef->monitor->enabled = 0;
		mdef->monitor->stop();
	}

	mdef->enabled = 0;
}

/*
 * Enable a given monitor.
 */
void enable_monitor(struct rv_monitor_def *mdef)
{
	/*
	 * Reset all internal monitors before starting.
	 */
	mdef->monitor->reset();
	if (!mdef->monitor->enabled)
		mdef->monitor->start();

	mdef->monitor->enabled = 1;
	mdef->enabled = 1;
}

/*
 * interface for enabling/disabling a monitor.
 */
static ssize_t monitor_enable_write_data(struct file *filp,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct rv_monitor_def *mdef = filp->private_data;
	int retval;
	u64 val;

	retval = kstrtoull_from_user(user_buf, count, 10, &val);
	if (retval)
		return retval;

	retval = count;

	mutex_lock(&rv_interface_lock);

	switch (val) {
		case 0:
			disable_monitor(mdef);
			break;
		case 1:
			enable_monitor(mdef);
			break;
		default:
			retval = -EINVAL;
	}

	mutex_unlock(&rv_interface_lock);

	return retval;
}

static const struct file_operations interface_enable_fops = {
	.open   = simple_open,
	.llseek = no_llseek,
	.write  = monitor_enable_write_data,
	.read   = monitor_enable_read_data,
};

/*
 * Interface to read the enable/disable status of a monitor.
 */
static ssize_t
monitor_desc_read_data(struct file *filp, char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	struct rv_monitor_def *mdef = filp->private_data;
	char buf[MAX_RV_MONITOR_NAME_SIZE];

	memset(buf, 0, sizeof(buf));

	mutex_lock(&rv_interface_lock);
	sprintf(buf, "%s\n", mdef->monitor->description);
	mutex_unlock(&rv_interface_lock);

	return simple_read_from_buffer(user_buf, count, ppos,
					buf, strlen(buf)+1);
}

static const struct file_operations interface_desc_fops = {
	.open   = simple_open,
	.llseek	= no_llseek,
	.read	= monitor_desc_read_data,
};

/*
 * During the registration of a monitor, this function creates
 * the monitor dir, where the specific options of the monitor
 * is exposed.
 */
static int create_monitor_dir(struct rv_monitor_def *mdef)
{
	struct dentry *root = get_monitors_root();
	struct dentry *tmp;
	const char *name = mdef->monitor->name;
	int retval = 0;

	mdef->root_d = rv_create_dir(name, root);

	if (!mdef->root_d)
		return -ENOMEM;

	tmp = rv_create_file("enable", 0600,
			     mdef->root_d, mdef,
			     &interface_enable_fops);
	if (!tmp) {
		retval = -ENOMEM;
		goto out_remove_root;
	}

	tmp = rv_create_file("desc", 0400,
			      mdef->root_d, mdef,
			      &interface_desc_fops);
	if (!tmp) {
		retval = -ENOMEM;
		goto out_remove_root;
	}
#ifdef CONFIG_RV_REACTORS
	retval = reactor_create_monitor_files(mdef);
	if (retval)
		goto out_remove_root;
#endif

	return 0;

out_remove_root:
	rv_remove(mdef->root_d);
	return retval;
}

/*
 * Available/Enable monitor shared seq functions.
 */
static int monitors_show(struct seq_file *m, void *p)
{
	struct rv_monitor_def *mon_def = p;
	seq_printf(m, "%s\n", mon_def->monitor->name);
	return 0;
}

/*
 * Used by the seq file operations at the end of a read
 * operation.
 */
static void monitors_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&rv_interface_lock);
}

/*
 * Available monitor seq functions:
 */
static void *available_monitors_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&rv_interface_lock);
	return seq_list_start(&rv_monitors_list, *pos);
}

static void *available_monitors_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &rv_monitors_list, pos);
}

/*
 * Enable monitor seq functions:
 */

static void *enabled_monitors_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct rv_monitor_def *m_def = p;

	(*pos)++;

	list_for_each_entry_continue(m_def, &rv_monitors_list, list) {
		if (m_def->monitor->enabled)
			return m_def;
	}

	return NULL;
}

static void *enabled_monitors_start(struct seq_file *m, loff_t *pos)
{
	struct rv_monitor_def *m_def;
	loff_t l;

	mutex_lock(&rv_interface_lock);
	m_def = list_entry(&rv_monitors_list, struct rv_monitor_def, list);

	for (l = 0; l <= *pos; ) {
		m_def = enabled_monitors_next(m, m_def, &l);
		if (!m_def)
			break;
	}

	return m_def;
}

/*
 * available/enabled monitors seq definition.
 */
static const struct seq_operations available_monitors_seq_ops = {
	.start	= available_monitors_start,
	.next	= available_monitors_next,
	.stop	= monitors_stop,
	.show	= monitors_show
};

static const struct seq_operations enabled_monitors_seq_ops = {
	.start  = enabled_monitors_start,
	.next   = enabled_monitors_next,
	.stop   = monitors_stop,
	.show   = monitors_show
};

/*
 * available_monitors interface.
 */
static int available_monitors_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &available_monitors_seq_ops);
};

static struct file_operations available_monitors_ops = {
	.open    = available_monitors_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * enabled_monitors interface
 */
static void disable_all_monitors(void)
{
	struct rv_monitor_def *mdef;

	list_for_each_entry(mdef, &rv_monitors_list, list)
		disable_monitor(mdef);

	return;
}

static int enabled_monitors_open(struct inode *inode, struct file *file)
{
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC))
		disable_all_monitors();

	return seq_open(file, &enabled_monitors_seq_ops);
};

static ssize_t
enabled_monitors_write(struct file *filp, const char __user *user_buf,
		      size_t count, loff_t *ppos)
{
	char buff[MAX_RV_MONITOR_NAME_SIZE+1];
	struct rv_monitor_def *mdef;
	int retval = -EINVAL;
	bool enable = true;
	char *ptr = buff;
	int len;

	if (count < 1 || count > MAX_RV_MONITOR_NAME_SIZE+1)
		return -EINVAL;

	memset(buff, 0, sizeof(buff));

	retval = simple_write_to_buffer(buff, sizeof(buff)-1, ppos, user_buf,
					count);
	if (!retval)
		return -EFAULT;

	if (buff[0] == '-') {
		enable=false;
		ptr++;
	}

	len = strlen(ptr);
	if (!len)
		return count;
	/*
	 * remove the \n
	 */
	ptr[len-1]='\0';

	mutex_lock(&rv_interface_lock);

	retval = -EINVAL;

	list_for_each_entry(mdef, &rv_monitors_list, list) {
		if (strcmp(ptr, mdef->monitor->name) == 0) {
			/*
			 * Monitor found!
			 */
			if (enable)
				enable_monitor(mdef);
			else
				disable_monitor(mdef);

			retval=count;
			break;
		}
	}

	mutex_unlock(&rv_interface_lock);

	return retval;
}

static struct file_operations enabled_monitors_ops = {
	.open		= enabled_monitors_open,
	.read		= seq_read,
	.write		= enabled_monitors_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * monitoring_on general switcher
 */
static ssize_t monitoring_on_read_data(struct file *filp,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	char buff[4];

	memset(buff, 0, sizeof(buff));

	mutex_lock(&rv_interface_lock);
	sprintf(buff, "%d\n", monitoring_on);
	mutex_unlock(&rv_interface_lock);

	return simple_read_from_buffer(user_buf, count, ppos,
				       buff, strlen(buff)+1);
}

static void turn_monitoring_off(void)
{
	monitoring_on=false;
}

static void turn_monitoring_on(void)
{
	reset_all_monitors();
	monitoring_on=true;

	return;
}

static ssize_t monitoring_on_write_data(struct file *filp,
					 const char __user *user_buf,
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
			turn_monitoring_off();
			break;
		case 1:
			turn_monitoring_on();
			break;
		default:
			retval = -EINVAL;
	}

	mutex_unlock(&rv_interface_lock);

	return retval;
}

static const struct file_operations monitoring_on_fops = {
	.open   = simple_open,
	.llseek = no_llseek,
	.write  = monitoring_on_write_data,
	.read   = monitoring_on_read_data,
};

/*
 * Monitor API.
 */
static void destroy_monitor_dir(struct rv_monitor_def *mdef)
{
	rv_remove(mdef->root_d);
}

/**
 * rv_register_monitor - register a rv monitor.
 * @monitor:    The rv_monitor to be registered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_register_monitor(struct rv_monitor *monitor)
{
	struct rv_monitor_def *r;
	int retval = 0;

	if (strlen(monitor->name) >= MAX_RV_MONITOR_NAME_SIZE) {
		pr_info("Monitor %s has a name longer than %d\n",
			monitor->name, MAX_RV_MONITOR_NAME_SIZE);
		return -1;
	}

	mutex_lock(&rv_interface_lock);

	list_for_each_entry(r, &rv_monitors_list, list) {
		if (strcmp(monitor->name, r->monitor->name) == 0) {
			pr_info("Monitor %s is already registered\n",
				monitor->name);
			retval = -1;
			goto out_unlock;
		}
	}

	r = kzalloc(sizeof(struct rv_monitor_def), GFP_KERNEL);
	if (!r) {
		retval = -ENOMEM;
		goto out_unlock;
	}

	r->monitor = monitor;

	retval = create_monitor_dir(r);
	if (retval) {
		kfree(r);
		goto out_unlock;
	}

	list_add_tail(&r->list, &rv_monitors_list);

out_unlock:
	mutex_unlock(&rv_interface_lock);
	return retval;
}
EXPORT_SYMBOL_GPL(rv_register_monitor);

/**
 * rv_unregister_monitor - unregister a rv monitor.
 * @monitor:    The rv_monitor to be unregistered.
 *
 * Returns 0 if successful, error otherwise.
 */
int rv_unregister_monitor(struct rv_monitor *monitor)
{
	struct rv_monitor_def *ptr, *next;

	mutex_lock(&rv_interface_lock);

	list_for_each_entry_safe(ptr, next, &rv_monitors_list, list) {
		if (strcmp(monitor->name, ptr->monitor->name) == 0) {
			list_del(&ptr->list);
			destroy_monitor_dir(ptr);
		}
	}

	mutex_unlock(&rv_interface_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(rv_unregister_monitor);

void reset_all_monitors(void)
{
	struct rv_monitor_def *mdef;

	/*
	 * Reset all monitors before re-enabling monitoring.
	 */
	list_for_each_entry(mdef, &rv_monitors_list, list) {
		if (mdef->monitor->enabled)
			mdef->monitor->reset();
	}

}

int __init rv_init_interface(void)
{
	rv_root.root_dir = rv_create_dir("rv", NULL);
	rv_root.monitors_dir = rv_create_dir("monitors", rv_root.root_dir);

	rv_create_file("available_monitors", 0400, rv_root.root_dir, NULL,
		       &available_monitors_ops);
	rv_create_file("enabled_monitors", 0600, rv_root.root_dir, NULL,
		       &enabled_monitors_ops);
	rv_create_file("monitoring_on", 0600, rv_root.root_dir, NULL,
		       &monitoring_on_fops);

#ifdef CONFIG_RV_REACTORS
	init_rv_reactors(rv_root.root_dir);
	reacting_on=true;
#endif
	monitoring_on=true;

	return 0;
}
