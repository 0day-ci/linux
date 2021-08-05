/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM udp

#if !defined(_TRACE_UDP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_UDP_H

#include <linux/udp.h>
#include <linux/tracepoint.h>

TRACE_EVENT(udp_fail_queue_rcv_skb,

	TP_PROTO(int rc, struct sock *sk),

	TP_ARGS(rc, sk),

	TP_STRUCT__entry(
		__field(int, rc)
		__field(__u16, lport)
	),

	TP_fast_assign(
		__entry->rc = rc;
		__entry->lport = inet_sk(sk)->inet_num;
	),

	TP_printk("rc=%d port=%hu", __entry->rc, __entry->lport)
);

TRACE_EVENT(udp_rcv,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(const void *, skbaddr)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
	),

	TP_printk("skbaddr=%px", __entry->skbaddr)
);

TRACE_EVENT(udp_send_skb,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(const void *, skbaddr)
	),

	TP_fast_assign(
		__entry->skaddr = sk;
		__entry->skbaddr = skb;
	),

	TP_printk("skaddr=%px, skbaddr=%px", __entry->skaddr, __entry->skbaddr)
);

#if IS_ENABLED(CONFIG_IPV6)
TRACE_EVENT(udp_v6_send_skb,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(const void *, skbaddr)
	),

	TP_fast_assign(
		__entry->skaddr = sk;
		__entry->skbaddr = skb;
	),

	TP_printk("skaddr=%px, skbaddr=%px", __entry->skaddr, __entry->skbaddr)
);
#endif

#endif /* _TRACE_UDP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
