/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dns_resolver

#if !defined(_TRACE_DNS_RESOLVER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DNS_RESOLVER_H

#include <linux/tracepoint.h>

TRACE_EVENT(dns_query,
	TP_PROTO(
		const char *type,
		const char *name,
		size_t namelen,
		const char *options
	),

	TP_ARGS(type, name, namelen, options),

	TP_STRUCT__entry(
		__string(type, type)
		__string_len(name, name, namelen)
		__string(options, options)
	),

	TP_fast_assign(
		__assign_str(type, type);
		__assign_str_len(name, name, namelen);
		__assign_str(options, options);
	),

	TP_printk("t=%s n=%s o=%s",
		__get_str(type), __get_str(name), __get_str(options))
);

#endif /* _TRACE_DNS_RESOLVER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
