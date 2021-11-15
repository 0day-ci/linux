/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwmmio

#if !defined(_TRACE_RWMMIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RWMMIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(rwmmio_write,

	TP_PROTO(unsigned long fn, const char *width, volatile void __iomem *addr),

	TP_ARGS(fn, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, addr)
		__string(width, width)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->addr = (u64)(void *)addr;
		__assign_str(width, width);
	),

	TP_printk("%pS %s addr=%#llx",
		(void *)(unsigned long)__entry->fn, __get_str(width), __entry->addr)
);

TRACE_EVENT(rwmmio_read,

	TP_PROTO(unsigned long fn, const char *width, const volatile void __iomem *addr),

	TP_ARGS(fn, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, addr)
		__string(width, width)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->addr = (u64)(void *)addr;
		__assign_str(width, width);
	),

	TP_printk("%pS %s addr=%#llx",
		 (void *)(unsigned long)__entry->fn, __get_str(width), __entry->addr)
);

#endif /* _TRACE_RWMMIO_H */

#include <trace/define_trace.h>
