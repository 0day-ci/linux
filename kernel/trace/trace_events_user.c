// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Beau Belgrave <beaub@linux.microsoft.com>
 */

#include <linux/bitmap.h>
#include <linux/cdev.h>
#include <linux/hashtable.h>
#include <linux/io.h>
#include <linux/uio.h>
#include <linux/ioctl.h>
#include <linux/jhash.h>
#include <linux/trace_events.h>
#include <linux/tracefs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <uapi/linux/user_events.h>
#include "trace.h"
#include "trace_dynevent.h"

#define USER_EVENTS_PREFIX_LEN (sizeof(USER_EVENTS_PREFIX)-1)

#define FIELD_DEPTH_TYPE 0
#define FIELD_DEPTH_NAME 1
#define FIELD_DEPTH_SIZE 2

/*
 * Limits how many trace_event calls user processes can create:
 * Must be multiple of PAGE_SIZE.
 */
#define MAX_PAGES 1
#define MAX_EVENTS (MAX_PAGES * PAGE_SIZE)
#define MAX_BPF_COPY_SIZE PAGE_SIZE
#define MAX_STACK_BPF_DATA 512

/* Limit how long of an event name plus args within the subsystem. */
#define MAX_EVENT_DESC 512
#define EVENT_NAME(user_event) ((user_event)->tracepoint.name)

#define FLAG_BPF_ITER 1

static char *register_page_data;

static DEFINE_HASHTABLE(register_table, 4);
static DECLARE_BITMAP(page_bitmap, MAX_EVENTS);

struct user_event {
	struct tracepoint tracepoint;
	struct trace_event_call call;
	struct trace_event_class class;
	struct dyn_event devent;
	struct hlist_node node;
	atomic_t refs;
	int index;
	char *args;
	int flags;
};

struct user_event_refs {
	struct rcu_head rcu;
	int count;
	struct user_event *events[];
};

typedef void (*user_event_func_t) (struct user_event *user,
				   struct iov_iter *i,
				   void *tpdata);

static int register_user_event(char *name, char *args,
			       struct user_event **newuser);

/*
 * Parses a register command for user_events
 * Format: event_name [field1;field2;...]
 *
 * Example event named test with a 20 char msg field with a unsigned int after:
 * test char[20] msg 20;unsigned int id 4;
 *
 * NOTE: Offsets are from the user data perspective, they are not from the
 * trace_entry/buffer perspective. We automatically add the common properties
 * sizes to the offset for the user.
 */
static int user_event_parse_cmd(char *raw_command, struct user_event **newuser)
{
	char *name = raw_command;
	char *args = strpbrk(name, " ");

	if (args)
		*args++ = 0;

	return register_user_event(name, args, newuser);
}

/*
 * Parses the values of a field within the description
 * Format: type name size [future]
 */
static int user_event_parse_field(char *field, struct user_event *user,
				  u32 *offset)
{
	char *part, *type, *name;
	u32 size = 0, depth = 0, saved_offset = *offset;

	field = skip_spaces(field);

	if (strstr(field, "unsigned ") == field) {
		type = field;
		field = strpbrk(field + sizeof("unsigned"), " ");

		if (field == NULL)
			return -EINVAL;

		*field++ = 0;
		depth++;
	} else if (strstr(field, "print_fmt ") == field) {
		user->call.print_fmt = field + sizeof("print_fmt");
		return 0;
	} else if (strstr(field, "flag ") == field) {
		field += sizeof("flag");

		if (!strcmp(field, "bpf_iter"))
			user->flags |= FLAG_BPF_ITER;

		return 0;
	}

	while ((part = strsep(&field, " ")) != NULL) {
		switch (depth++) {
		case FIELD_DEPTH_TYPE:
			type = part;
			break;
		case FIELD_DEPTH_NAME:
			name = part;
			break;
		case FIELD_DEPTH_SIZE:
			if (kstrtou32(part, 10, &size))
				return -EINVAL;
			break;
		default:
			/* Forward compatibility, ignore */
			goto end;
		}
	}
end:
	if (depth < FIELD_DEPTH_SIZE)
		return -EINVAL;

	*offset = saved_offset + size;

	return trace_define_field(&user->call, type, name, saved_offset, size,
				  type[0] != 'u', FILTER_OTHER);
}

