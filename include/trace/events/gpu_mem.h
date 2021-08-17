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
 * The gpu_memory_total event indicates that there's an update to either the
 * global or process total and imported gpu memory counters.
 *
 * This event should be emitted whenever the kernel device driver allocates,
 * frees, imports, unimports memory in the GPU addressable space.
 *
 * @gpu_id: This is the gpu id.
 *
 * @pid: Put 0 for global total, while positive pid for process total.
 *
 * @size: Total size of allocated and imported memory in bytes.
 *
 * @imported_size: Total size of imported memory in bytes.
 *
 */
TRACE_EVENT(gpu_mem_total,

	TP_PROTO(uint32_t gpu_id, uint32_t pid, uint64_t size, uint64_t imported_size),

	TP_ARGS(gpu_id, pid, size, imported_size),

	TP_STRUCT__entry(
		__field(uint32_t, gpu_id)
		__field(uint32_t, pid)
		__field(uint64_t, size)
		__field(uint64_t, imported_size)
	),

	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->pid = pid;
		__entry->size = size;
		__entry->imported_size = imported_size;
	),

	TP_printk("gpu_id=%u pid=%u size=%llu imported_size=%llu",
		__entry->gpu_id,
		__entry->pid,
		__entry->size,
		__entry->imported_size)
);

#endif /* _TRACE_GPU_MEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
