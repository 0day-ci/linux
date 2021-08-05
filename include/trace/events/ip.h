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

#endif /* _TRACE_IP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
