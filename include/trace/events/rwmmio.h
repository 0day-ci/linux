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

	TP_PROTO(unsigned long caller, unsigned long parent, u64 val,
		 u8 width, volatile void __iomem *addr),

	TP_ARGS(caller, parent, val, width, addr),

	TP_STRUCT__entry(
		__field(u64, caller)
		__field(u64, parent)
		__field(u64, val)
		__field(u64, addr)
		__field(u8, width)
	),

	TP_fast_assign(
		__entry->caller = caller;
		__entry->parent = parent;
		__entry->val = val;
		__entry->addr = (unsigned long)(void *)addr;
		__entry->width = width;
	),

	TP_printk("%pS <- %pS width=%d val=%#llx addr=%#llx",
		(void *)(unsigned long)__entry->caller, (void *)(unsigned long)__entry->parent,
		__entry->width,	__entry->val, __entry->addr)
);

TRACE_EVENT(rwmmio_read,

	TP_PROTO(unsigned long caller, unsigned long parent, u8 width,
		 const volatile void __iomem *addr),

	TP_ARGS(caller, parent, width, addr),

	TP_STRUCT__entry(
		__field(u64, caller)
		__field(u64, parent)
		__field(u64, addr)
		__field(u8, width)
	),

	TP_fast_assign(
		__entry->caller = caller;
		__entry->parent = parent;
		__entry->addr = (unsigned long)(void *)addr;
		__entry->width = width;
	),

	TP_printk("%pS <- %pS width=%d addr=%#llx",
		 (void *)(unsigned long)__entry->caller, (void *)(unsigned long)__entry->parent,
		 __entry->width, __entry->addr)
);

#endif /* _TRACE_RWMMIO_H */

#include <trace/define_trace.h>
