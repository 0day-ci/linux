/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv

#if !defined(_WWNR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _WWNR_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(event_wwnr,

	TP_PROTO(pid_t pid, char state, char event, char next_state, bool safe),

	TP_ARGS(pid, state, event, next_state, safe),

	TP_STRUCT__entry(
		__field(	pid_t,		pid		)
		__field(	char,		state		)
		__field(	char,		event		)
		__field(	char,		next_state	)
		__field(	bool,		safe		)
	),

	TP_fast_assign(
		__entry->pid = pid;
		__entry->state = state;
		__entry->event = event;
		__entry->next_state = next_state;
		__entry->safe = safe;
	),

	TP_printk("%d: %s x %s -> %s %s",
		__entry->pid,
		model_get_state_name_wwnr(__entry->state),
		model_get_event_name_wwnr(__entry->event),
		model_get_state_name_wwnr(__entry->next_state),
		__entry->safe ? "(safe)" : "")
);

TRACE_EVENT(error_wwnr,

	TP_PROTO(pid_t pid, char state, char event),

	TP_ARGS(pid, state, event),

	TP_STRUCT__entry(
		__field(	pid_t,		pid		)
		__field(	char,		state		)
		__field(	char,		event		)
	),

	TP_fast_assign(
		__entry->pid = pid;
		__entry->state = state;
		__entry->event = event;
	),

	TP_printk("%d event %s not expected in the state %s",
		__entry->pid,
		model_get_event_name_wwnr(__entry->event),
		model_get_state_name_wwnr(__entry->state))
);

#endif /* _WWNR_H */

/* This part ust be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE wwnr
#include <trace/define_trace.h>
