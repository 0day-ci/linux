// SPDX-License-Identifier: GPL-2.0
/*
 * trace any object
 * Copyright (C) 2021 Jeff Xie <xiehuan09@gmail.com>
 */

#define pr_fmt(fmt) "trace_object: " fmt

#include <linux/workqueue.h>
#include "trace_output.h"

static struct work_struct obj_refill_work;
static DEFINE_PER_CPU(atomic_t, trace_object_event_disable);
static DEFINE_RAW_SPINLOCK(object_spin_lock);
static struct trace_event_file event_trace_file;
static LIST_HEAD(obj_head);
static const int max_args_num = 6;
static atomic_t trace_object_ref;

struct obj_pool {
	void **obj;
	int min_nr;
	int curr_nr;
};

static struct obj_pool obj_pool;
static int init_obj_num = 1024;
static int reserved_obj_num = 100;

static int object_exist(void *obj)
{
	int i, used, ret = false;

	if (!obj)
		goto out;

	used = obj_pool.min_nr - obj_pool.curr_nr;
	if (!used)
		goto out;

	for (i = 0; i < used; i++) {
		if (obj_pool.obj[obj_pool.curr_nr + i] == obj) {
			ret = true;
			goto out;
		}
	}
out:
	return ret;
}

static int object_empty(void)
{
	int ret;

	ret = obj_pool.curr_nr == obj_pool.min_nr;

	return ret;
}

static void **remove_object_element(void)
{
	void **obj = &obj_pool.obj[--obj_pool.curr_nr];
	BUG_ON(obj_pool.curr_nr < 0);

	return obj;
}

static void add_object_element(void *obj)
{
	BUG_ON(obj_pool.curr_nr >= obj_pool.min_nr);
	obj_pool.obj[obj_pool.curr_nr++] = obj;
}

void set_trace_object(void *obj)
{
	unsigned long flags;
	void **new_obj;

	if (in_nmi())
		return;

	if (!obj)
		return;

	raw_spin_lock_irqsave(&object_spin_lock, flags);

	if (object_exist(obj))
		goto out;

	if (obj_pool.curr_nr == 0) {
		raw_spin_unlock_irqrestore(&object_spin_lock, flags);
		trace_printk("object_pool is full, can't trace object:0x%px\n", obj);
		return;
	}

	new_obj = remove_object_element();
	*new_obj = obj;
	if (obj_pool.curr_nr == reserved_obj_num) {
		raw_spin_unlock_irqrestore(&object_spin_lock, flags);
		schedule_work(&obj_refill_work);
		return;
	}
out:
	raw_spin_unlock_irqrestore(&object_spin_lock, flags);
}

static void object_pool_exit(void)
{
	obj_pool.min_nr = 0;
	obj_pool.curr_nr = 0;
	kfree(obj_pool.obj);
	obj_pool.obj = NULL;
}

static void obj_refill_fn(struct work_struct *refill_work)
{
	void **new_obj_element;
	unsigned long flags;
	int new_min_nr, new_curr_nr;
	int used_nr;

	new_min_nr = obj_pool.min_nr * 2;
	new_obj_element = kmalloc_array(new_min_nr, sizeof(void *), GFP_KERNEL);

	if (!new_obj_element)
		return;

	raw_spin_lock_irqsave(&object_spin_lock, flags);

	used_nr = obj_pool.min_nr - obj_pool.curr_nr;
	new_curr_nr = new_min_nr - used_nr;
	memcpy(new_obj_element + new_curr_nr, obj_pool.obj + obj_pool.curr_nr,
			used_nr * sizeof(void *));

	kfree(obj_pool.obj);
	obj_pool.obj = new_obj_element;
	obj_pool.curr_nr = new_curr_nr;
	obj_pool.min_nr  = new_min_nr;

	raw_spin_unlock_irqrestore(&object_spin_lock, flags);
}

static int init_object_pool(void)
{
	int ret = 0;
	int i;

	obj_pool.obj = kmalloc_array(init_obj_num, sizeof(void *), GFP_KERNEL);
	if (!obj_pool.obj) {
		ret = -ENOMEM;
		goto out;
	}
	obj_pool.min_nr = init_obj_num;

	for (i = 0; i < init_obj_num; i++)
		add_object_element(obj_pool.obj[i]);

out:
	return ret;
}

static void submit_trace_object(unsigned long ip, unsigned long parent_ip,
				 unsigned long object)
{

	struct trace_buffer *buffer;
	struct ring_buffer_event *event;
	struct trace_object_entry *entry;
	int pc;

	pc = preempt_count();
	event = trace_event_buffer_lock_reserve(&buffer, &event_trace_file,
			TRACE_OBJECT, sizeof(*entry), pc);
	if (!event)
		return;
	entry   = ring_buffer_event_data(event);
	entry->ip                       = ip;
	entry->parent_ip                = parent_ip;
	entry->object			= object;

	event_trigger_unlock_commit(&event_trace_file, buffer, event,
		entry, pc);
}

