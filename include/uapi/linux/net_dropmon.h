/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __NET_DROPMON_H
#define __NET_DROPMON_H

#include <linux/types.h>
#include <uapi/linux/netlink.h>

struct net_dm_drop_point {
	__u8 pc[8];
	__u32 count;
};

#define is_drop_point_hw(x) do {\
	int ____i, ____j;\
	for (____i = 0; ____i < 8; i ____i++)\
		____j |= x[____i];\
	____j;\
} while (0)

#define NET_DM_CFG_VERSION  0
#define NET_DM_CFG_ALERT_COUNT  1
#define NET_DM_CFG_ALERT_DELAY 2
#define NET_DM_CFG_MAX 3

struct net_dm_config_entry {
	__u32 type;
	__u64 data __attribute__((aligned(8)));
};

struct net_dm_config_msg {
	__u32 entries;
	struct net_dm_config_entry options[0];
};

struct net_dm_alert_msg {
	__u32 entries;
	struct net_dm_drop_point points[0];
};

struct net_dm_user_msg {
	union {
		struct net_dm_config_msg user;
		struct net_dm_alert_msg alert;
	} u;
};


/* These are the netlink message types for this protocol */

enum {
	NET_DM_CMD_UNSPEC = 0,
	NET_DM_CMD_ALERT,
	NET_DM_CMD_CONFIG,
	NET_DM_CMD_START,
	NET_DM_CMD_STOP,
	NET_DM_CMD_PACKET_ALERT,
	NET_DM_CMD_CONFIG_GET,
	NET_DM_CMD_CONFIG_NEW,
	NET_DM_CMD_STATS_GET,
	NET_DM_CMD_STATS_NEW,
	_NET_DM_CMD_MAX,
};

#define NET_DM_CMD_MAX (_NET_DM_CMD_MAX - 1)

/*
 * Our group identifiers
 */
#define NET_DM_GRP_ALERT 1

enum net_dm_attr {
	NET_DM_ATTR_UNSPEC,

	NET_DM_ATTR_ALERT_MODE,			/* u8 */
	NET_DM_ATTR_PC,				/* u64 */
	NET_DM_ATTR_SYMBOL,			/* string */
	NET_DM_ATTR_IN_PORT,			/* nested */
	NET_DM_ATTR_TIMESTAMP,			/* u64 */
	NET_DM_ATTR_PROTO,			/* u16 */
	NET_DM_ATTR_PAYLOAD,			/* binary */
	NET_DM_ATTR_PAD,
	NET_DM_ATTR_TRUNC_LEN,			/* u32 */
	NET_DM_ATTR_ORIG_LEN,			/* u32 */
	NET_DM_ATTR_QUEUE_LEN,			/* u32 */
	NET_DM_ATTR_STATS,			/* nested */
	NET_DM_ATTR_HW_STATS,			/* nested */
	NET_DM_ATTR_ORIGIN,			/* u16 */
	NET_DM_ATTR_HW_TRAP_GROUP_NAME,		/* string */
	NET_DM_ATTR_HW_TRAP_NAME,		/* string */
	NET_DM_ATTR_HW_ENTRIES,			/* nested */
	NET_DM_ATTR_HW_ENTRY,			/* nested */
	NET_DM_ATTR_HW_TRAP_COUNT,		/* u32 */
	NET_DM_ATTR_SW_DROPS,			/* flag */
	NET_DM_ATTR_HW_DROPS,			/* flag */
	NET_DM_ATTR_FLOW_ACTION_COOKIE,		/* binary */
	NET_DM_ATTR_REASON,			/* string */

	__NET_DM_ATTR_MAX,
	NET_DM_ATTR_MAX = __NET_DM_ATTR_MAX - 1
};

/**
 * enum net_dm_alert_mode - Alert mode.
 * @NET_DM_ALERT_MODE_SUMMARY: A summary of recent drops is sent to user space.
 * @NET_DM_ALERT_MODE_PACKET: Each dropped packet is sent to user space along
 *                            with metadata.
 */
