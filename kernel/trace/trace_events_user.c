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
#include <linux/list.h>
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

/* Limit how long of an event name plus args within the subsystem. */
#define MAX_EVENT_DESC 512
#define EVENT_NAME(user_event) ((user_event)->tracepoint.name)

#define MAX_BPF_COPY_SIZE PAGE_SIZE
#define MAX_STACK_BPF_DATA 512
#define copy_nofault copy_from_iter_nocache

static char *register_page_data;

static DEFINE_MUTEX(reg_mutex);
static DEFINE_HASHTABLE(register_table, 4);
static DECLARE_BITMAP(page_bitmap, MAX_EVENTS);

struct user_event {
	struct tracepoint tracepoint;
	struct trace_event_call call;
	struct trace_event_class class;
	struct dyn_event devent;
	struct hlist_node node;
	struct list_head fields;
	atomic_t refcnt;
	int index;
	int flags;
};

struct user_event_refs {
	struct rcu_head rcu;
	int count;
	struct user_event *events[];
};

typedef void (*user_event_func_t) (struct user_event *user, struct iov_iter *i,
				   void *tpdata);

static int user_event_parse(char *name, char *args, char *flags,
			    struct user_event **newuser);

static u32 user_event_key(char *name)
{
	return jhash(name, strlen(name), 0);
}

static struct list_head *user_event_get_fields(struct trace_event_call *call)
{
	struct user_event *user = (struct user_event *)call->data;

	return &user->fields;
}

/*
 * Parses a register command for user_events
 * Format: event_name[:FLAG1[,FLAG2...]] [field1[;field2...]]
 *
 * Example event named test with a 20 char msg field with a unsigned int after:
 * test char[20] msg;unsigned int id
 *
 * NOTE: Offsets are from the user data perspective, they are not from the
 * trace_entry/buffer perspective. We automatically add the common properties
 * sizes to the offset for the user.
 */
static int user_event_parse_cmd(char *raw_command, struct user_event **newuser)
{
	char *name = raw_command;
	char *args = strpbrk(name, " ");
	char *flags;

	if (args)
		*args++ = 0;

	flags = strpbrk(name, ":");

	if (flags)
		*flags++ = 0;

	return user_event_parse(name, args, flags, newuser);
}

static int user_field_array_size(const char *type)
{
	const char *start = strchr(type, '[');
	int size = 0;

	if (start == NULL)
		return -EINVAL;

	start++;

	while (*start >= '0' && *start <= '9')
		size = (size * 10) + (*start++ - '0');

	if (*start != ']')
		return -EINVAL;

	return size;
}

static int user_field_size(const char *type)
{
	/* long is not allowed from a user, since it's ambigious in size */
	if (strcmp(type, "s64") == 0)
		return sizeof(s64);
	if (strcmp(type, "u64") == 0)
		return sizeof(u64);
	if (strcmp(type, "s32") == 0)
		return sizeof(s32);
	if (strcmp(type, "u32") == 0)
		return sizeof(u32);
	if (strcmp(type, "int") == 0)
		return sizeof(int);
	if (strcmp(type, "unsigned int") == 0)
		return sizeof(unsigned int);
	if (strcmp(type, "s16") == 0)
		return sizeof(s16);
	if (strcmp(type, "u16") == 0)
		return sizeof(u16);
	if (strcmp(type, "short") == 0)
		return sizeof(short);
	if (strcmp(type, "unsigned short") == 0)
		return sizeof(unsigned short);
	if (strcmp(type, "s8") == 0)
		return sizeof(s8);
	if (strcmp(type, "u8") == 0)
		return sizeof(u8);
	if (strcmp(type, "char") == 0)
		return sizeof(char);
	if (strcmp(type, "unsigned char") == 0)
		return sizeof(unsigned char);
	if (strstr(type, "char[") == type)
		return user_field_array_size(type);
	if (strstr(type, "unsigned char[") == type)
		return user_field_array_size(type);
	if (strstr(type, "__data_loc ") == type)
		return sizeof(u32);
	if (strstr(type, "__rel_loc ") == type)
		return sizeof(u32);

	/* Uknown basic type, error */
	return -EINVAL;
}