static void
trace_object_events_call(unsigned long ip, unsigned long parent_ip,
		struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	struct pt_regs *pt_regs = ftrace_get_regs(fregs);
	unsigned long obj;
	long disabled;
	int cpu, n;

	preempt_disable_notrace();

	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&per_cpu(trace_object_event_disable, cpu));

	if (disabled != 1)
		goto out;

	if (object_empty())
		goto out;

	for (n = 0; n < max_args_num; n++) {
		obj = regs_get_kernel_argument(pt_regs, n);
		if (object_exist((void *)obj))
			submit_trace_object(ip, parent_ip, obj);
	}

out:
	atomic_dec(&per_cpu(trace_object_event_disable, cpu));
	preempt_enable_notrace();
}

static struct ftrace_ops trace_ops = {
	.func  = trace_object_events_call,
	.flags = FTRACE_OPS_FL_SAVE_REGS,
};

static void
trace_object_trigger(struct event_trigger_data *data,
		   struct trace_buffer *buffer,  void *rec,
		   struct ring_buffer_event *event)
{

	struct ftrace_event_field *field = data->private_data;
	void *obj = NULL;

	memcpy(&obj, (char *)rec + field->offset, field->size);
	set_trace_object(obj);
}

static void
trace_object_trigger_free(struct event_trigger_ops *ops,
		   struct event_trigger_data *data)
{
	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref)
		trigger_data_free(data);
}

static void
trace_object_count_trigger(struct event_trigger_data *data,
			 struct trace_buffer *buffer, void *rec,
			 struct ring_buffer_event *event)
{
	if (!data->count)
		return;

	if (data->count != -1)
		(data->count)--;

	trace_object_trigger(data, buffer, rec, event);
}

static int event_object_trigger_init(struct event_trigger_ops *ops,
		       struct event_trigger_data *data)
{
	data->ref++;
	return 0;
}

static int
event_trigger_print(const char *name, struct seq_file *m,
		    void *data, char *filter_str)
{
	long count = (long)data;

	seq_puts(m, name);

	if (count == -1)
		seq_puts(m, ":unlimited");
	else
		seq_printf(m, ":count=%ld", count);

	if (filter_str)
		seq_printf(m, " if %s\n", filter_str);
	else
		seq_putc(m, '\n');

	return 0;
}

static int
trace_object_trigger_print(struct seq_file *m, struct event_trigger_ops *ops,
			 struct event_trigger_data *data)
{
	return event_trigger_print("objtrace", m, (void *)data->count,
				   data->filter_str);
}


static struct event_trigger_ops objecttrace_trigger_ops = {
	.func			= trace_object_trigger,
	.print			= trace_object_trigger_print,
	.init			= event_object_trigger_init,
	.free			= trace_object_trigger_free,
};

static struct event_trigger_ops objecttrace_count_trigger_ops = {
	.func			= trace_object_count_trigger,
	.print			= trace_object_trigger_print,
	.init			= event_object_trigger_init,
	.free			= trace_object_trigger_free,
};

static struct event_trigger_ops *
objecttrace_get_trigger_ops(char *cmd, char *param)
{
	return param ? &objecttrace_count_trigger_ops : &objecttrace_trigger_ops;
}

static int register_object_trigger(char *glob, struct event_trigger_ops *ops,
			    struct event_trigger_data *data,
			    struct trace_event_file *file)
{
	struct event_trigger_data *test;
	int ret = 0;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(test, &file->triggers, list) {
		if (test->cmd_ops->trigger_type == data->cmd_ops->trigger_type) {
			ret = -EEXIST;
			goto out;
		}
	}

	if (data->ops->init) {
		ret = data->ops->init(data->ops, data);
		if (ret < 0)
			goto out;
	}

	list_add_rcu(&data->list, &file->triggers);
	ret++;

	update_cond_flag(file);
	if (trace_event_trigger_enable_disable(file, 1) < 0) {
		list_del_rcu(&data->list);
		update_cond_flag(file);
		ret--;
	}
	init_trace_object();
out:
	return ret;
}

void unregister_object_trigger(char *glob, struct event_trigger_ops *ops,
			       struct event_trigger_data *test,
			       struct trace_event_file *file)
{
	struct event_trigger_data *data;
	bool unregistered = false;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(data, &file->triggers, list) {
		if (data->cmd_ops->trigger_type == test->cmd_ops->trigger_type) {
			unregistered = true;
			list_del_rcu(&data->list);
			trace_event_trigger_enable_disable(file, 0);
			update_cond_flag(file);
			break;
		}
	}

