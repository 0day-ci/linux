/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM watchdog

#if !defined(_TRACE_WATCHDOG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WATCHDOG_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(dev_operations_template,

	TP_PROTO(struct watchdog_device *wdd),

	TP_ARGS(wdd),

	TP_STRUCT__entry(
		__field(__u32, id)
	),

	TP_fast_assign(
		__entry->id = wdd->id;
	),

	TP_printk("id=%d",
		  __entry->id)
);

/*
 * Add a comment
 */
DEFINE_EVENT(dev_operations_template, watchdog_open,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));

DEFINE_EVENT(dev_operations_template, watchdog_close,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));

DEFINE_EVENT(dev_operations_template, watchdog_start,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));

DEFINE_EVENT(dev_operations_template, watchdog_stop,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));

DEFINE_EVENT(dev_operations_template, watchdog_ping,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));

DEFINE_EVENT(dev_operations_template, watchdog_keep_alive,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));

DEFINE_EVENT(dev_operations_template, watchdog_nowayout,
	     TP_PROTO(struct watchdog_device *wdd),
	     TP_ARGS(wdd));


TRACE_EVENT(watchdog_set_timeout,

	TP_PROTO(struct watchdog_device *wdd, u64 timeout),

	TP_ARGS(wdd, timeout),

	TP_STRUCT__entry(
		__field(__u32, id)
		__field(__u64, timeout)
	),

	TP_fast_assign(
		__entry->id		= wdd->id;
		__entry->timeout	= timeout;
	),

	TP_printk("id=%d timeout=%llus",
		  __entry->id, __entry->timeout)
);

TRACE_EVENT(watchdog_set_keep_alive,

	TP_PROTO(struct watchdog_device *wdd, u64 timeout),

	TP_ARGS(wdd, timeout),

	TP_STRUCT__entry(
		__field(__u32, id)
		__field(__u64, timeout)
	),

	TP_fast_assign(
		__entry->id		= wdd->id;
		__entry->timeout	= timeout;
	),

	TP_printk("id=%d keep_alive=%llums",
		  __entry->id, __entry->timeout)
);

#endif /* _TRACE_WATCHDOG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
