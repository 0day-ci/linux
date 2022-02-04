// SPDX-License-Identifier: GPL-2.0
/*
 * trace the kernel object in the kernel function parameter
 * Copyright (C) 2021 Jeff Xie <xiehuan09@gmail.com>
 */

#define pr_fmt(fmt) "trace_object: " fmt

#include "trace_output.h"

#define MAX_TRACED_OBJECT 5
#define OBJTRACE_CMD_LEN  10
static DEFINE_PER_CPU(unsigned int, trace_object_event_disable);
static DEFINE_RAW_SPINLOCK(trace_obj_lock);
static struct trace_event_file event_trace_file;
static const int max_args_num = 6;
static atomic_t trace_object_ref;
static atomic_t num_traced_obj;
static int exit_trace_object(void);
static int init_trace_object(void);

static struct object_instance {
	void *obj;
} traced_obj[MAX_TRACED_OBJECT];

/* objtrace private data */
struct objtrace_trigger_data {
	struct ftrace_event_field *field;
	char objtrace_cmd[OBJTRACE_CMD_LEN];
};

static bool object_exist(void *obj)
{
	int i, max;

	max = atomic_read(&num_traced_obj);
	smp_rmb();
	for (i = 0; i < max; i++) {
		if (traced_obj[i].obj == obj)
			return true;
	}
	return false;
}

static bool object_empty(void)
{
	return !atomic_read(&num_traced_obj);
}

static void set_trace_object(void *obj)
{
	unsigned long flags;

	if (in_nmi())
		return;

	if (!obj)
		return;

	if (object_exist(obj))
		return;

	/* only this place has write operations */
	raw_spin_lock_irqsave(&trace_obj_lock, flags);
	if (atomic_read(&num_traced_obj) == MAX_TRACED_OBJECT) {
		trace_printk("object_pool is full, can't trace object:0x%px\n", obj);
		goto out;
	}
	traced_obj[atomic_read(&num_traced_obj)].obj = obj;
	/* make sure the num_traced_obj update always appears after traced_obj update */
	smp_wmb();
	atomic_inc(&num_traced_obj);
out:
	raw_spin_unlock_irqrestore(&trace_obj_lock, flags);
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
	unsigned int disabled;
	int n;

	preempt_disable_notrace();

	disabled = this_cpu_inc_return(trace_object_event_disable);
	if (disabled != 1)
		goto out;

	if (object_empty())
		goto out;

	for (n = 0; n < max_args_num; n++) {
		obj = regs_get_kernel_argument(pt_regs, n);
		if (object_exist((void *)obj))
			submit_trace_object(ip, parent_ip, obj);
	/* The parameters of a function may match multiple objects */
	}
out:
	this_cpu_dec(trace_object_event_disable);
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
	struct objtrace_trigger_data *obj_data = data->private_data;
	struct ftrace_event_field *field;
	void *obj = NULL;

	field = obj_data->field;
	memcpy(&obj, rec + field->offset, sizeof(obj));
	set_trace_object(obj);
}

static void
trace_object_trigger_free(struct event_trigger_ops *ops,
		   struct event_trigger_data *data)
{
	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref) {
		kfree(data->private_data);
		trigger_data_free(data);
	}
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
		void *data, char *filter_str, void *objtrace_data)
{
	long count = (long)data;
	struct objtrace_trigger_data *obj_data = objtrace_data;

	seq_puts(m, name);

	seq_printf(m, ":%s", obj_data->objtrace_cmd);
	seq_printf(m, ":%s", obj_data->field->name);

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
				   data->filter_str, data->private_data);
}

static struct event_trigger_ops objecttrace_trigger_ops = {
	.trigger		= trace_object_trigger,
	.print			= trace_object_trigger_print,
	.init			= event_object_trigger_init,
	.free			= trace_object_trigger_free,
};

static struct event_trigger_ops objecttrace_count_trigger_ops = {
	.trigger		= trace_object_count_trigger,
	.print			= trace_object_trigger_print,
	.init			= event_object_trigger_init,
	.free			= trace_object_trigger_free,
};

static struct event_trigger_ops *
objecttrace_get_trigger_ops(char *cmd, char *param)
{
	return param ? &objecttrace_count_trigger_ops : &objecttrace_trigger_ops;
}