/*
 * Parses the fields that were described for the event
 */
static int user_event_parse_fields(struct user_event *user)
{
	char *field;
	u32 offset = sizeof(struct trace_entry);
	int ret = -EINVAL;

	while ((field = strsep(&user->args, ";")) != NULL) {
		ret = user_event_parse_field(field, user, &offset);

		if (ret)
			break;
	}

	return ret;
}

static int user_event_define_fields(struct trace_event_call *call)
{
	struct user_event *user = (struct user_event *)call->data;

	/* User chose to not disclose arguments */
	if (user->args == NULL)
		return 0;

	return user_event_parse_fields(user);
}

static struct trace_event_fields user_event_fields_array[] = {
	{ .type = TRACE_FUNCTION_TYPE,
	  .define_fields = user_event_define_fields },
	{}
};

static enum print_line_t user_event_print_trace(struct trace_iterator *iter,
						int flags,
						struct trace_event *event)
{
	/* Unsafe to try to decode user provided print_fmt, use hex */
	trace_print_hex_dump_seq(&iter->seq, "", DUMP_PREFIX_OFFSET, 16,
				 1, iter->ent, iter->ent_size, true);

	return trace_handle_return(&iter->seq);
}

static struct trace_event_functions user_event_funcs = {
	.trace = user_event_print_trace,
};

static int destroy_user_event(struct user_event *user)
{
	int ret = 0;

	/*
	 * trace_remove_event_call invokes unregister_trace_event:
	 * Pick the correct one based on if we set the data or not
	 */
	if (user->index != 0) {
		ret = trace_remove_event_call(&user->call);

		if (ret)
			return ret;

		dyn_event_remove(&user->devent);

		register_page_data[user->index] = 0;
		clear_bit(user->index, page_bitmap);
		hash_del(&user->node);
	} else {
		unregister_trace_event(&user->call.event);
	}

	kfree(EVENT_NAME(user));
	kfree(user);

	return ret;
}

static struct user_event *find_user_event(u32 key, char *name)
{
	struct user_event *user;

	hash_for_each_possible(register_table, user, node, key)
		if (!strcmp(EVENT_NAME(user), name))
			return user;

	return NULL;
}

/*
 * Writes the user supplied payload out to a trace file.
 */
static void user_event_ftrace(struct user_event *user, struct iov_iter *i,
			      void *tpdata)
{
	struct trace_event_file *file;
	struct trace_entry *entry;
	struct trace_event_buffer event_buffer;
	size_t count = i->count;

	file = (struct trace_event_file *)tpdata;

	if (!file ||
	    !(file->flags & EVENT_FILE_FL_ENABLED) ||
	    trace_trigger_soft_disabled(file))
		return;

	entry = trace_event_buffer_reserve(&event_buffer, file,
					   sizeof(*entry) + count);

	if (unlikely(!entry))
		return;

	if (unlikely(copy_from_iter(entry + 1, count, i) != count))
		return;

	trace_event_buffer_commit(&event_buffer);
}

#ifdef CONFIG_PERF_EVENTS
/*
 * Writes the user supplied payload out to perf ring buffer or eBPF program.
 */
