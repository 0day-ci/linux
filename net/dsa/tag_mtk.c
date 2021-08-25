// SPDX-License-Identifier: GPL-2.0
/*
 * Mediatek DSA Tag support
 * Copyright (C) 2017 Landen Chao <landen.chao@mediatek.com>
 *		      Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>

#include "dsa_priv.h"

#define MTK_HDR_LEN		4
#define MTK_HDR_XMIT_UNTAGGED		0
#define MTK_HDR_XMIT_TAGGED_TPID_8100	1
#define MTK_HDR_XMIT_TAGGED_TPID_88A8	2
#define MTK_HDR_RECV_SOURCE_PORT_MASK	GENMASK(2, 0)
#define MTK_HDR_XMIT_DP_BIT_MASK	GENMASK(5, 0)
#define MTK_HDR_XMIT_SA_DIS		BIT(6)

static struct sk_buff *mtk_tag_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	u8 *mtk_tag;

	/* Build the special tag after the MAC Source Address. If VLAN header
	 * is present, it's required that VLAN header and special tag is
	 * being combined. Only in this way we can allow the switch can parse
	 * the both special and VLAN tag at the same time and then look up VLAN
	 * table with VID.
	 */
	skb_push(skb, MTK_HDR_LEN);
	dsa_alloc_etype_header(skb, MTK_HDR_LEN);
	mtk_tag = dsa_etype_header_pos_tx(skb);

	if (skb_vlan_tag_present(skb)) {
		switch (skb->vlan_proto) {
		case htons(ETH_P_8021Q):
			mtk_tag[0] = MTK_HDR_XMIT_TAGGED_TPID_8100;
			break;
		case htons(ETH_P_8021AD):
			mtk_tag[0] = MTK_HDR_XMIT_TAGGED_TPID_88A8;
			break;
		default:
			return NULL;
		}

		((__be16 *)mtk_tag)[1] = htons(skb_vlan_tag_get(skb));
		__vlan_hwaccel_clear_tag(skb);
	} else {
		mtk_tag[0] = MTK_HDR_XMIT_UNTAGGED;
		((__be16 *)mtk_tag)[1] = 0;
	}

	mtk_tag[1] = (1 << dp->index) & MTK_HDR_XMIT_DP_BIT_MASK;

	return skb;
}

static struct sk_buff *mtk_tag_rcv(struct sk_buff *skb, struct net_device *dev)
{
	u16 hdr;
	int port;
	__be16 *phdr;

	if (unlikely(!pskb_may_pull(skb, MTK_HDR_LEN)))
		return NULL;

	phdr = dsa_etype_header_pos_rx(skb);
	hdr = ntohs(*phdr);

	/* Remove MTK tag and recalculate checksum. */
	skb_pull_rcsum(skb, MTK_HDR_LEN);

	dsa_strip_etype_header(skb, MTK_HDR_LEN);

	/* Get source port information */
	port = (hdr & MTK_HDR_RECV_SOURCE_PORT_MASK);

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static const struct dsa_device_ops mtk_netdev_ops = {
	.name		= "mtk",
	.proto		= DSA_TAG_PROTO_MTK,
	.xmit		= mtk_tag_xmit,
	.rcv		= mtk_tag_rcv,
	.needed_headroom = MTK_HDR_LEN,
	.features	= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_STAG_TX,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_MTK);

module_dsa_tag_driver(mtk_netdev_ops);
