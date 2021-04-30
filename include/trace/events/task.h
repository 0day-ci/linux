/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM task

#if !defined(_TRACE_TASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TASK_H
#include <linux/tracepoint.h>

TRACE_EVENT(task_newtask,

	TP_PROTO(struct task_struct *task, unsigned long clone_flags),

	TP_ARGS(task, clone_flags),

	TP_STRUCT__entry(
		__field(	pid_t,	pid)
		__string(comm, task->comm)
		__field( unsigned long, clone_flags)
		__field(	short,	oom_score_adj)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		__assign_str(comm, task->comm);
		__entry->clone_flags = clone_flags;
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("pid=%d comm=%s clone_flags=%lx oom_score_adj=%hd",
		  __entry->pid, __get_str(comm),
		__entry->clone_flags, __entry->oom_score_adj)
);

TRACE_EVENT(task_rename,

	TP_PROTO(struct task_struct *task, const char *comm),

	TP_ARGS(task, comm),

	TP_STRUCT__entry(
		__field(	pid_t,	pid)
		__string(oldcomm, task->comm)
		__string(newcomm, comm)
		__field(	short,	oom_score_adj)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		__assign_str(oldcomm, task->comm);
		__assign_str(newcomm, comm);
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("pid=%d oldcomm=%s newcomm=%s oom_score_adj=%hd",
		__entry->pid, __get_str(oldcomm),
		__get_str(newcomm), __entry->oom_score_adj)
);

TRACE_EVENT(task_exit,

	TP_PROTO(struct task_struct *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(pid_t,	pid)
		__field(short,	oom_score_adj)
		__field(int,	exit_signal)
		__field(int,	exit_code)
		__field(int,	exit_state)
		__string(comm, task->comm)

	),

	TP_fast_assign(
		__entry->pid = task->pid;
		__entry->oom_score_adj = task->signal->oom_score_adj;
		__entry->exit_signal = task->exit_signal;
		__entry->exit_code = task->exit_code;
		__entry->exit_state = task->exit_state;
		__assign_str(comm, task->comm);
	),

	TP_printk("pid=%d oom_score_adj=%hd exit_signal=%d exit_code=%d exit_state=0x%x comm=%s",
		  __entry->pid,
		  __entry->oom_score_adj, __entry->exit_signal,
		  __entry->exit_code, __entry->exit_state,
		  __get_str(comm))
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
