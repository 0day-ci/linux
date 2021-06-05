// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>

#include "dsa_priv.h"

#define QCA_HDR_LEN	2
#define QCA_HDR_VERSION	0x2

#define QCA_HDR_RECV_VERSION_MASK	GENMASK(15, 14)
#define QCA_HDR_RECV_VERSION_S		14
#define QCA_HDR_RECV_PRIORITY_MASK	GENMASK(13, 11)
#define QCA_HDR_RECV_PRIORITY_S		11
#define QCA_HDR_RECV_TYPE_MASK		GENMASK(10, 6)
#define QCA_HDR_RECV_TYPE_S		6
#define QCA_HDR_RECV_FRAME_IS_TAGGED	BIT(3)
#define QCA_HDR_RECV_SOURCE_PORT_MASK	GENMASK(2, 0)

#define QCA_HDR_XMIT_VERSION_MASK	GENMASK(15, 14)
#define QCA_HDR_XMIT_VERSION_S		14
#define QCA_HDR_XMIT_PRIORITY_MASK	GENMASK(13, 11)
#define QCA_HDR_XMIT_PRIORITY_S		11
#define QCA_HDR_XMIT_CONTROL_MASK	GENMASK(10, 8)
#define QCA_HDR_XMIT_CONTROL_S		8
#define QCA_HDR_XMIT_FROM_CPU		BIT(7)
#define QCA_HDR_XMIT_DP_BIT_MASK	GENMASK(6, 0)

static struct sk_buff *qca_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	__be16 *phdr;
	u16 hdr;

	skb_push(skb, QCA_HDR_LEN);

	memmove(skb->data, skb->data + QCA_HDR_LEN, 2 * ETH_ALEN);
	phdr = (__be16 *)(skb->data + 2 * ETH_ALEN);

	/* Set the version field, and set destination port information */
	hdr = QCA_HDR_VERSION << QCA_HDR_XMIT_VERSION_S |
		QCA_HDR_XMIT_FROM_CPU | BIT(dp->index);

	*phdr = htons(hdr);

	return skb;
}

static struct sk_buff *qca_tag_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt)
{
	u8 ver;
	u16 hdr, vlan_hdr;
	int port, vlan_offset = 0, vlan_skip = 0;
	__be16 *phdr, *vlan_phdr;

	if (unlikely(!pskb_may_pull(skb, QCA_HDR_LEN)))
		return NULL;

	/* The QCA header is added by the switch between src addr and
	 * Ethertype. Normally at this point, skb->data points to ethertype so the
	 * header should be right before. However if a VLAN tag has subsequently
	 * been added upstream, we need to skip past it to find the QCA header.
	 */
	vlan_phdr = (__be16 *)(skb->data - 2);
	vlan_hdr = ntohs(*vlan_phdr);

	/* Check for VLAN tag before QCA tag */
	if (!(vlan_hdr ^ ETH_P_8021Q))
		vlan_offset = VLAN_HLEN;

	/* Look for QCA tag at the correct location */
	phdr = (__be16 *)(skb->data - 2 + vlan_offset);
	hdr = ntohs(*phdr);

	/* Make sure the version is correct */
	ver = (hdr & QCA_HDR_RECV_VERSION_MASK) >> QCA_HDR_RECV_VERSION_S;
	if (unlikely(ver != QCA_HDR_VERSION))
		return NULL;

	/* Check for second VLAN tag after QCA tag if one was found prior */
	if (!!(vlan_offset)) {
		vlan_phdr = (__be16 *)(skb->data + 4);
		vlan_hdr = ntohs(*vlan_phdr);
		if (!!(vlan_hdr ^ ETH_P_8021Q)) {
		/* Do not remove existing tag in case a tag is required */
			vlan_offset = 0;
			vlan_skip = VLAN_HLEN;
		}
	}

	/* Remove QCA tag and recalculate checksum */
	skb_pull_rcsum(skb, QCA_HDR_LEN + vlan_offset);
	memmove(skb->data - ETH_HLEN,
		skb->data - ETH_HLEN - QCA_HDR_LEN - vlan_offset,
		ETH_HLEN - QCA_HDR_LEN + vlan_skip);

	/* Get source port information */
	port = (hdr & QCA_HDR_RECV_SOURCE_PORT_MASK);

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	return skb;
}

static const struct dsa_device_ops qca_netdev_ops = {
	.name	= "qca",
	.proto	= DSA_TAG_PROTO_QCA,
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
	.overhead = QCA_HDR_LEN,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_QCA);

module_dsa_tag_driver(qca_netdev_ops);
