/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CONNTRACK_PROTO_ESP_H
#define _CONNTRACK_PROTO_ESP_H
#include <asm/byteorder.h>

/* ESP PROTOCOL HEADER */

struct esphdr {
	__u32 spi;
};

struct nf_ct_esp {
	unsigned int stream_timeout;
	unsigned int timeout;
};

#ifdef __KERNEL__
#include <net/netfilter/nf_conntrack_tuple.h>

void destroy_esp_conntrack_entry(struct nf_conn *ct);

bool esp_pkt_to_tuple(const struct sk_buff *skb, unsigned int dataoff,
		      struct net *net, struct nf_conntrack_tuple *tuple);
#endif /* __KERNEL__ */
#endif /* _CONNTRACK_PROTO_ESP_H */
