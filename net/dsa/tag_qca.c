// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include <linux/dsa/tag_qca.h>

#include "dsa_priv.h"

static struct sk_buff *qca_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	__be16 *phdr;
	u16 hdr;

	skb_push(skb, QCA_HDR_LEN);

	dsa_alloc_etype_header(skb, QCA_HDR_LEN);
	phdr = dsa_etype_header_pos_tx(skb);

	/* Set the version field, and set destination port information */
	hdr = FIELD_PREP(QCA_HDR_XMIT_VERSION, QCA_HDR_VERSION);
	hdr |= QCA_HDR_XMIT_FROM_CPU;
	hdr |= FIELD_PREP(QCA_HDR_XMIT_DP_BIT, BIT(dp->index));

	*phdr = htons(hdr);

	return skb;
}

static struct sk_buff *qca_tag_rcv(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dev->dsa_ptr;
	struct qca8k_port_tag *header = dp->priv;
	u16  hdr, pk_type;
	__be16 *phdr;
	int port;
	u8 ver;

	if (unlikely(!pskb_may_pull(skb, QCA_HDR_LEN)))
		return NULL;

	phdr = dsa_etype_header_pos_rx(skb);
	hdr = ntohs(*phdr);

	/* Make sure the version is correct */
	ver = FIELD_GET(QCA_HDR_RECV_VERSION, hdr);
	if (unlikely(ver != QCA_HDR_VERSION))
		return NULL;

	/* Get pk type */
	pk_type = FIELD_GET(QCA_HDR_RECV_TYPE, hdr);

	/* Ethernet MDIO read/write packet */
	if (pk_type == QCA_HDR_RECV_TYPE_RW_REG_ACK) {
		if (header->rw_reg_ack_handler)
			header->rw_reg_ack_handler(dp, skb);
		return NULL;
	}

	/* Ethernet MIB counter packet */
	if (pk_type == QCA_HDR_RECV_TYPE_MIB) {
		if (header->mib_autocast_handler)
			header->mib_autocast_handler(dp, skb);
		return NULL;
	}

	/* Remove QCA tag and recalculate checksum */
	skb_pull_rcsum(skb, QCA_HDR_LEN);
	dsa_strip_etype_header(skb, QCA_HDR_LEN);

	/* Get source port information */
	port = FIELD_GET(QCA_HDR_RECV_SOURCE_PORT, hdr);

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	return skb;
}

static int qca_tag_connect(struct dsa_switch_tree *dst)
{
	struct qca8k_port_tag *header;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (!dsa_port_is_cpu(dp))
			continue;

		header = kzalloc(sizeof(*header), GFP_KERNEL);
		if (!header)
			return -ENOMEM;

		dp->priv = header;
	}

	return 0;
}

static void qca_tag_disconnect(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (!dsa_port_is_cpu(dp))
			continue;

		kfree(dp->priv);
	}
}

static const struct dsa_device_ops qca_netdev_ops = {
	.name	= "qca",
	.proto	= DSA_TAG_PROTO_QCA,
	.connect = qca_tag_connect,
	.disconnect = qca_tag_disconnect,
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
	.needed_headroom = QCA_HDR_LEN,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_QCA);

module_dsa_tag_driver(qca_netdev_ops);
