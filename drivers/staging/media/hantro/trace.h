/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hantro

#if !defined(__HANTRO_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __HANTRO_TRACE_H__

#include <linux/tracepoint.h>
#include <media/videobuf2-v4l2.h>

#include "hantro.h"

TRACE_EVENT(hantro_hevc_perf,
	TP_PROTO(struct hantro_ctx *ctx, u32 hw_cycles),

	TP_ARGS(ctx, hw_cycles),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(u32, hw_cycles)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->hw_cycles = hw_cycles;
	),

	TP_printk("minor = %d, %8d cycles / mb",
		  __entry->minor, __entry->hw_cycles)
);

#endif /* __HANTRO_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/staging/media/hantro
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
