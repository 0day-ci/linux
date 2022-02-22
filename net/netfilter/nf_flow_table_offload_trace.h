/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM nf

#if !defined(_NF_FLOW_TABLE_OFFLOAD_TRACE_) || defined(TRACE_HEADER_MULTI_READ)
#define _NF_FLOW_TABLE_OFFLOAD_TRACE_

#include <linux/tracepoint.h>
#include <net/netfilter/nf_tables.h>

DECLARE_EVENT_CLASS(
	nf_flow_offload_work_template,
	TP_PROTO(struct flow_offload_work *w),
	TP_ARGS(w),
	TP_STRUCT__entry(
		__field(void *, work)
		__field(void *, flowtable)
		__field(void *, flow)
	),
	TP_fast_assign(
		__entry->work = w;
		__entry->flowtable = w->flowtable;
		__entry->flow = w->flow;
	),
	TP_printk("work=%p flowtable=%p flow=%p",
		  __entry->work, __entry->flowtable, __entry->flow)
);

#define DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(name)				\
	DEFINE_EVENT(nf_flow_offload_work_template, name,		\
		     TP_PROTO(struct flow_offload_work *w), TP_ARGS(w))

DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(flow_offload_add);
DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(flow_offload_work_add);
DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(flow_offload_del);
DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(flow_offload_work_del);
DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(flow_offload_stats);
DEFINE_NF_FLOW_OFFLOAD_WORK_EVENT(flow_offload_work_stats);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../net/netfilter
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE nf_flow_table_offload_trace
#include <trace/define_trace.h>