static void user_event_destroy_fields(struct user_event *user)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head = &user->fields;

	list_for_each_entry_safe(field, next, head, link) {
		list_del(&field->link);
		kfree(field);
	}
}

static int user_event_add_field(struct user_event *user, const char *type,
				const char *name, int offset, int size,
				int is_signed, int filter_type)
{
	struct ftrace_event_field *field;

	field = kmalloc(sizeof(*field), GFP_KERNEL);

	if (!field)
		return -ENOMEM;

	field->type = type;
	field->name = name;
	field->offset = offset;
	field->size = size;
	field->is_signed = is_signed;
	field->filter_type = filter_type;

	list_add(&field->link, &user->fields);

	return 0;
}

/*
 * Parses the values of a field within the description
 * Format: type name [size]
 */
static int user_event_parse_field(char *field, struct user_event *user,
				  u32 *offset)
{
	char *part, *type, *name;
	u32 depth = 0, saved_offset = *offset;
	int size = -EINVAL;
	bool is_struct = false;

	field = skip_spaces(field);

	if (*field == 0)
		return 0;

	/* Handle types that have a space within */
	if (strstr(field, "unsigned ") == field) {
		type = field;
		field = strpbrk(field + sizeof("unsigned"), " ");
		goto skip_next;
	} else if (strstr(field, "struct ") == field) {
		type = field;
		field = strpbrk(field + sizeof("struct"), " ");
		is_struct = true;
		goto skip_next;
	} else if (strstr(field, "__data_loc unsigned ") == field) {
		type = field;
		field = strpbrk(field + sizeof("__data_loc unsigned"), " ");
		goto skip_next;
	} else if (strstr(field, "__data_loc ") == field) {
		type = field;
		field = strpbrk(field + sizeof("__data_loc"), " ");
		goto skip_next;
	} else if (strstr(field, "__rel_loc unsigned ") == field) {
		type = field;
		field = strpbrk(field + sizeof("__rel_loc unsigned"), " ");
		goto skip_next;
	} else if (strstr(field, "__rel_loc ") == field) {
		type = field;
		field = strpbrk(field + sizeof("__rel_loc"), " ");
		goto skip_next;
	}
	goto parse;
skip_next:
	if (field == NULL)
		return -EINVAL;

	*field++ = 0;
	depth++;
parse:
	while ((part = strsep(&field, " ")) != NULL) {
		switch (depth++) {
		case FIELD_DEPTH_TYPE:
			type = part;
			break;
		case FIELD_DEPTH_NAME:
			name = part;
			break;
		case FIELD_DEPTH_SIZE:
			if (!is_struct)
				return -EINVAL;

			if (kstrtou32(part, 10, &size))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	}

	if (depth < FIELD_DEPTH_SIZE)
		return -EINVAL;

	if (depth == FIELD_DEPTH_SIZE)
		size = user_field_size(type);

	if (size == 0)
		return -EINVAL;

	if (size < 0)
		return size;

	*offset = saved_offset + size;

	return user_event_add_field(user, type, name, saved_offset, size,
				    type[0] != 'u', FILTER_OTHER);
}

static void user_event_parse_flags(struct user_event *user, char *flags)
{
	char *flag;

	if (flags == NULL)
		return;

	while ((flag = strsep(&flags, ",")) != NULL) {
		if (strcmp(flag, "BPF_ITER") == 0)
			user->flags |= FLAG_BPF_ITER;
	}
}

static int user_event_parse_fields(struct user_event *user, char *args)
{
	char *field;
	u32 offset = sizeof(struct trace_entry);
	int ret = -EINVAL;

	if (args == NULL)
		return 0;

	while ((field = strsep(&args, ";")) != NULL) {
		ret = user_event_parse_field(field, user, &offset);

		if (ret)
			break;
	}

	return ret;
}

static char *user_field_format(const char *type)
{
	if (strcmp(type, "s64") == 0)
		return "%lld";
	if (strcmp(type, "u64") == 0)
		return "%llu";
	if (strcmp(type, "s32") == 0)
		return "%d";
	if (strcmp(type, "u32") == 0)
		return "%u";
	if (strcmp(type, "int") == 0)
		return "%d";
	if (strcmp(type, "unsigned int") == 0)
		return "%u";
	if (strcmp(type, "s16") == 0)
		return "%d";
	if (strcmp(type, "u16") == 0)
		return "%u";
	if (strcmp(type, "short") == 0)
		return "%d";
	if (strcmp(type, "unsigned short") == 0)
		return "%u";
	if (strcmp(type, "s8") == 0)
		return "%d";
	if (strcmp(type, "u8") == 0)
		return "%u";
	if (strcmp(type, "char") == 0)
		return "%d";
	if (strcmp(type, "unsigned char") == 0)
		return "%u";
	if (strstr(type, "char[") != 0)
		return "%s";

	/* Unknown, likely struct, allowed treat as 64-bit */
	return "%llu";
}

static bool user_field_is_dyn_string(const char *type)
{
	if (strstr(type, "__data_loc ") == type ||
	    strstr(type, "__rel_loc ") == type) {
		if (strstr(type, "char[") != 0)
			return true;
	}

	return false;
}

#define LEN_OR_ZERO (len ? len - pos : 0)
static int user_event_set_print_fmt(struct user_event *user, char *buf, int len)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head = &user->fields;
	int pos = 0, depth = 0;

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	list_for_each_entry_safe_reverse(field, next, head, link) {
		if (depth != 0)
			pos += snprintf(buf + pos, LEN_OR_ZERO, " ");

		pos += snprintf(buf + pos, LEN_OR_ZERO, "%s=%s",
				field->name, user_field_format(field->type));

		depth++;
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"");

	list_for_each_entry_safe_reverse(field, next, head, link) {
		if (user_field_is_dyn_string(field->type))
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", __get_str(%s)", field->name);
		else
			pos += snprintf(buf + pos, LEN_OR_ZERO,
					", REC->%s", field->name);
	}

	return pos + 1;
}
#undef LEN_OR_ZERO