static void user_event_perf(struct user_event *user, struct iov_iter *i,
			    void *tpdata)
{
	struct hlist_head *perf_head;
	size_t count = i->count;

	if (bpf_prog_array_valid(&user->call)) {
		struct user_bpf_context context = {0};
		struct user_bpf_iter bpf_i = {0};
		char fast_data[MAX_STACK_BPF_DATA];
		void *temp = NULL;

		context.data_len = count;

		if ((user->flags & FLAG_BPF_ITER) && iter_is_iovec(i)) {
			/* Raw iovec to BPF, no copies */
			bpf_i.iov_offset = i->iov_offset;
			bpf_i.iov = i->iov;
			bpf_i.nr_segs = i->nr_segs;

			context.data_type = USER_BPF_DATA_ITER;
			context.iter = &bpf_i;
		} else if (i->nr_segs == 1 && iter_is_iovec(i)) {
			/* Single vector, no copies */
			context.data_type = USER_BPF_DATA_USER;
			context.udata = i->iov->iov_base + i->iov_offset;
		} else {
			/* Multi vector, copy on stack or heap */
			struct iov_iter copy;
			size_t copy_size = count;

			context.data_type = USER_BPF_DATA_KERNEL;

			if (copy_size > MAX_BPF_COPY_SIZE)
				copy_size = MAX_BPF_COPY_SIZE;

			if (copy_size <= sizeof(fast_data))
				context.kdata = fast_data;
			else {
				temp = kmalloc(copy_size, GFP_KERNEL);
				context.kdata = temp;
			}

			/* Must keep in case perf_events is hooked as well */
			copy = *i;

			context.data_len = copy_from_iter(context.kdata,
							  copy_size, &copy);
		}

		trace_call_bpf(&user->call, &context);

		kfree(temp);
	}

	perf_head = this_cpu_ptr(user->call.perf_events);

	if (perf_head && !hlist_empty(perf_head)) {
		struct trace_entry *perf_entry;
		struct pt_regs *regs;
		size_t size = sizeof(*perf_entry) + count;
		int context;

		perf_entry = perf_trace_buf_alloc(ALIGN(size, 8),
						  &regs, &context);

		if (unlikely(!perf_entry))
			return;

		perf_fetch_caller_regs(regs);

		if (unlikely(copy_from_iter(perf_entry + 1, count, i) != count))
			return;

		perf_trace_buf_submit(perf_entry, size, context,
				      user->call.event.type, 1, regs,
				      perf_head, NULL);
	}
}
#endif

/*
 * Update the register page that is shared between user processes.
 */
static void update_reg_page_for(struct user_event *user)
{
	struct tracepoint *tp = &user->tracepoint;
	char status = 0;

	if (atomic_read(&tp->key.enabled) > 0) {
		struct tracepoint_func *probe_func_ptr;
		user_event_func_t probe_func;

		probe_func_ptr = rcu_dereference_sched(tp->funcs);

		if (probe_func_ptr) {
			do {
				probe_func = probe_func_ptr->func;

				if (probe_func == user_event_ftrace)
					status |= EVENT_STATUS_FTRACE;
#ifdef CONFIG_PERF_EVENTS
				else if (probe_func == user_event_perf)
					status |= EVENT_STATUS_PERF;
#endif
				else
					status |= EVENT_STATUS_OTHER;
			} while ((++probe_func_ptr)->func);
		}
	}

	register_page_data[user->index] = status;
}

/*
 * Register callback for our events from tracing sub-systems.
 */
static int user_event_reg(struct trace_event_call *call,
			  enum trace_reg type,
			  void *data)
{
	struct user_event *user = (struct user_event *)call->data;
	int ret = 0;

	if (!user)
		return -ENOENT;

	switch (type) {
	case TRACE_REG_REGISTER:
		ret = tracepoint_probe_register(call->tp,
						call->class->probe,
						data);
		if (!ret)
			goto inc;
		break;

	case TRACE_REG_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->probe,
					    data);
		goto dec;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		ret = tracepoint_probe_register(call->tp,
						call->class->perf_probe,
						data);
		if (!ret)
			goto inc;
		break;

	case TRACE_REG_PERF_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->perf_probe,
					    data);
		goto dec;

	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		break;
#endif
	}

	return ret;
inc:
	atomic_inc(&user->refs);
	update_reg_page_for(user);
	return 0;
dec:
	update_reg_page_for(user);
	atomic_dec(&user->refs);
	return 0;
}

static u32 user_event_key(char *name)
{
	return jhash(name, strlen(name), 0);
}

static int user_event_create(const char *raw_command)
{
	struct user_event *user;
	char *name;
	int ret;

	if (strstr(raw_command, USER_EVENTS_PREFIX) != raw_command)
		return -ECANCELED;

	raw_command += USER_EVENTS_PREFIX_LEN;
	raw_command = skip_spaces(raw_command);

	name = kstrdup(raw_command, GFP_KERNEL);

	if (!name)
		return -ENOMEM;

	mutex_lock(&event_mutex);
	ret = user_event_parse_cmd(name, &user);
	mutex_unlock(&event_mutex);

	return ret;
}