enum net_dm_alert_mode {
	NET_DM_ALERT_MODE_SUMMARY,
	NET_DM_ALERT_MODE_PACKET,
};

enum {
	NET_DM_ATTR_PORT_NETDEV_IFINDEX,	/* u32 */
	NET_DM_ATTR_PORT_NETDEV_NAME,		/* string */

	__NET_DM_ATTR_PORT_MAX,
	NET_DM_ATTR_PORT_MAX = __NET_DM_ATTR_PORT_MAX - 1
};

enum {
	NET_DM_ATTR_STATS_DROPPED,		/* u64 */

	__NET_DM_ATTR_STATS_MAX,
	NET_DM_ATTR_STATS_MAX = __NET_DM_ATTR_STATS_MAX - 1
};

enum net_dm_origin {
	NET_DM_ORIGIN_SW,
	NET_DM_ORIGIN_HW,
};

/* The reason of skb drop, which is used in kfree_skb_reason().
 * en...maybe they should be splited by group?
 *
 * Each item here should also be in 'TRACE_SKB_DROP_REASON', which is
 * used to translate the reason to string.
 */
enum skb_drop_reason {
	SKB_NOT_DROPPED_YET = 0,
	SKB_DROP_REASON_NOT_SPECIFIED,	/* drop reason is not specified */
	SKB_DROP_REASON_NO_SOCKET,	/* socket not found */
	SKB_DROP_REASON_PKT_TOO_SMALL,	/* packet size is too small */
	SKB_DROP_REASON_TCP_CSUM,	/* TCP checksum error */
	SKB_DROP_REASON_SOCKET_FILTER,	/* dropped by socket filter */
	SKB_DROP_REASON_UDP_CSUM,	/* UDP checksum error */
	SKB_DROP_REASON_NETFILTER_DROP,	/* dropped by netfilter */
	SKB_DROP_REASON_OTHERHOST,	/* packet don't belong to current
					 * host (interface is in promisc
					 * mode)
					 */
	SKB_DROP_REASON_IP_CSUM,	/* IP checksum error */
	SKB_DROP_REASON_IP_INHDR,	/* there is something wrong with
					 * IP header (see
					 * IPSTATS_MIB_INHDRERRORS)
					 */
	SKB_DROP_REASON_IP_RPFILTER,	/* IP rpfilter validate failed.
					 * see the document for rp_filter
					 * in ip-sysctl.rst for more
					 * information
					 */
	SKB_DROP_REASON_UNICAST_IN_L2_MULTICAST, /* destination address of L2
						  * is multicast, but L3 is
						  * unicast.
						  */
	SKB_DROP_REASON_XFRM_POLICY,	/* xfrm policy check failed */
	SKB_DROP_REASON_IP_NOPROTO,	/* no support for IP protocol */
	SKB_DROP_REASON_SOCKET_RCVBUFF,	/* socket receive buff is full */
	SKB_DROP_REASON_PROTO_MEM,	/* proto memory limition, such as
					 * udp packet drop out of
					 * udp_memory_allocated.
					 */
	SKB_DROP_REASON_TCP_MD5NOTFOUND,	/* no MD5 hash and one
						 * expected, corresponding
						 * to LINUX_MIB_TCPMD5NOTFOUND
						 */
	SKB_DROP_REASON_TCP_MD5UNEXPECTED,	/* MD5 hash and we're not
						 * expecting one, corresponding
						 * to LINUX_MIB_TCPMD5UNEXPECTED
						 */
	SKB_DROP_REASON_TCP_MD5FAILURE,	/* MD5 hash and its wrong,
					 * corresponding to
					 * LINUX_MIB_TCPMD5FAILURE
					 */
	SKB_DROP_REASON_SOCKET_BACKLOG,	/* failed to add skb to socket
					 * backlog (see
					 * LINUX_MIB_TCPBACKLOGDROP)
					 */
	SKB_DROP_REASON_TCP_FLAGS,	/* TCP flags invalid */
	SKB_DROP_REASON_TCP_ZEROWINDOW,	/* TCP receive window size is zero,
					 * see LINUX_MIB_TCPZEROWINDOWDROP
					 */
	SKB_DROP_REASON_TCP_OLD_DATA,	/* the TCP data reveived is already
					 * received before (spurious retrans
					 * may happened), see
					 * LINUX_MIB_DELAYEDACKLOST
					 */
	SKB_DROP_REASON_TCP_OVERWINDOW,	/* the TCP data is out of window,
					 * the seq of the first byte exceed
					 * the right edges of receive
					 * window
					 */
	SKB_DROP_REASON_TCP_OFOMERGE,	/* the data of skb is already in
					 * the ofo queue, corresponding to
					 * LINUX_MIB_TCPOFOMERGE
					 */
	SKB_DROP_REASON_IP_OUTNOROUTES,	/* route lookup failed */
	SKB_DROP_REASON_BPF_CGROUP_EGRESS,	/* dropped by
						 * BPF_PROG_TYPE_CGROUP_SKB
						 * eBPF program
						 */
	SKB_DROP_REASON_IPV6DISABLED,	/* IPv6 is disabled on the device */
	SKB_DROP_REASON_NEIGH_CREATEFAIL,	/* failed to create neigh
						 * entry
						 */
	SKB_DROP_REASON_NEIGH_FAILED,	/* neigh entry in failed state */
	SKB_DROP_REASON_NEIGH_QUEUEFULL,	/* arp_queue for neigh
						 * entry is full
						 */
	SKB_DROP_REASON_NEIGH_DEAD,	/* neigh entry is dead */
	SKB_DROP_REASON_TC_EGRESS,	/* dropped in TC egress HOOK */
	SKB_DROP_REASON_QDISC_DROP,	/* dropped by qdisc when packet
					 * outputting (failed to enqueue to
					 * current qdisc)
					 */
	SKB_DROP_REASON_CPU_BACKLOG,	/* failed to enqueue the skb to
					 * the per CPU backlog queue. This
					 * can be caused by backlog queue
					 * full (see netdev_max_backlog in
					 * net.rst) or RPS flow limit
					 */
	SKB_DROP_REASON_XDP,		/* dropped by XDP in input path */
	SKB_DROP_REASON_TC_INGRESS,	/* dropped in TC ingress HOOK */
	SKB_DROP_REASON_PTYPE_ABSENT,	/* not packet_type found to handle
					 * the skb. For an etner packet,
					 * this means that L3 protocol is
					 * not supported
					 */
	SKB_DROP_REASON_SKB_CSUM,	/* sk_buff checksum computation
					 * error
					 */
	SKB_DROP_REASON_SKB_GSO_SEG,	/* gso segmentation error */
	SKB_DROP_REASON_SKB_UCOPY_FAULT,	/* failed to copy data from
						 * user space, e.g., via
						 * zerocopy_sg_from_iter()
						 * or skb_orphan_frags_rx()
						 */
	SKB_DROP_REASON_DEV_HDR,	/* device driver specific
					 * header/metadata is invalid
					 */
	/* the device is not ready to xmit/recv due to any of its data
	 * structure that is not up/ready/initialized, e.g., the IFF_UP is
	 * not set, or driver specific tun->tfiles[txq] is not initialized
	 */
	SKB_DROP_REASON_DEV_READY,
	SKB_DROP_REASON_FULL_RING,	/* ring buffer is full */
	SKB_DROP_REASON_NOMEM,		/* error due to OOM */
	SKB_DROP_REASON_HDR_TRUNC,	/* failed to trunc/extract the header
					 * from networking data, e.g., failed
					 * to pull the protocol header from
					 * frags via pskb_may_pull()
					 */
	SKB_DROP_REASON_TAP_FILTER,	/* dropped by (ebpf) filter directly
					 * attached to tun/tap, e.g., via
					 * TUNSETFILTEREBPF
					 */
	SKB_DROP_REASON_TAP_TXFILTER,	/* dropped by tx filter implemented
					 * at tun/tap, e.g., check_filter()
					 */
	SKB_DROP_REASON_MAX,
};

#endif