static int user_event_create_print_fmt(struct user_event *user)
{
	char *print_fmt;
	int len;

	len = user_event_set_print_fmt(user, NULL, 0);

	print_fmt = kmalloc(len, GFP_KERNEL);

	if (!print_fmt)
		return -ENOMEM;

	user_event_set_print_fmt(user, print_fmt, len);

	user->call.print_fmt = print_fmt;

	return 0;
}

static struct trace_event_fields user_event_fields_array[] = {
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

	/* Must destroy fields before call removal */
	user_event_destroy_fields(user);

	ret = trace_remove_event_call(&user->call);

	if (ret)
		return ret;

	dyn_event_remove(&user->devent);

	register_page_data[user->index] = 0;
	clear_bit(user->index, page_bitmap);
	hash_del(&user->node);

	kfree(user->call.print_fmt);
	kfree(EVENT_NAME(user));
	kfree(user);

	return ret;
}

static struct user_event *find_user_event(char *name, u32 *outkey)
{
	struct user_event *user;
	u32 key = user_event_key(name);

	*outkey = key;

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

	file = (struct trace_event_file *)tpdata;

	if (!file ||
	    !(file->flags & EVENT_FILE_FL_ENABLED) ||
	    trace_trigger_soft_disabled(file))
		return;

	entry = trace_event_buffer_reserve(&event_buffer, file,
					   sizeof(*entry) + i->count);

	if (unlikely(!entry))
		return;

	if (unlikely(!copy_nofault(entry + 1, i->count, i)))
		return;

	trace_event_buffer_commit(&event_buffer);
}

