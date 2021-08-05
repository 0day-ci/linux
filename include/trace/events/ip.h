/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ip

#if !defined(_TRACE_IP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IP_H

#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/tracepoint.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/sock_diag.h>

#define TP_STORE_V4MAPPED(__entry, saddr, daddr)		\
	do {							\
		struct in6_addr *pin6;				\
								\
		pin6 = (struct in6_addr *)__entry->saddr_v6;	\
		ipv6_addr_set_v4mapped(saddr, pin6);		\
		pin6 = (struct in6_addr *)__entry->daddr_v6;	\
		ipv6_addr_set_v4mapped(daddr, pin6);		\
	} while (0)

#if IS_ENABLED(CONFIG_IPV6)
#define TP_STORE_ADDRS(__entry, saddr, daddr, saddr6, daddr6)		\
	do {								\
		if (sk->sk_family == AF_INET6) {			\
			struct in6_addr *pin6;				\
									\
			pin6 = (struct in6_addr *)__entry->saddr_v6;	\
			*pin6 = saddr6;					\
			pin6 = (struct in6_addr *)__entry->daddr_v6;	\
			*pin6 = daddr6;					\
		} else {						\
			TP_STORE_V4MAPPED(__entry, saddr, daddr);	\
		}							\
	} while (0)
#else
#define TP_STORE_ADDRS(__entry, saddr, daddr, saddr6, daddr6)	\
	TP_STORE_V4MAPPED(__entry, saddr, daddr)
#endif

TRACE_EVENT(ip_queue_xmit,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb),

	TP_STRUCT__entry(
		__field(const void *, skbaddr)
		__field(const void *, skaddr)
		__field(__u16, sport)
		__field(__u16, dport)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_sock *inet = inet_sk(sk);
		__be32 *p32;

		__entry->skbaddr = skb;
		__entry->skaddr = sk;

		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);

		p32 = (__be32 *) __entry->saddr;
		*p32 = inet->inet_saddr;

		p32 = (__be32 *) __entry->daddr;
		*p32 =  inet->inet_daddr;

		TP_STORE_ADDRS(__entry, inet->inet_saddr, inet->inet_daddr,
			      sk->sk_v6_rcv_saddr, sk->sk_v6_daddr);
	),

	TP_printk("sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c skbaddr=%px",
		  __entry->sport, __entry->dport, __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6, __entry->skbaddr)
);

TRACE_EVENT(ip_rcv,
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

#endif /* _TRACE_IP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
