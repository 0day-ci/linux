// SPDX-License-Identifier: GPL-2.0
/*
 * trace any object
 * Copyright (C) 2021 Jeff Xie <xiehuan09@gmail.com>
 */

#define pr_fmt(fmt) "trace_object: " fmt

#include "trace_output.h"

static DEFINE_PER_CPU(atomic_t, trace_object_event_disable);
static DEFINE_RAW_SPINLOCK(object_spin_lock);
static struct trace_event_file event_trace_file;
static LIST_HEAD(obj_head);
static const int max_args_num = 6;
static unsigned long trace_object_ref;

struct trace_obj {
	struct list_head head;
	unsigned long obj;
};

void set_trace_object(void *obj)
{
	struct trace_obj *trace_obj;
	struct trace_obj *new_obj;
	unsigned long flags;

	if (!obj)
		goto out;

	list_for_each_entry_rcu(trace_obj, &obj_head, head) {
		if (trace_obj->obj == (unsigned long)obj)
			goto out;
	}

	new_obj = kmalloc(sizeof(*new_obj), GFP_KERNEL);
	if (!new_obj) {
		pr_warn("allocate trace object fail\n");
		goto out;
	}

	raw_spin_lock_irqsave(&object_spin_lock, flags);

	new_obj->obj = (unsigned long)obj;

	list_add_rcu(&new_obj->head, &obj_head);

	raw_spin_unlock_irqrestore(&object_spin_lock, flags);
out:
	return;
}

void record_trace_object(struct trace_event_file *trace_file,
		struct pt_regs *regs)
{

	struct object_trigger_param *obj_param;
	struct event_trigger_data *data;

	list_for_each_entry_rcu(data, &trace_file->triggers, list) {
		if (data->cmd_ops->trigger_type == ETT_TRACE_OBJECT) {
			obj_param = data->private_data;
			obj_param->regs = regs;
		}
	}

}

static inline void free_trace_object(void)
{
	struct trace_obj *trace_obj;

	list_for_each_entry_rcu(trace_obj, &obj_head, head)
		kfree(trace_obj);
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
	struct trace_obj *trace_obj;
	long disabled;
	int cpu, n;

	preempt_disable_notrace();

	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&per_cpu(trace_object_event_disable, cpu));

	if (disabled != 1)
		goto out;

	if (list_empty(&obj_head))
		goto out;

	for (n = 0; n < max_args_num; n++) {
		obj = regs_get_kernel_argument(pt_regs, n);
		list_for_each_entry_rcu(trace_obj, &obj_head, head) {
			if (trace_obj->obj == (unsigned long)obj)
				submit_trace_object(ip, parent_ip, obj);
		}
	}

out:
	atomic_dec(&per_cpu(trace_object_event_disable, cpu));
	preempt_enable_notrace();
}

static struct ftrace_ops trace_ops = {
	.func  = trace_object_events_call,
	.flags = FTRACE_OPS_FL_SAVE_REGS,
};

int init_trace_object(void)
{
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&object_spin_lock, flags);

	if (++trace_object_ref != 1) {
		ret = 0;
		goto out_lock;
	}

	raw_spin_unlock_irqrestore(&object_spin_lock, flags);

	event_trace_file.tr = top_trace_array();
	if (WARN_ON(!event_trace_file.tr)) {
		ret = -1;
		goto out;
	}
	ret = register_ftrace_function(&trace_ops);

	return ret;

out_lock:
	raw_spin_unlock_irqrestore(&object_spin_lock, flags);
out:
	return ret;
}

int exit_trace_object(void)
{
	int ret;
	unsigned long flags;

	raw_spin_lock_irqsave(&object_spin_lock, flags);

	if (--trace_object_ref > 0) {
		ret = 0;
		goto out_lock;
	}

	raw_spin_unlock_irqrestore(&object_spin_lock, flags);

	free_trace_object();
	ret = unregister_ftrace_function(&trace_ops);

	return ret;

out_lock:
	raw_spin_unlock_irqrestore(&object_spin_lock, flags);
	return ret;
}
