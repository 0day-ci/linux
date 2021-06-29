// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/crc32.h>
#include "rxe.h"
#include "rxe_loc.h"

/**
 * rxe_icrc_init - Initialize crypto function for computing crc32
 * @rxe: rdma_rxe device object
 *
 * Returns 0 on success else an error
 */
int rxe_icrc_init(struct rxe_dev *rxe)
{
	struct crypto_shash *tfm;

	tfm = crypto_alloc_shash("crc32", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("failed to init crc32 algorithm err:%ld\n",
			       PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	rxe->tfm = tfm;

	return 0;
}

/**
 * rxe_crc32 - Compute cumulative crc32 for a contiguous segment
 * @rxe: rdma_rxe device object
 * @crc: starting crc32 value from previous segments
 * @addr: starting address of segment
 * @len: length of the segment in bytes
 *
 * Returns the crc32 cumulative checksum including the segment starting
 * from crc.
 */
static __be32 rxe_crc32(struct rxe_dev *rxe, __be32 crc, void *addr,
			size_t len)
{
	__be32 icrc;
	int err;

	SHASH_DESC_ON_STACK(shash, rxe->tfm);

	shash->tfm = rxe->tfm;
	*(__be32 *)shash_desc_ctx(shash) = crc;
	err = crypto_shash_update(shash, addr, len);
	if (unlikely(err)) {
		pr_warn_ratelimited("failed crc calculation, err: %d\n", err);
		return crc32_le(crc, addr, len);
	}

	icrc = *(__be32 *)shash_desc_ctx(shash);
	barrier_data(shash_desc_ctx(shash));

	return icrc;
}

/**
 * rxe_icrc_hdr - Compute a partial ICRC for the IB transport headers.
 * @pkt: packet information
 * @skb: packet buffer
 *
 * Returns the partial ICRC
 */
static u32 rxe_icrc_hdr(struct rxe_pkt_info *pkt, struct sk_buff *skb)
{
	struct udphdr *udph;
	struct rxe_bth *bth;
	__be32 crc;
	int length;
	int hdr_size = sizeof(struct udphdr) +
		(skb->protocol == htons(ETH_P_IP) ?
		sizeof(struct iphdr) : sizeof(struct ipv6hdr));
	/* pseudo header buffer size is calculate using ipv6 header size since
	 * it is bigger than ipv4
	 */
	u8 pshdr[sizeof(struct udphdr) +
		sizeof(struct ipv6hdr) +
		RXE_BTH_BYTES];

	/* This seed is the result of computing a CRC with a seed of
	 * 0xfffffff and 8 bytes of 0xff representing a masked LRH.
	 */
	crc = 0xdebb20e3;

	if (skb->protocol == htons(ETH_P_IP)) { /* IPv4 */
		struct iphdr *ip4h = NULL;

		memcpy(pshdr, ip_hdr(skb), hdr_size);
		ip4h = (struct iphdr *)pshdr;
		udph = (struct udphdr *)(ip4h + 1);

		ip4h->ttl = 0xff;
		ip4h->check = CSUM_MANGLED_0;
		ip4h->tos = 0xff;
	} else {				/* IPv6 */
		struct ipv6hdr *ip6h = NULL;

		memcpy(pshdr, ipv6_hdr(skb), hdr_size);
		ip6h = (struct ipv6hdr *)pshdr;
		udph = (struct udphdr *)(ip6h + 1);

		memset(ip6h->flow_lbl, 0xff, sizeof(ip6h->flow_lbl));
		ip6h->priority = 0xf;
		ip6h->hop_limit = 0xff;
	}

	bth = (struct rxe_bth *)(udph + 1);
	memcpy(bth, pkt->hdr, RXE_BTH_BYTES);

	/* exclude bth.resv8a */
	bth->qpn |= cpu_to_be32(~BTH_QPN_MASK);

	length = hdr_size + RXE_BTH_BYTES;
	crc = rxe_crc32(pkt->rxe, crc, pshdr, length);

	/* And finish to compute the CRC on the remainder of the headers. */
	crc = rxe_crc32(pkt->rxe, crc, pkt->hdr + RXE_BTH_BYTES,
			rxe_opcode[pkt->opcode].length - RXE_BTH_BYTES);
	return crc;
}

/**
 * rxe_icrc_check - Compute ICRC for a packet and compare to the ICRC
 *		    delivered in the packet.
 * @skb: packet buffer with packet info in skb->cb[] (receive path)
 *
 * Returns 0 if the ICRCs match or an error on failure
 */
int rxe_icrc_check(struct sk_buff *skb)
{
	struct rxe_pkt_info *pkt = SKB_TO_PKT(skb);
	__be32 *icrcp;
	__be32 packet_icrc;
	__be32 computed_icrc;

	icrcp = (__be32 *)(pkt->hdr + pkt->paylen - RXE_ICRC_SIZE);
	packet_icrc = *icrcp;

	computed_icrc = rxe_icrc_hdr(pkt, skb);
	computed_icrc = rxe_crc32(pkt->rxe, computed_icrc,
		(u8 *)payload_addr(pkt), payload_size(pkt) + bth_pad(pkt));
	computed_icrc = ~computed_icrc;

	if (unlikely(computed_icrc != packet_icrc)) {
		if (skb->protocol == htons(ETH_P_IPV6))
			pr_warn_ratelimited("bad ICRC from %pI6c\n",
					    &ipv6_hdr(skb)->saddr);
		else if (skb->protocol == htons(ETH_P_IP))
			pr_warn_ratelimited("bad ICRC from %pI4\n",
					    &ip_hdr(skb)->saddr);
		else
			pr_warn_ratelimited("bad ICRC from unknown\n");

		return -EINVAL;
	}

	return 0;
}

/**
 * rxe_icrc_generate - Compute ICRC for a packet.
 * @pkt: packet information
 * @skb: packet buffer
 */
void rxe_icrc_generate(struct rxe_pkt_info *pkt, struct sk_buff *skb)
{
	__be32 *icrcp;
	__be32 icrc;

	icrcp = (__be32 *)(pkt->hdr + pkt->paylen - RXE_ICRC_SIZE);
	icrc = rxe_icrc_hdr(pkt, skb);
	icrc = rxe_crc32(pkt->rxe, icrc, (u8 *)payload_addr(pkt),
				payload_size(pkt) + bth_pad(pkt));
	*icrcp = ~icrc;
}
