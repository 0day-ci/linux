/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IF_LINK_H
#define _LINUX_IF_LINK_H

#include <uapi/linux/if_link.h>

/* We don't want these structures exposed to user space */

struct ifla_vf_stats {
	__u64 rx_packets;
	__u64 tx_packets;
	__u64 rx_bytes;
	__u64 tx_bytes;
	__u64 broadcast;
	__u64 multicast;
	__u64 rx_dropped;
	__u64 tx_dropped;
};

struct ifla_vf_info {
	__u32 vf;
	__u8 mac[32];
	__u32 vlan;
	__u32 qos;
	__u32 spoofchk;
	__u32 linkstate;
	__u32 min_tx_rate;
	__u32 max_tx_rate;
	__u32 rss_query_en;
	__u32 trusted;
	__be16 vlan_proto;
};

/**
 * struct ifla_xdp_stats - driver-side XDP statistics
 * @packets: number of frames passed to bpf_prog_run_xdp().
 * @bytes: number of bytes went through bpf_prog_run_xdp().
 * @errors: number of general XDP errors, if driver has one unified counter.
 * @aborted: number of %XDP_ABORTED returns.
 * @drop: number of %XDP_DROP returns.
 * @invalid: number of returns of unallowed values (i.e. not XDP_*).
 * @pass: number of %XDP_PASS returns.
 * @redirect: number of successfully performed %XDP_REDIRECT requests.
 * @redirect_errors: number of failed %XDP_REDIRECT requests.
 * @tx: number of successfully performed %XDP_TX requests.
 * @tx_errors: number of failed %XDP_TX requests.
 * @xmit_packets: number of successfully transmitted XDP/XSK frames.
 * @xmit_bytes: number of successfully transmitted XDP/XSK frames.
 * @xmit_errors: of XDP/XSK frames failed to transmit.
 * @xmit_full: number of XDP/XSK queue being full at the moment of transmission.
 */
struct ifla_xdp_stats {
	__u64	packets;
	__u64	bytes;
	__u64	errors;
	__u64	aborted;
	__u64	drop;
	__u64	invalid;
	__u64	pass;
	__u64	redirect;
	__u64	redirect_errors;
	__u64	tx;
	__u64	tx_errors;
	__u64	xmit_packets;
	__u64	xmit_bytes;
	__u64	xmit_errors;
	__u64	xmit_full;
};

#endif /* _LINUX_IF_LINK_H */