#ifdef CONFIG_PERF_EVENTS
static void user_event_bpf(struct user_event *user, struct iov_iter *i)
{
	struct user_bpf_context context;
	struct user_bpf_iter bpf_i;
	char fast_data[MAX_STACK_BPF_DATA];
	void *temp = NULL;

	if ((user->flags & FLAG_BPF_ITER) && iter_is_iovec(i)) {
		/* Raw iterator */
		context.data_type = USER_BPF_DATA_ITER;
		context.data_len = i->count;
		context.iter = &bpf_i;

		bpf_i.iov_offset = i->iov_offset;
		bpf_i.iov = i->iov;
		bpf_i.nr_segs = i->nr_segs;
	} else if (i->nr_segs == 1 && iter_is_iovec(i)) {
		/* Single buffer from user */
		context.data_type = USER_BPF_DATA_USER;
		context.data_len = i->count;
		context.udata = i->iov->iov_base + i->iov_offset;
	} else {
		/* Multi buffer from user */
		struct iov_iter copy = *i;
		size_t copy_size = min(i->count, MAX_BPF_COPY_SIZE);

		context.data_type = USER_BPF_DATA_KERNEL;
		context.kdata = fast_data;

		if (unlikely(copy_size > sizeof(fast_data))) {
			temp = kmalloc(copy_size, GFP_NOWAIT);

			if (temp)
				context.kdata = temp;
			else
				copy_size = sizeof(fast_data);
		}

		context.data_len = copy_nofault(context.kdata,
						copy_size, &copy);
	}

	trace_call_bpf(&user->call, &context);

	kfree(temp);
}

/*
 * Writes the user supplied payload out to perf ring buffer or eBPF program.
 */
static void user_event_perf(struct user_event *user, struct iov_iter *i,
			    void *tpdata)
{
	struct hlist_head *perf_head;

	if (bpf_prog_array_valid(&user->call))
		user_event_bpf(user, i);

	perf_head = this_cpu_ptr(user->call.perf_events);

	if (perf_head && !hlist_empty(perf_head)) {
		struct trace_entry *perf_entry;
		struct pt_regs *regs;
		size_t size = sizeof(*perf_entry) + i->count;
		int context;

		perf_entry = perf_trace_buf_alloc(ALIGN(size, 8),
						  &regs, &context);

		if (unlikely(!perf_entry))
			return;

		perf_fetch_caller_regs(regs);

		if (unlikely(!copy_nofault(perf_entry + 1, i->count, i)))
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

		rcu_read_lock_sched();

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

		rcu_read_unlock_sched();
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
	atomic_inc(&user->refcnt);
	update_reg_page_for(user);
	return 0;
dec:
	update_reg_page_for(user);
	atomic_dec(&user->refcnt);
	return 0;
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

	mutex_lock(&reg_mutex);
	ret = user_event_parse_cmd(name, &user);
	mutex_unlock(&reg_mutex);

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

	return atomic_read(&user->refcnt) != 0;
}

static int user_event_free(struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);

	return destroy_user_event(user);
}

static int user_field_match(struct ftrace_event_field *field, int argc,
			    const char **argv, int *iout)
{
	char field_name[256];
	char arg_name[256];
	int len, pos, i = *iout;
	bool colon = false;

	if (i >= argc)
		return false;

	len = sizeof(arg_name);
	pos = 0;

	for (; i < argc; ++i) {
		if (i != *iout)
			pos += snprintf(arg_name + pos, len - pos, " ");

		pos += snprintf(arg_name + pos, len - pos, argv[i]);

		if (strchr(argv[i], ';')) {
			++i;
			colon = true;
			break;
		}
	}

	len = sizeof(field_name);
	pos = 0;

	pos += snprintf(field_name + pos, len - pos, field->type);
	pos += snprintf(field_name + pos, len - pos, " ");
	pos += snprintf(field_name + pos, len - pos, field->name);

	if (colon)
		pos += snprintf(field_name + pos, len - pos, ";");

	*iout = i;

	return strcmp(arg_name, field_name) == 0;
}

static bool user_fields_match(struct user_event *user, int argc,
			      const char **argv)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head = &user->fields;
	int i = 0;

	list_for_each_entry_safe_reverse(field, next, head, link)
		if (!user_field_match(field, argc, argv, &i))
			return false;

	if (i != argc)
		return false;

	return true;
}

static bool user_event_match(const char *system, const char *event,
			     int argc, const char **argv, struct dyn_event *ev)
{
	struct user_event *user = container_of(ev, struct user_event, devent);
	bool match;

	match = strcmp(EVENT_NAME(user), event) == 0 &&
		(!system || strcmp(system, USER_EVENTS_SYSTEM) == 0);

	if (match && argc > 0)
		match = user_fields_match(user, argc, argv);

	return match;
}