	if (unregistered && data->ops->free) {
		data->ops->free(data->ops, data);
		exit_trace_object();
	}
}

static int
event_object_trigger_callback(struct event_command *cmd_ops,
		       struct trace_event_file *file,
		       char *glob, char *cmd, char *param)
{
	struct event_trigger_data *trigger_data;
	struct event_trigger_ops *trigger_ops;
	struct trace_event_call *call;
	struct ftrace_event_field *field;
	char *trigger = NULL;
	char *arg;
	char *number;
	int ret;

	ret = -EINVAL;
	if (!param)
		goto out;

	/* separate the trigger from the filter (a:n [if filter]) */
	trigger = strsep(&param, " \t");
	if (!trigger)
		goto out;
	if (param) {
		param = skip_spaces(param);
		if (!*param)
			param = NULL;
	}

	arg = strsep(&trigger, ":");
	if (!arg)
		goto out;
	call = file->event_call;
	field = trace_find_event_field(call, arg);
	if (!field)
		goto out;

	trigger_ops = cmd_ops->get_trigger_ops(cmd, trigger);

	ret = -ENOMEM;
	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		goto out;

	trigger_data->count = -1;
	trigger_data->ops = trigger_ops;
	trigger_data->cmd_ops = cmd_ops;
	trigger_data->private_data = field;
	INIT_LIST_HEAD(&trigger_data->list);
	INIT_LIST_HEAD(&trigger_data->named_list);

	if (glob[0] == '!') {
		cmd_ops->unreg(glob+1, trigger_ops, trigger_data, file);
		kfree(trigger_data);
		ret = 0;
		goto out;
	}

	if (trigger) {
		number = strsep(&trigger, ":");

		ret = -EINVAL;
		if (!strlen(number))
			goto out_free;

		/*
		 * We use the callback data field (which is a pointer)
		 * as our counter.
		 */
		ret = kstrtoul(number, 0, &trigger_data->count);
		if (ret)
			goto out_free;
	}

	if (!param) /* if param is non-empty, it's supposed to be a filter */
		goto out_reg;

	if (!cmd_ops->set_filter)
		goto out_reg;

	ret = cmd_ops->set_filter(param, trigger_data, file);
	if (ret < 0)
		goto out_free;

 out_reg:
	/* Up the trigger_data count to make sure reg doesn't free it on failure */
	event_object_trigger_init(trigger_ops, trigger_data);
	ret = cmd_ops->reg(glob, trigger_ops, trigger_data, file);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */
	if (!ret) {
		cmd_ops->unreg(glob, trigger_ops, trigger_data, file);
		ret = -ENOENT;
	} else if (ret > 0)
		ret = 0;

	/* Down the counter of trigger_data or free it if not used anymore */
	trace_object_trigger_free(trigger_ops, trigger_data);
 out:
	return ret;

 out_free:
	if (cmd_ops->set_filter)
		cmd_ops->set_filter(NULL, trigger_data, NULL);
	kfree(trigger_data);
	goto out;
}

static struct event_command trigger_object_cmd = {
	.name			= "objtrace",
	.trigger_type		= ETT_TRACE_OBJECT,
	.flags			= EVENT_CMD_FL_NEEDS_REC,
	.func			= event_object_trigger_callback,
	.reg			= register_object_trigger,
	.unreg			= unregister_object_trigger,
	.get_trigger_ops	= objecttrace_get_trigger_ops,
	.set_filter		= set_trigger_filter,
};

__init int register_trigger_object_cmd(void)
{
	int ret;

	ret = register_event_command(&trigger_object_cmd);
	WARN_ON(ret < 0);

	return ret;
}

int init_trace_object(void)
{
	int ret;

	if (atomic_inc_return(&trace_object_ref) != 1) {
		ret = 0;
		goto out;
	}

	ret = init_object_pool();
	if (ret)
		goto out;

	INIT_WORK(&obj_refill_work, obj_refill_fn);
	event_trace_file.tr = top_trace_array();
	if (WARN_ON(!event_trace_file.tr)) {
		ret = -1;
		goto out;
	}
	ret = register_ftrace_function(&trace_ops);
out:
	return ret;
}

int exit_trace_object(void)
{
	int ret;

	if (WARN_ON_ONCE(atomic_read(&trace_object_ref) <= 0))
		goto out;

	if (atomic_dec_return(&trace_object_ref) != 0) {
		ret = 0;
		goto out;
	}

	ret = unregister_ftrace_function(&trace_ops);
	if (ret) {
		pr_err("can't unregister ftrace for trace object\n");
		goto out;
	}
	object_pool_exit();
out:
	return ret;
}
