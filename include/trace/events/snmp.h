/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM snmp

#if !defined(_TRACE_SNMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SNMP_H

#include <linux/tracepoint.h>
#include <linux/skbuff.h>
#include <linux/snmp.h>

DECLARE_EVENT_CLASS(snmp_template,

	TP_PROTO(struct sk_buff *skb, int field, int val),

	TP_ARGS(skb, field, val),

	TP_STRUCT__entry(
		__field(void *, skbaddr)
		__field(int, field)
		__field(int, val)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
		__entry->field = field;
		__entry->val = val;
	),

	TP_printk("skbaddr=%p, field=%d, val=%d", __entry->skbaddr,
		  __entry->field, __entry->val)
);

#define DEFINE_SNMP_EVENT(proto)				\
DEFINE_EVENT(snmp_template, snmp_##proto,			\
	TP_PROTO(struct sk_buff *skb, int field, int val),	\
	TP_ARGS(skb, field, val)				\
)



DEFINE_SNMP_EVENT(udp);
DEFINE_SNMP_EVENT(udplite);

#define TRACE_SNMP(skb, proto, field, val) \
	trace_snmp_##proto(skb, field, val)

#endif

#include <trace/define_trace.h>