static struct dyn_event_operations user_event_dops = {
	.create = user_event_create,
	.show = user_event_show,
	.is_busy = user_event_is_busy,
	.free = user_event_free,
	.match = user_event_match,
};

static int user_event_trace_register(struct user_event *user)
{
	int ret;

	ret = register_trace_event(&user->call.event);

	if (!ret)
		return -ENODEV;

	ret = trace_add_event_call(&user->call);

	if (ret)
		unregister_trace_event(&user->call.event);

	return ret;
}

/*
 * Parses the event name, arguments and flags then registers if successful.
 */
static int user_event_parse(char *name, char *args, char *flags,
			    struct user_event **newuser)
{
	int ret;
	int index;
	u32 key;
	struct user_event *user = find_user_event(name, &key);

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
	INIT_LIST_HEAD(&user->fields);

	user->tracepoint.name = name;

	user_event_parse_flags(user, flags);

	ret = user_event_parse_fields(user, args);

	if (ret)
		goto put_user;

	ret = user_event_create_print_fmt(user);

	if (ret)
		goto put_user;

	user->call.data = user;
	user->call.class = &user->class;
	user->call.name = name;
	user->call.flags = TRACE_EVENT_FL_TRACEPOINT;
	user->call.tp = &user->tracepoint;
	user->call.event.funcs = &user_event_funcs;

	user->class.system = USER_EVENTS_SYSTEM;
	user->class.fields_array = user_event_fields_array;
	user->class.get_fields = user_event_get_fields;
	user->class.reg = user_event_reg;
	user->class.probe = user_event_ftrace;
#ifdef CONFIG_PERF_EVENTS
	user->class.perf_probe = user_event_perf;
#endif

	mutex_lock(&event_mutex);
	ret = user_event_trace_register(user);
	mutex_unlock(&event_mutex);

	if (ret)
		goto put_user;

	user->index = index;
	dyn_event_init(&user->devent, &user_event_dops);
	dyn_event_add(&user->devent);
	set_bit(user->index, page_bitmap);
	hash_add(register_table, &user->node, key);

	*newuser = user;
	return 0;
put_user:
	user_event_destroy_fields(user);
	kfree(user);
put_name:
	kfree(name);
	return ret;
}

/*
 * Deletes a previously created event if it is no longer being used.
 */
static int delete_user_event(char *name)
{
	u32 key;
	int ret;
	struct user_event *user = find_user_event(name, &key);

	if (!user)
		return -ENOENT;

	if (atomic_read(&user->refcnt) != 0)
		return -EBUSY;

	mutex_lock(&event_mutex);
	ret = destroy_user_event(user);
	mutex_unlock(&event_mutex);

	return ret;
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

	if (unlikely(copy_from_iter(&idx, sizeof(idx), i) != sizeof(idx)))
		return -EFAULT;

	rcu_read_lock_sched();

	refs = rcu_dereference_sched(file->private_data);

	if (likely(refs && idx < refs->count))
		user = refs->events[idx];

	rcu_read_unlock_sched();

	if (unlikely(user == NULL))
		return -ENOENT;

	tp = &user->tracepoint;

	if (likely(atomic_read(&tp->key.enabled) > 0)) {
		struct tracepoint_func *probe_func_ptr;
		user_event_func_t probe_func;
		struct iov_iter copy;
		void *tpdata;

		if (unlikely(iov_iter_fault_in_readable(i, i->count)))
			return -EFAULT;

		rcu_read_lock_sched();
		pagefault_disable();

		probe_func_ptr = rcu_dereference_sched(tp->funcs);

		if (probe_func_ptr) {
			do {
				copy = *i;
				probe_func = probe_func_ptr->func;
				tpdata = probe_func_ptr->data;
				probe_func(user, &copy, tpdata);
			} while ((++probe_func_ptr)->func);
		}

		pagefault_enable();
		rcu_read_unlock_sched();
	}

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

	rcu_read_lock_sched();
	refs = rcu_dereference_sched(file->private_data);
	rcu_read_unlock_sched();

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

	atomic_inc(&user->refcnt);

	rcu_assign_pointer(file->private_data, new_refs);

	if (refs)
		kfree_rcu(refs, rcu);

	return i;
}