static int user_event_show(struct seq_file *m, struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);
	struct ftrace_event_field *field, *next;
	struct list_head *head;
	int depth = 0;

	seq_printf(m, "%s%s", USER_EVENTS_PREFIX, EVENT_NAME(user));

	head = trace_get_fields(&user->call);

	list_for_each_entry_safe_reverse(field, next, head, link) {
		if (depth == 0)
			seq_puts(m, " ");
		else
			seq_puts(m, "; ");
		seq_printf(m, "%s %s", field->type, field->name);
		depth++;
	}

	seq_puts(m, "\n");

	return 0;
}

static bool user_event_is_busy(struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);

	return atomic_read(&user->refs) != 0;
}

static int user_event_free(struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);

	return destroy_user_event(user);
}

static bool user_event_match(const char *system, const char *event,
			     int argc, const char **argv, struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);

	return strcmp(EVENT_NAME(user), event) == 0 &&
		(!system || strcmp(system, USER_EVENTS_SYSTEM) == 0);
}

static struct dyn_event_operations user_event_dops = {
	.create = user_event_create,
	.show = user_event_show,
	.is_busy = user_event_is_busy,
	.free = user_event_free,
	.match = user_event_match,
};

/*
 * Register a trace_event into the system, either find or create.
 */
static int register_user_event(char *name, char *args,
			       struct user_event **newuser)
{
	int ret;
	int index;
	u32 key = user_event_key(name);
	struct user_event *user = find_user_event(key, name);

	if (user) {
		*newuser = user;
		ret = 0;
		goto put_name;
	}

	index = find_first_zero_bit(page_bitmap, MAX_EVENTS);

	if (index == MAX_EVENTS) {
		ret = -EMFILE;
		goto put_name;
	}

	user = kzalloc(sizeof(*user), GFP_KERNEL);

	if (!user) {
		ret = -ENOMEM;
		goto put_name;
	}

	INIT_LIST_HEAD(&user->class.fields);

	user->tracepoint.name = name;
	user->args = args;

	user->call.data = user;
	user->call.class = &user->class;
	user->call.name = name;
	user->call.flags = TRACE_EVENT_FL_TRACEPOINT;
	user->call.tp = &user->tracepoint;
	user->call.event.funcs = &user_event_funcs;

	/* libtraceevent requires atleast double quotes */
	user->call.print_fmt = "\"\"";

	user->class.system = USER_EVENTS_SYSTEM;
	user->class.fields_array = user_event_fields_array;
	user->class.reg = user_event_reg;
	user->class.probe = user_event_ftrace;
#ifdef CONFIG_PERF_EVENTS
	user->class.perf_probe = user_event_perf;
#endif

	ret = register_trace_event(&user->call.event);

	if (!ret) {
		ret = -ENODEV;
		goto put_user;
	}

	ret = trace_add_event_call(&user->call);

	if (ret) {
		destroy_user_event(user);
		goto out;
	}

	user->index = index;
	dyn_event_init(&user->devent, &user_event_dops);
	dyn_event_add(&user->devent);
	set_bit(user->index, page_bitmap);
	hash_add(register_table, &user->node, key);

	*newuser = user;
	return 0;
put_user:
	kfree(user);
put_name:
	kfree(name);
out:
	return ret;
}

/*
 * Deletes a previously created event if it is no longer being used.
 */
static int delete_user_event(char *name)
{
	u32 key = user_event_key(name);
	struct user_event *user = find_user_event(key, name);

	if (!user)
		return -ENOENT;

	if (atomic_read(&user->refs) != 0)
		return -EBUSY;

	return destroy_user_event(user);
}

/*
 * Validates the user payload and writes via iterator.
 */
static ssize_t user_events_write_core(struct file *file, struct iov_iter *i)
{
	struct user_event_refs *refs;
	struct user_event *user = NULL;
	struct tracepoint *tp;
	ssize_t ret = i->count;
	int idx;

	preempt_disable();

	if (unlikely(!(cpu_online(raw_smp_processor_id()))))
		goto out;

	if (unlikely(copy_from_iter(&idx, sizeof(idx), i) != sizeof(idx))) {
		ret = -EFAULT;
		goto out;
	}

	idx = INDEX_WRITE(idx);

	refs = rcu_dereference_sched(file->private_data);

	if (likely(refs && idx < refs->count))
		user = refs->events[idx];

	if (unlikely(user == NULL)) {
		ret = -ENOENT;
		goto out;
	}

	tp = &user->tracepoint;

	if (likely(atomic_read(&tp->key.enabled) > 0)) {
		struct tracepoint_func *probe_func_ptr;
		user_event_func_t probe_func;
		struct iov_iter copy;
		void *tpdata;

		probe_func_ptr = rcu_dereference_sched(tp->funcs);

		if (probe_func_ptr) {
			do {
				/*
				 * Probes advance the iterator so we
				 * need to have a copy for each probe.
				 */
				copy = *i;

				probe_func = probe_func_ptr->func;
				tpdata = probe_func_ptr->data;
				probe_func(user, &copy, tpdata);
			} while ((++probe_func_ptr)->func);
		}
	}
out:
	preempt_enable();

	return ret;
}

