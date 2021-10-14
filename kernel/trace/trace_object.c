// SPDX-License-Identifier: GPL-2.0
/*
 * trace any object
 * Copyright (C) 2021 Jeff Xie <xiehuan09@gmail.com>
 */

#define pr_fmt(fmt) "trace_object: " fmt

#include "trace_output.h"

static DEFINE_PER_CPU(atomic_t, trace_object_event_disable);
static DEFINE_MUTEX(object_mutex_lock);
static struct trace_event_file event_trace_file;

#define MAX_TRACE_OBJ_NUM 1024
static unsigned long *global_trace_obj;
static int global_trace_count;
static const int max_args_num = 6;

void set_trace_object(void *obj)
{
	int i;

	if (!obj || global_trace_count == MAX_TRACE_OBJ_NUM)
		goto out;

	for (i = 0; i < global_trace_count; i++) {
		if (global_trace_obj[i] == (unsigned long)obj)
			goto out;
	}
	mutex_lock(&object_mutex_lock);

	global_trace_obj[global_trace_count++] = (unsigned long)obj;

	mutex_unlock(&object_mutex_lock);
out:
	return;
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
	unsigned long object;
	long disabled;
	int cpu, n, i;

	preempt_disable_notrace();

	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&per_cpu(trace_object_event_disable, cpu));

	if (disabled != 1)
		goto out;

	if (!global_trace_obj[0])
		goto out;

	for (n = 0; n < max_args_num; n++) {
		object = regs_get_kernel_argument(pt_regs, n);
		for (i = 0; i < global_trace_count; i++) {
			if (object == global_trace_obj[i])
				submit_trace_object(ip, parent_ip, object);
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
	int ret;

	event_trace_file.tr = top_trace_array();
	if (WARN_ON(!event_trace_file.tr))
		return -1;

	global_trace_obj = kzalloc(sizeof(unsigned long) * MAX_TRACE_OBJ_NUM,
				GFP_KERNEL);
	if (!global_trace_obj)
		return -ENOMEM;

	ret = register_ftrace_function(&trace_ops);

	return ret;
}

int exit_trace_object(void)
{
	int ret;

	ret = unregister_ftrace_function(&trace_ops);

	kfree(global_trace_obj);
	return ret;
}
