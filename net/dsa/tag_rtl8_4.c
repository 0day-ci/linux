// SPDX-License-Identifier: GPL-2.0
/*
 * Handler for Realtek 8 byte switch tags
 *
 * Copyright (C) 2021 Alvin Šipraga <alsi@bang-olufsen.dk>
 *
 * NOTE: Currently only supports protocol "4" found in the RTL8365MB, hence
 * named tag_rtl8_4.
 *
 * This "proprietary tag" header has the following format:
 *
 *  -------------------------------------------
 *  | MAC DA | MAC SA | 8 byte tag | Type | ...
 *  -------------------------------------------
 *     _______________/            \______________________________________
 *    /                                                                   \
 *  0                                  7|8                                 15
 *  |-----------------------------------+-----------------------------------|---
 *  |                               (16-bit)                                | ^
 *  |                       Realtek EtherType [0x8899]                      | |
 *  |-----------------------------------+-----------------------------------| 8
 *  |              (8-bit)              |              (8-bit)              |
 *  |          Protocol [0x04]          |              REASON               | b
 *  |-----------------------------------+-----------------------------------| y
 *  |   (1)  | (1) | (2) |   (1)  | (3) | (1)  | (1) |    (1)    |   (5)    | t
 *  | FID_EN |  X  | FID | PRI_EN | PRI | KEEP |  X  | LEARN_DIS |    X     | e
 *  |-----------------------------------+-----------------------------------| s
 *  |   (1)  |                       (15-bit)                               | |
 *  |  ALLOW |                        TX/RX                                 | v
 *  |-----------------------------------+-----------------------------------|---
 *
 * With the following field descriptions:
 *
 *    field      | description
 *   ------------+-------------
 *    Realtek    | 0x8899: indicates that this is a proprietary Realtek tag;
 *     EtherType |         note that Realtek uses the same EtherType for
 *               |         other incompatible tag formats (e.g. tag_rtl4_a.c)
 *    Protocol   | 0x04: indicates that this tag conforms to this format
 *    X          | reserved
 *   ------------+-------------
 *    REASON     | reason for forwarding packet to CPU
 *    FID_EN     | 1: packet has an FID
 *               | 0: no FID
 *    FID        | FID of packet (if FID_EN=1)
 *    PRI_EN     | 1: force priority of packet
 *               | 0: don't force priority
 *    PRI        | priority of packet (if PRI_EN=1)
 *    KEEP       | preserve packet VLAN tag format
 *    LEARN_DIS  | don't learn the source MAC address of the packet
 *    ALLOW      | 1: treat TX/RX field as an allowance port mask, meaning the
 *               |    packet may only be forwarded to ports specified in the
 *               |    mask
 *               | 0: no allowance port mask, TX/RX field is the forwarding
 *               |    port mask
 *    TX/RX      | TX (switch->CPU): port number the packet was received on
 *               | RX (CPU->switch): forwarding port mask (if ALLOW=0)
 *               |                   allowance port mask (if ALLOW=1)
 */

#include <linux/etherdevice.h>
#include <linux/bits.h>

#include "dsa_priv.h"

#define RTL8_4_TAG_LEN			8
#define RTL8_4_ETHERTYPE		0x8899
/* 0x04 = RTL8365MB DSA protocol
 */
#define RTL8_4_PROTOCOL_RTL8365MB	0x04

static struct sk_buff *rtl8_4_tag_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	__be16 *p;
	u8 *tag;
	u16 out;

	/* Pad out so that the (stripped) packet is at least 64 bytes long
	 * (including FCS), otherwise the switch will drop the packet.
	 * Then we need an additional 8 bytes for the Realtek tag.
	 */
	if (__skb_put_padto(skb, ETH_ZLEN + RTL8_4_TAG_LEN, false))
		return NULL;

	skb_push(skb, RTL8_4_TAG_LEN);

	dsa_alloc_etype_header(skb, RTL8_4_TAG_LEN);
	tag = dsa_etype_header_pos_tx(skb);

	/* Set Realtek EtherType */
	p = (__be16 *)tag;
	*p = htons(RTL8_4_ETHERTYPE);

	/* Set Protocol; zero REASON */
	p = (__be16 *)(tag + 2);
	*p = htons(RTL8_4_PROTOCOL_RTL8365MB << 8);

	/* Zero FID_EN, FID, PRI_EN, PRI, KEEP, LEARN_DIS */
	p = (__be16 *)(tag + 4);
	*p = 0;

	/* Zero ALLOW; set RX (CPU->switch) forwarding port mask */
	p = (__be16 *)(tag + 6);
	out = BIT(dp->index);
	*p = htons(~(1 << 15) & BIT(dp->index));

	return skb;
}

static struct sk_buff *rtl8_4_tag_rcv(struct sk_buff *skb,
				      struct net_device *dev)
{
	__be16 *p;
	u16 etype;
	u8 proto;
	u8 *tag;
	u8 port;
	u16 tmp;

	if (unlikely(!pskb_may_pull(skb, RTL8_4_TAG_LEN)))
		return NULL;

	tag = dsa_etype_header_pos_rx(skb);

	/* Parse Realtek EtherType */
	p = (__be16 *)tag;
	etype = ntohs(*p);
	if (unlikely(etype != RTL8_4_ETHERTYPE)) {
		netdev_dbg(dev, "non-realtek ethertype 0x%04x\n", etype);
		return NULL;
	}

	/* Parse Protocol; ignore REASON */
	p = (__be16 *)(tag + 2);
	tmp = ntohs(*p);
	proto = tmp >> 8;
	if (unlikely(proto != RTL8_4_PROTOCOL_RTL8365MB)) {
		netdev_dbg(dev, "unknown realtek protocol 0x%02x\n", proto);
		return NULL;
	}

	/* Ignore FID_EN, FID, PRI_EN, PRI, KEEP, LEARN_DIS */
	p = (__be16 *)(tag + 4);

	/* Ignore ALLOW; parse TX (switch->CPU) */
	p = (__be16 *)(tag + 6);
	tmp = ntohs(*p);
	port = tmp & 0xf; /* Port number is the LSB 4 bits */

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev) {
		netdev_dbg(dev, "could not find slave for port %d\n", port);
		return NULL;
	}

	/* Remove tag and recalculate checksum */
	skb_pull_rcsum(skb, RTL8_4_TAG_LEN);

	dsa_strip_etype_header(skb, RTL8_4_TAG_LEN);

	skb->offload_fwd_mark = 1;

	return skb;
}

static const struct dsa_device_ops rtl8_4_netdev_ops = {
	.name = "rtl8_4",
	.proto = DSA_TAG_PROTO_RTL8_4,
	.xmit = rtl8_4_tag_xmit,
	.rcv = rtl8_4_tag_rcv,
	.needed_headroom = RTL8_4_TAG_LEN,
};
module_dsa_tag_driver(rtl8_4_netdev_ops);

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_RTL8_4);