static int register_object_trigger(char *glob,
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

static void unregister_object_trigger(char *glob,
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

	if (unregistered) {
		if (data->ops->free)
			data->ops->free(data->ops, data);
		exit_trace_object();
	}
}

static bool field_exist(struct trace_event_file *file,
			struct event_command *cmd_ops,
			const char *field_name)
{
	struct event_trigger_data *data;
	struct objtrace_trigger_data *obj_data;

	lockdep_assert_held(&event_mutex);

	list_for_each_entry(data, &file->triggers, list) {
		if (data->cmd_ops->trigger_type == cmd_ops->trigger_type) {
			obj_data = data->private_data;
			if (!strcmp(obj_data->field->name, field_name))
				return true;
		}
	}

	return false;
}

static int
event_object_trigger_parse(struct event_command *cmd_ops,
		       struct trace_event_file *file,
		       char *glob, char *cmd, char *param)
{
	struct event_trigger_data *trigger_data;
	struct event_trigger_ops *trigger_ops;
	struct objtrace_trigger_data *obj_data;
	struct trace_event_call *call;
	struct ftrace_event_field *field;
	char *objtrace_cmd;
	char *trigger = NULL;
	char *arg;
	char *number;
	int ret;
	bool remove = false;

	ret = -EINVAL;
	if (!param)
		goto out;

	/* separate the trigger from the filter (c:a:n [if filter]) */
	trigger = strsep(&param, " \t");
	if (!trigger)
		goto out;
	if (param) {
		param = skip_spaces(param);
		if (!*param)
			param = NULL;
	}

	objtrace_cmd = strsep(&trigger, ":");
	if (!objtrace_cmd || strcmp(objtrace_cmd, "add"))
		goto out;

	arg = strsep(&trigger, ":");
	if (!arg)
		goto out;
	call = file->event_call;
	field = trace_find_event_field(call, arg);
	if (!field)
		goto out;

	if (field->size != sizeof(void *))
		goto out;

	if (glob[0] == '!')
		remove = true;

	if (remove && !field_exist(file, cmd_ops, field->name))
	goto out;
	trigger_ops = cmd_ops->get_trigger_ops(cmd, trigger);
	ret = -ENOMEM;
	obj_data = kzalloc(sizeof(*obj_data), GFP_KERNEL);
	if (!obj_data)
		goto out;
	obj_data->field = field;
	snprintf(obj_data->objtrace_cmd, OBJTRACE_CMD_LEN, objtrace_cmd);

	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data) {
		kfree(obj_data);
		goto out;
	}

	trigger_data->count = -1;
	trigger_data->ops = trigger_ops;
	trigger_data->cmd_ops = cmd_ops;
	trigger_data->private_data = obj_data;
	INIT_LIST_HEAD(&trigger_data->list);
	INIT_LIST_HEAD(&trigger_data->named_list);

	if (remove) {
		cmd_ops->unreg(glob+1, trigger_data, file);
		kfree(obj_data);
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
	ret = cmd_ops->reg(glob, trigger_data, file);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */
	if (!ret) {
		cmd_ops->unreg(glob, trigger_data, file);
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
	kfree(obj_data);
	kfree(trigger_data);
	goto out;
}

static struct event_command trigger_object_cmd = {
	.name			= "objtrace",
	.trigger_type		= ETT_TRACE_OBJECT,
	.flags			= EVENT_CMD_FL_NEEDS_REC,
	.parse			= event_object_trigger_parse,
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

static int init_trace_object(void)
{
	int ret;

	if (atomic_inc_return(&trace_object_ref) != 1) {
		ret = 0;
		goto out;
	}

	event_trace_file.tr = top_trace_array();
	if (WARN_ON(!event_trace_file.tr)) {
		ret = -1;
		atomic_dec(&trace_object_ref);
		goto out;
	}
	ret = register_ftrace_function(&trace_ops);
out:
	return ret;
}

static int exit_trace_object(void)
{
	int ret;

	if (WARN_ON_ONCE(atomic_read(&trace_object_ref) <= 0)) {
		ret = -1;
		goto out;
	}

	if (atomic_dec_return(&trace_object_ref) != 0) {
		ret = 0;
		goto out;
	}

	ret = unregister_ftrace_function(&trace_ops);
	if (ret) {
		pr_err("can't unregister ftrace for trace object\n");
		goto out;
	}
	atomic_set(&num_traced_obj, 0);
out:
	return ret;
}