static long user_reg_get(struct user_reg __user *ureg, struct user_reg *kreg)
{
	u32 size;
	long ret;

	ret = get_user(size, &ureg->size);

	if (ret)
		return ret;

	if (size > PAGE_SIZE)
		return -E2BIG;

	return copy_struct_from_user(kreg, sizeof(*kreg), ureg, size);
}

/*
 * Registers a user_event on behalf of a user process.
 */
static long user_events_ioctl_reg(struct file *file, unsigned long uarg)
{
	struct user_reg __user *ureg = (struct user_reg __user *)uarg;
	struct user_reg reg;
	struct user_event *user;
	char *name;
	long ret;

	ret = user_reg_get(ureg, &reg);

	if (ret)
		return ret;

	name = strndup_user((const char __user *)(uintptr_t)reg.name_args,
			    MAX_EVENT_DESC);

	if (IS_ERR(name)) {
		ret = PTR_ERR(name);
		return ret;
	}

	ret = user_event_parse_cmd(name, &user);

	if (ret < 0)
		return ret;

	ret = user_events_ref_add(file, user);

	if (ret < 0)
		return ret;

	put_user((u32)ret, &ureg->write_index);
	put_user(user->index, &ureg->status_index);

	return 0;
}

/*
 * Deletes a user_event on behalf of a user process.
 */
static long user_events_ioctl_del(struct file *file, unsigned long uarg)
{
	void __user *ubuf = (void __user *)uarg;
	char *name;
	long ret;

	name = strndup_user(ubuf, MAX_EVENT_DESC);

	if (IS_ERR(name))
		return PTR_ERR(name);

	ret = delete_user_event(name);

	kfree(name);

	return ret;
}

/*
 * Handles the ioctl from user mode to register or alter operations.
 */
static long user_events_ioctl(struct file *file, unsigned int cmd,
			      unsigned long uarg)
{
	long ret = -ENOTTY;

	switch (cmd) {
	case DIAG_IOCSREG:
		mutex_lock(&reg_mutex);
		ret = user_events_ioctl_reg(file, uarg);
		mutex_unlock(&reg_mutex);
		break;

	case DIAG_IOCSDEL:
		mutex_lock(&reg_mutex);
		ret = user_events_ioctl_del(file, uarg);
		mutex_unlock(&reg_mutex);
		break;
	}

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

	rcu_read_lock_sched();
	refs = rcu_dereference_sched(file->private_data);
	rcu_read_unlock_sched();

	if (!refs)
		goto out;

	for (i = 0; i < refs->count; ++i) {
		user = refs->events[i];

		if (user)
			atomic_dec(&user->refcnt);
	}

	kfree_rcu(refs, rcu);
out:
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
			       size, vm_get_page_prot(VM_READ));
}

static int user_status_show(struct seq_file *m, void *p)
{
	struct user_event *user;
	char status;
	int i, active = 0, busy = 0, flags;

	mutex_lock(&reg_mutex);

	hash_for_each(register_table, i, user, node) {
		status = register_page_data[user->index];
		flags = user->flags;

		seq_printf(m, "%d:%s", user->index, EVENT_NAME(user));

		if (flags != 0 || status != 0)
			seq_puts(m, " #");

		if (status != 0) {
			seq_puts(m, " Used by");
			if (status & EVENT_STATUS_FTRACE)
				seq_puts(m, " ftrace");
			if (status & EVENT_STATUS_PERF)
				seq_puts(m, " perf");
			if (status & EVENT_STATUS_OTHER)
				seq_puts(m, " other");
			busy++;
		}

		if (flags & FLAG_BPF_ITER)
			seq_puts(m, " FLAG:BPF_ITER");

		seq_puts(m, "\n");
		active++;
	}

	mutex_unlock(&reg_mutex);

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
