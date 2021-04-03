// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */


#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/igmp.h>
#include <net/addrconf.h>

#include "dsa_priv.h"

#define AR9331_HDR_LEN			2
#define AR9331_HDR_VERSION		1

#define AR9331_HDR_VERSION_MASK		GENMASK(15, 14)
#define AR9331_HDR_PRIORITY_MASK	GENMASK(13, 12)
#define AR9331_HDR_TYPE_MASK		GENMASK(10, 8)
#define AR9331_HDR_BROADCAST		BIT(7)
#define AR9331_HDR_FROM_CPU		BIT(6)
/* AR9331_HDR_RESERVED - not used or may be version field.
 * According to the AR8216 doc it should 0b10. On AR9331 it is 0b11 on RX path
 * and should be set to 0b11 to make it work.
 */
#define AR9331_HDR_RESERVED_MASK	GENMASK(5, 4)
#define AR9331_HDR_PORT_NUM_MASK	GENMASK(3, 0)

/*
 * This code replicated MLD detection more or less in the same way as the
 * switch is doing it
 */
static int ipv6_mc_check_ip6hdr(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h;
	unsigned int offset;

	offset = skb_network_offset(skb) + sizeof(*ip6h);

	if (!pskb_may_pull(skb, offset))
		return -EINVAL;

	ip6h = ipv6_hdr(skb);

	if (ip6h->version != 6)
		return -EINVAL;

	skb_set_transport_header(skb, offset);

	return 0;
}

static int ipv6_mc_check_exthdrs(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h;
	int offset;
	u8 nexthdr;
	__be16 frag_off;

	ip6h = ipv6_hdr(skb);

	if (ip6h->nexthdr != IPPROTO_HOPOPTS)
		return -ENOMSG;

	nexthdr = ip6h->nexthdr;
	offset = skb_network_offset(skb) + sizeof(*ip6h);
	offset = ipv6_skip_exthdr(skb, offset, &nexthdr, &frag_off);

	if (offset < 0)
		return -EINVAL;

	if (nexthdr != IPPROTO_ICMPV6)
		return -ENOMSG;

	skb_set_transport_header(skb, offset);

	return 0;
}

static int my_ipv6_mc_check_mld(struct sk_buff *skb)
{
	int ret;

	ret = ipv6_mc_check_ip6hdr(skb);
	if (ret < 0)
		return ret;

	return ipv6_mc_check_exthdrs(skb);
}


static struct sk_buff *ar9331_tag_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	__le16 *phdr;
	u16 hdr;

	if (dp->stp_state == BR_STATE_BLOCKING) {
		/* TODO: should we reflect it in the stats? */
		netdev_warn_once(dev, "%s:%i dropping blocking packet\n",
				 __func__, __LINE__);
		return NULL;
	}

	phdr = skb_push(skb, AR9331_HDR_LEN);

	hdr = FIELD_PREP(AR9331_HDR_VERSION_MASK, AR9331_HDR_VERSION);
	hdr |= AR9331_HDR_FROM_CPU | dp->index;
	/* 0b10 for AR8216 and 0b11 for AR9331 */
	hdr |= AR9331_HDR_RESERVED_MASK;

	phdr[0] = cpu_to_le16(hdr);

	return skb;
}

static struct sk_buff *ar9331_tag_rcv(struct sk_buff *skb,
				      struct net_device *ndev,
				      struct packet_type *pt)
{
	u8 ver, port;
	u16 hdr;

	if (unlikely(!pskb_may_pull(skb, AR9331_HDR_LEN)))
		return NULL;

	hdr = le16_to_cpu(*(__le16 *)skb_mac_header(skb));

	ver = FIELD_GET(AR9331_HDR_VERSION_MASK, hdr);
	if (unlikely(ver != AR9331_HDR_VERSION)) {
		netdev_warn_once(ndev, "%s:%i wrong header version 0x%2x\n",
				 __func__, __LINE__, hdr);
		return NULL;
	}

	if (unlikely(hdr & AR9331_HDR_FROM_CPU)) {
		netdev_warn_once(ndev, "%s:%i packet should not be from cpu 0x%2x\n",
				 __func__, __LINE__, hdr);
		return NULL;
	}

	skb_pull_rcsum(skb, AR9331_HDR_LEN);

	/* Get source port information */
	port = FIELD_GET(AR9331_HDR_PORT_NUM_MASK, hdr);

	skb->dev = dsa_master_find_slave(ndev, 0, port);
	if (!skb->dev)
		return NULL;

	return skb;
}

static void ar9331_tag_rcv_post(struct sk_buff *skb)
{
	const struct iphdr *iph;
	unsigned char *dest;
	int ret;

	/*
	 * Since the switch do not tell us which packets was offloaded we assume
	 * that all of them did. Except:
	 * - port is not configured for forwarding to any other ports
	 * - igmp/mld snooping is enabled
	 * - unicast or multicast flood is disabled on some of bridged ports
	 * - if we have two port bridge and one is not in forwarding state.
	 * - packet was dropped on the output port..
	 * - any other reasons?
	 */
	skb->offload_fwd_mark = true;

	dest = eth_hdr(skb)->h_dest;
	/*
	 * Complete not multicast traffic seems to be forwarded automatically,
	 * as long as multicast and unicast flood are enabled
	 */
	if (!(is_multicast_ether_addr(dest) && !is_broadcast_ether_addr(dest)))
		return;


	/*
	 * Documentation do not providing any usable information on how the
	 * igmp/mld snooping is implemented on this switch. Following
	 * implementation is based on testing, by sending correct and malformed
	 * packets to the switch.
	 * It is not trying to find sane and properly formated packets. Instead
	 * it is trying to be as close to the switch behavior as possible.
	 */
	skb_reset_network_header(skb);
	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:

		if (!pskb_network_may_pull(skb, sizeof(*iph)))
			break;

		iph = ip_hdr(skb);
		if (iph->protocol == IPPROTO_IGMP)
			skb->offload_fwd_mark = false;

		break;
	case ETH_P_IPV6:
		ret = my_ipv6_mc_check_mld(skb);
		if (!ret)
			skb->offload_fwd_mark = false;

		break;
	}
}


static const struct dsa_device_ops ar9331_netdev_ops = {
	.name	= "ar9331",
	.proto	= DSA_TAG_PROTO_AR9331,
	.xmit	= ar9331_tag_xmit,
	.rcv	= ar9331_tag_rcv,
	.rcv_post = ar9331_tag_rcv_post,
	.overhead = AR9331_HDR_LEN,
};

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_AR9331);
module_dsa_tag_driver(ar9331_netdev_ops);
