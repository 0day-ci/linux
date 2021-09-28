/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  IPv6 IOAM Lightweight Tunnel API
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#ifndef _UAPI_LINUX_IOAM6_IPTUNNEL_H
#define _UAPI_LINUX_IOAM6_IPTUNNEL_H

#include <linux/in6.h>
#include <linux/ioam6.h>
#include <linux/types.h>

enum {
	IOAM6_IPTUNNEL_MODE_UNSPEC,
	IOAM6_IPTUNNEL_MODE_INLINE,	/* direct insertion only */
	IOAM6_IPTUNNEL_MODE_ENCAP,	/* encap (ip6ip6) only */
	IOAM6_IPTUNNEL_MODE_AUTO,	/* inline or encap based on situation */
};

struct ioam6_iptunnel_trace {
	__u8 mode;
	struct in6_addr tundst;	/* unused for inline mode */
	struct ioam6_trace_hdr trace;
};

enum {
	IOAM6_IPTUNNEL_UNSPEC,
	IOAM6_IPTUNNEL_TRACE,
	__IOAM6_IPTUNNEL_MAX,
};

#define IOAM6_IPTUNNEL_MAX (__IOAM6_IPTUNNEL_MAX - 1)

#endif /* _UAPI_LINUX_IOAM6_IPTUNNEL_H */
