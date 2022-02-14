/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rv

#if !defined(_SAFETY_MONITOR_NWO_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _SAFETY_MONITOR_NWO_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(event_safe_wtd_nwo,

	TP_PROTO(char state, char event, char next_state, bool safe),

	TP_ARGS(state, event, next_state, safe),

	TP_STRUCT__entry(
		__field(	char,		state		)
		__field(	char,		event		)
		__field(	char,		next_state	)
		__field(	bool,		safe		)
	),

	TP_fast_assign(
		__entry->state = state;
		__entry->event = event;
		__entry->next_state = next_state;
		__entry->safe = safe;
	),

	TP_printk("%s x %s -> %s %s",
		model_get_state_name_safe_wtd_nwo(__entry->state),
		model_get_event_name_safe_wtd_nwo(__entry->event),
		model_get_state_name_safe_wtd_nwo(__entry->next_state),
		__entry->safe ? "(safe)" : "")
);

TRACE_EVENT(error_safe_wtd_nwo,

	TP_PROTO(char state, char event),

	TP_ARGS(state, event),

	TP_STRUCT__entry(
		__field(	char,		state		)
		__field(	char,		event		)
	),

	TP_fast_assign(
		__entry->state = state;
		__entry->event = event;
	),

	TP_printk("event %s not expected in the state %s",
		model_get_event_name_safe_wtd_nwo(__entry->event),
		model_get_state_name_safe_wtd_nwo(__entry->state))
);

#endif /* _SAFETY_MONITOR_NWO_H */

/* This part ust be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../kernel/trace/rv/monitor_safe_wtd_nwo/
#define TRACE_INCLUDE_FILE safe_wtd_nwo
#include <trace/define_trace.h>
