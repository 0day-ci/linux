/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dyndbg

#if !defined(_TRACE_DYNDBG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DYNDBG_H

#include <linux/tracepoint.h>

/* capture pr_debug() callsite descriptor and message */
TRACE_EVENT(prdbg,
	TP_PROTO(const struct _ddebug *desc, const char *text, size_t len),

	TP_ARGS(desc, text, len),

	TP_STRUCT__entry(
		__dynamic_array(char, msg, len + 1)
	),

	TP_fast_assign(
		/*
		 * Each trace entry is printed in a new line.
		 * If the msg finishes with '\n', cut it off
		 * to avoid blank lines in the trace.
		 */
		if ((len > 0) && (text[len-1] == '\n'))
			len -= 1;

		memcpy(__get_str(msg), text, len);
		__get_str(msg)[len] = 0;
	),

	TP_printk("%s", __get_str(msg))

);

/* capture dev_dbg() callsite descriptor, device, and message */
TRACE_EVENT(devdbg,
	TP_PROTO(const struct _ddebug *desc, const struct device *dev,
		 const char *text, size_t len),

	TP_ARGS(desc, dev, text, len),

	TP_STRUCT__entry(
		__dynamic_array(char, msg, len + 1)
	),

	TP_fast_assign(
		/*
		 * Each trace entry is printed in a new line.
		 * If the msg finishes with '\n', cut it off
		 * to avoid blank lines in the trace.
		 */
		if ((len > 0) && (text[len-1] == '\n'))
			len -= 1;

		memcpy(__get_str(msg), text, len);
		__get_str(msg)[len] = 0;
	),

	TP_printk("%s", __get_str(msg))
);

#endif /* _TRACE_DYNDBG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