static ssize_t user_events_write(struct file *file, const char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	struct iovec iov;
	struct iov_iter i;

	if (unlikely(*ppos != 0))
		return -EFAULT;

	if (unlikely(import_single_range(READ, (char *)ubuf, count, &iov, &i)))
		return -EFAULT;

	return user_events_write_core(file, &i);
}

static ssize_t user_events_write_iter(struct kiocb *kp, struct iov_iter *i)
{
	return user_events_write_core(kp->ki_filp, i);
}

static int user_events_ref_add(struct file *file, struct user_event *user)
{
	struct user_event_refs *refs, *new_refs;
	int i, size, count = 0;

	refs = rcu_dereference_sched(file->private_data);

	if (refs) {
		count = refs->count;

		for (i = 0; i < count; ++i)
			if (refs->events[i] == user)
				return i;
	}

	size = sizeof(*refs) + (sizeof(struct user_event *) * (count + 1));

	new_refs = kzalloc(size, GFP_KERNEL);

	if (!new_refs)
		return -ENOMEM;

	new_refs->count = count + 1;

	for (i = 0; i < count; ++i)
		new_refs->events[i] = refs->events[i];

	new_refs->events[i] = user;

	atomic_inc(&user->refs);

	rcu_assign_pointer(file->private_data, new_refs);

	if (refs)
		kfree_rcu(refs, rcu);

	return i;
}

/*
 * Handles the ioctl from user mode to register or alter operations.
 */
static long user_events_ioctl(struct file *file, unsigned int cmd,
			      unsigned long uarg)
{
	void __user *ubuf = (void __user *)uarg;
	struct user_event *user;
	char *name;
	long ret;

	switch (cmd) {
	case DIAG_IOCSREG:
		/* Register/lookup on behalf of user process */
		name = strndup_user(ubuf, MAX_EVENT_DESC);

		if (IS_ERR(name)) {
			ret = PTR_ERR(name);
			goto out;
		}

		mutex_lock(&event_mutex);

		ret = user_event_parse_cmd(name, &user);

		if (ret < 0)
			goto reg_out;

		preempt_disable();
		ret = user_events_ref_add(file, user);
		preempt_enable();

		if (ret < 0)
			goto reg_out;

		ret = INDEX_COMBINE(ret, user->index);
reg_out:
		mutex_unlock(&event_mutex);

		break;

	case DIAG_IOCSDEL:
		/* Delete on behalf of user process */
		name = strndup_user(ubuf, MAX_EVENT_DESC);

		if (IS_ERR(name)) {
			ret = PTR_ERR(name);
			goto out;
		}

		mutex_lock(&event_mutex);
		ret = delete_user_event(name);
		mutex_unlock(&event_mutex);

		kfree(name);
		break;

	default:
		ret = -ENOTTY;
		break;
	}
out:
	return ret;
}

/*
 * Handles the final close of the file from user mode.
 */
static int user_events_release(struct inode *node, struct file *file)
{
	struct user_event_refs *refs;
	struct user_event *user;
	int i;

	preempt_disable();

	refs = rcu_dereference_sched(file->private_data);

	if (!refs)
		goto out;

	for (i = 0; i < refs->count; ++i) {
		user = refs->events[i];

		if (user)
			atomic_dec(&user->refs);
	}

	kfree_rcu(refs, rcu);
out:
	preempt_enable();

	return 0;
}

static const struct file_operations user_data_fops = {
	.write = user_events_write,
	.write_iter = user_events_write_iter,
	.unlocked_ioctl	= user_events_ioctl,
	.release = user_events_release,
};

