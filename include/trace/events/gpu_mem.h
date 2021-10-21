/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GPU memory trace points
 *
 * Copyright (C) 2020 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_mem

#if !defined(_TRACE_GPU_MEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_MEM_H

#include <linux/tracepoint.h>

/*
 * The gpu_mem_total event indicates that there's an update to local or
 * global gpu memory counters.
 *
 * This event should be emitted whenever a GPU device (ctx_id == 0):
 *
 *   1) allocates memory.
 *   2) frees memory.
 *   3) imports memory from an external exporter.
 *
 * OR when a GPU device instance (ctx_id != 0):
 *
 *   1) allocates or acquires a reference to memory from another instance.
 *   2) frees or releases a reference to memory from another instance.
 *   3) imports memory from another GPU device instance.
 *
 * When ctx_id == 0, both mem_total and import_mem_total total counters
 * represent a global total.  When ctx_id == 0, these counters represent
 * an instance specifical total.
 *
 * Note allocation does not necessarily mean backing the memory with pages.
 *
 * @gpu_id: unique ID of the GPU.
 *
 * @ctx_id: an ID for specific instance of the GPU device.
 *
 * @mem_total: - total size of memory known to a GPU device, including
 *		 imports (ctx_id == 0)
 *	       - total size of memory known to a GPU device instance
 *		 (ctx_id != 0)
 *
 * @import_mem_total: - size of memory imported from outside GPU
 *			device (ctx_id == 0)
 *		      - size of memory imported into GPU device instance.
 *			(ctx_id == 0)
 */
TRACE_EVENT(gpu_mem_total,

	TP_PROTO(u32 gpu_id, u32 ctx_id, u64 mem_total, u64 import_mem_total),

	TP_ARGS(gpu_id, ctx_id, mem_total, import_mem_total),

	TP_STRUCT__entry(
		__field(u32, gpu_id)
		__field(u32, ctx_id)
		__field(u64, mem_total)
		__field(u64, import_mem_total)
	),

	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->ctx_id = ctx_id;
		__entry->mem_total = mem_total;
		__entry->import_mem_total = import_mem_total;
	),

	TP_printk("gpu_id=%u, ctx_id=%u, mem total=%llu, mem import total=%llu",
		  __entry->gpu_id,
		  __entry->ctx_id,
		  __entry->mem_total,
		  __entry->import_mem_total)
);

#endif /* _TRACE_GPU_MEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
