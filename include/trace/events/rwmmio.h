/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwmmio

#if !defined(_TRACE_MMIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(rwmmio_write,

	TP_PROTO(unsigned long fn, const char *width, u64 val, volatile void __iomem *addr),

	TP_ARGS(fn, width, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__string(width, width)
		__field(u64, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__assign_str(width, width);
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS %s addr=%#llx val=%#llx",
		(void *)__entry->fn, __get_str(width), __entry->addr, __entry->val)
);

TRACE_EVENT(rwmmio_read,

	TP_PROTO(unsigned long fn, const char *width, const volatile void __iomem *addr),

	TP_ARGS(fn, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__string(width, width)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__assign_str(width, width);
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS %s addr=%#llx",
		 (void *)__entry->fn, __get_str(width), __entry->addr)
);

#endif /* _TRACE_MMIO_H */

#include <trace/define_trace.h>