/*
 * Maps the shared page into the user process for checking if event is enabled.
 */
static int user_status_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if (size != MAX_EVENTS)
		return -EINVAL;

	return remap_pfn_range(vma, vma->vm_start,
			       virt_to_phys(register_page_data) >> PAGE_SHIFT,
			       size, PAGE_READONLY);
}

static int user_status_show(struct seq_file *m, void *p)
{
	struct user_event *user;
	char status;
	int i, active = 0, busy = 0;

	mutex_lock(&event_mutex);

	hash_for_each(register_table, i, user, node) {
		status = register_page_data[user->index];

		seq_printf(m, "%d:%s", user->index, EVENT_NAME(user));

		if (status != 0) {
			seq_puts(m, " # Used by");
			if (status & EVENT_STATUS_FTRACE)
				seq_puts(m, " ftrace");
			if (status & EVENT_STATUS_PERF)
				seq_puts(m, " perf");
			if (status & EVENT_STATUS_OTHER)
				seq_puts(m, " other");
			busy++;
		}

		seq_puts(m, "\n");
		active++;
	}

	mutex_unlock(&event_mutex);

	seq_puts(m, "\n");
	seq_printf(m, "Active: %d\n", active);
	seq_printf(m, "Busy: %d\n", busy);
	seq_printf(m, "Max: %ld\n", MAX_EVENTS);

	return 0;
}

static ssize_t user_status_read(struct file *file, char __user *ubuf,
				size_t count, loff_t *ppos)
{
	/*
	 * Delay allocation of seq data until requested, most callers
	 * will never read the status file. They will only mmap.
	 */
	if (file->private_data == NULL) {
		int ret;

		if (*ppos != 0)
			return -EINVAL;

		ret = single_open(file, user_status_show, NULL);

		if (ret)
			return ret;
	}

	return seq_read(file, ubuf, count, ppos);
}

static loff_t user_status_seek(struct file *file, loff_t offset, int whence)
{
	if (file->private_data == NULL)
		return 0;

	return seq_lseek(file, offset, whence);
}

static int user_status_release(struct inode *node, struct file *file)
{
	if (file->private_data == NULL)
		return 0;

	return single_release(node, file);
}

static const struct file_operations user_status_fops = {
	.mmap = user_status_mmap,
	.read = user_status_read,
	.llseek  = user_status_seek,
	.release = user_status_release,
};

/*
 * Creates a set of tracefs files to allow user mode interactions.
 */
static int create_user_tracefs(void)
{
	struct dentry *edata, *emmap;

	edata = tracefs_create_file("user_events_data", 0644, NULL,
				    NULL, &user_data_fops);

	if (!edata) {
		pr_warn("Could not create tracefs 'user_events_data' entry\n");
		goto err;
	}

	/* mmap with MAP_SHARED requires writable fd */
	emmap = tracefs_create_file("user_events_status", 0644, NULL,
				    NULL, &user_status_fops);

	if (!emmap) {
		tracefs_remove(edata);
		pr_warn("Could not create tracefs 'user_events_mmap' entry\n");
		goto err;
	}

	return 0;
err:
	return -ENODEV;
}

static void set_page_reservations(bool set)
{
	int page;

	for (page = 0; page < MAX_PAGES; ++page) {
		void *addr = register_page_data + (PAGE_SIZE * page);

		if (set)
			SetPageReserved(virt_to_page(addr));
		else
			ClearPageReserved(virt_to_page(addr));
	}
}

static int __init trace_events_user_init(void)
{
	int ret;

	/* Zero all bits beside 0 (which is reserved for failures) */
	bitmap_zero(page_bitmap, MAX_EVENTS);
	set_bit(0, page_bitmap);

	register_page_data = kzalloc(MAX_EVENTS, GFP_KERNEL);

	if (!register_page_data)
		return -ENOMEM;

	set_page_reservations(true);

	ret = create_user_tracefs();

	if (ret) {
		pr_warn("user_events could not register with tracefs\n");
		set_page_reservations(false);
		kfree(register_page_data);
		return ret;
	}

	if (dyn_event_register(&user_event_dops))
		pr_warn("user_events could not register with dyn_events\n");

	return 0;
}

fs_initcall(trace_events_user_init);
