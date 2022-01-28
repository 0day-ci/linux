// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "iecm.h"

const struct iecm_rx_ptype_decoded iecm_ptype_lookup[IECM_RX_MAX_PTYPE] = {
	/* ptype indices are dynamic and package dependent. Indices represented
	 * in this lookup table are for reference and will be replaced by the
	 * values which CP sends. Also these values are static for older
	 * versions of virtchnl and if VIRTCHNL2_CAP_PTYPE is not set in
	 * virtchnl2_get_capabilities.
	 */
	/* L2 Packet types */
	IECM_PTT_UNUSED_ENTRY(0),
	IECM_PTT(1,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	IECM_PTT(2,  L2, NONE, NOF, NONE, NONE, NOF, TS,   PAY2),
	IECM_PTT(3,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	IECM_PTT_UNUSED_ENTRY(4),
	IECM_PTT_UNUSED_ENTRY(5),
	IECM_PTT(6,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	IECM_PTT(7,  L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	IECM_PTT_UNUSED_ENTRY(8),
	IECM_PTT_UNUSED_ENTRY(9),
	IECM_PTT(10, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY2),
	IECM_PTT(11, L2, NONE, NOF, NONE, NONE, NOF, NONE, NONE),
	IECM_PTT(12, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(13, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(14, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(15, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(16, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(17, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(18, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(19, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(20, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(21, L2, NONE, NOF, NONE, NONE, NOF, NONE, PAY3),

	/* Non Tunneled IPv4 */
	IECM_PTT(22, IP, IPV4, FRG, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(23, IP, IPV4, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(24, IP, IPV4, NOF, NONE, NONE, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(25),
	IECM_PTT(26, IP, IPV4, NOF, NONE, NONE, NOF, TCP,  PAY4),
	IECM_PTT(27, IP, IPV4, NOF, NONE, NONE, NOF, SCTP, PAY4),
	IECM_PTT(28, IP, IPV4, NOF, NONE, NONE, NOF, ICMP, PAY4),

	/* IPv4 --> IPv4 */
	IECM_PTT(29, IP, IPV4, NOF, IP_IP, IPV4, FRG, NONE, PAY3),
	IECM_PTT(30, IP, IPV4, NOF, IP_IP, IPV4, NOF, NONE, PAY3),
	IECM_PTT(31, IP, IPV4, NOF, IP_IP, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(32),
	IECM_PTT(33, IP, IPV4, NOF, IP_IP, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(34, IP, IPV4, NOF, IP_IP, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(35, IP, IPV4, NOF, IP_IP, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> IPv6 */
	IECM_PTT(36, IP, IPV4, NOF, IP_IP, IPV6, FRG, NONE, PAY3),
	IECM_PTT(37, IP, IPV4, NOF, IP_IP, IPV6, NOF, NONE, PAY3),
	IECM_PTT(38, IP, IPV4, NOF, IP_IP, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(39),
	IECM_PTT(40, IP, IPV4, NOF, IP_IP, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(41, IP, IPV4, NOF, IP_IP, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(42, IP, IPV4, NOF, IP_IP, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT */
	IECM_PTT(43, IP, IPV4, NOF, IP_GRENAT, NONE, NOF, NONE, PAY3),

	/* IPv4 --> GRE/NAT --> IPv4 */
	IECM_PTT(44, IP, IPV4, NOF, IP_GRENAT, IPV4, FRG, NONE, PAY3),
	IECM_PTT(45, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, NONE, PAY3),
	IECM_PTT(46, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(47),
	IECM_PTT(48, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(49, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(50, IP, IPV4, NOF, IP_GRENAT, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> IPv6 */
	IECM_PTT(51, IP, IPV4, NOF, IP_GRENAT, IPV6, FRG, NONE, PAY3),
	IECM_PTT(52, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, NONE, PAY3),
	IECM_PTT(53, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(54),
	IECM_PTT(55, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(56, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(57, IP, IPV4, NOF, IP_GRENAT, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC */
	IECM_PTT(58, IP, IPV4, NOF, IP_GRENAT_MAC, NONE, NOF, NONE, PAY3),

	/* IPv4 --> GRE/NAT --> MAC --> IPv4 */
	IECM_PTT(59, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, FRG, NONE, PAY3),
	IECM_PTT(60, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, NONE, PAY3),
	IECM_PTT(61, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(62),
	IECM_PTT(63, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(64, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(65, IP, IPV4, NOF, IP_GRENAT_MAC, IPV4, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT -> MAC --> IPv6 */
	IECM_PTT(66, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, FRG, NONE, PAY3),
	IECM_PTT(67, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, NONE, PAY3),
	IECM_PTT(68, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(69),
	IECM_PTT(70, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(71, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(72, IP, IPV4, NOF, IP_GRENAT_MAC, IPV6, NOF, ICMP, PAY4),

	/* IPv4 --> GRE/NAT --> MAC/VLAN */
	IECM_PTT(73, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, NONE, NOF, NONE, PAY3),

	/* IPv4 ---> GRE/NAT -> MAC/VLAN --> IPv4 */
	IECM_PTT(74, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, FRG, NONE, PAY3),
	IECM_PTT(75, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, NONE, PAY3),
	IECM_PTT(76, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(77),
	IECM_PTT(78, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(79, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(80, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, ICMP, PAY4),

	/* IPv4 -> GRE/NAT -> MAC/VLAN --> IPv6 */
	IECM_PTT(81, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, FRG, NONE, PAY3),
	IECM_PTT(82, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, NONE, PAY3),
	IECM_PTT(83, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(84),
	IECM_PTT(85, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(86, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(87, IP, IPV4, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, ICMP, PAY4),

	/* Non Tunneled IPv6 */
	IECM_PTT(88, IP, IPV6, FRG, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(89, IP, IPV6, NOF, NONE, NONE, NOF, NONE, PAY3),
	IECM_PTT(90, IP, IPV6, NOF, NONE, NONE, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(91),
	IECM_PTT(92, IP, IPV6, NOF, NONE, NONE, NOF, TCP,  PAY4),
	IECM_PTT(93, IP, IPV6, NOF, NONE, NONE, NOF, SCTP, PAY4),
	IECM_PTT(94, IP, IPV6, NOF, NONE, NONE, NOF, ICMP, PAY4),

	/* IPv6 --> IPv4 */
	IECM_PTT(95,  IP, IPV6, NOF, IP_IP, IPV4, FRG, NONE, PAY3),
	IECM_PTT(96,  IP, IPV6, NOF, IP_IP, IPV4, NOF, NONE, PAY3),
	IECM_PTT(97,  IP, IPV6, NOF, IP_IP, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(98),
	IECM_PTT(99,  IP, IPV6, NOF, IP_IP, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(100, IP, IPV6, NOF, IP_IP, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(101, IP, IPV6, NOF, IP_IP, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> IPv6 */
	IECM_PTT(102, IP, IPV6, NOF, IP_IP, IPV6, FRG, NONE, PAY3),
	IECM_PTT(103, IP, IPV6, NOF, IP_IP, IPV6, NOF, NONE, PAY3),
	IECM_PTT(104, IP, IPV6, NOF, IP_IP, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(105),
	IECM_PTT(106, IP, IPV6, NOF, IP_IP, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(107, IP, IPV6, NOF, IP_IP, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(108, IP, IPV6, NOF, IP_IP, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT */
	IECM_PTT(109, IP, IPV6, NOF, IP_GRENAT, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> IPv4 */
	IECM_PTT(110, IP, IPV6, NOF, IP_GRENAT, IPV4, FRG, NONE, PAY3),
	IECM_PTT(111, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, NONE, PAY3),
	IECM_PTT(112, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(113),
	IECM_PTT(114, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(115, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(116, IP, IPV6, NOF, IP_GRENAT, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> IPv6 */
	IECM_PTT(117, IP, IPV6, NOF, IP_GRENAT, IPV6, FRG, NONE, PAY3),
	IECM_PTT(118, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, NONE, PAY3),
	IECM_PTT(119, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(120),
	IECM_PTT(121, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(122, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(123, IP, IPV6, NOF, IP_GRENAT, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC */
	IECM_PTT(124, IP, IPV6, NOF, IP_GRENAT_MAC, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> MAC -> IPv4 */
	IECM_PTT(125, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, FRG, NONE, PAY3),
	IECM_PTT(126, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, NONE, PAY3),
	IECM_PTT(127, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(128),
	IECM_PTT(129, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(130, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(131, IP, IPV6, NOF, IP_GRENAT_MAC, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC -> IPv6 */
	IECM_PTT(132, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, FRG, NONE, PAY3),
	IECM_PTT(133, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, NONE, PAY3),
	IECM_PTT(134, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(135),
	IECM_PTT(136, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(137, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(138, IP, IPV6, NOF, IP_GRENAT_MAC, IPV6, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC/VLAN */
	IECM_PTT(139, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, NONE, NOF, NONE, PAY3),

	/* IPv6 --> GRE/NAT -> MAC/VLAN --> IPv4 */
	IECM_PTT(140, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, FRG, NONE, PAY3),
	IECM_PTT(141, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, NONE, PAY3),
	IECM_PTT(142, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(143),
	IECM_PTT(144, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, TCP,  PAY4),
	IECM_PTT(145, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, SCTP, PAY4),
	IECM_PTT(146, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV4, NOF, ICMP, PAY4),

	/* IPv6 --> GRE/NAT -> MAC/VLAN --> IPv6 */
	IECM_PTT(147, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, FRG, NONE, PAY3),
	IECM_PTT(148, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, NONE, PAY3),
	IECM_PTT(149, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, UDP,  PAY4),
	IECM_PTT_UNUSED_ENTRY(150),
	IECM_PTT(151, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, TCP,  PAY4),
	IECM_PTT(152, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, SCTP, PAY4),
	IECM_PTT(153, IP, IPV6, NOF, IP_GRENAT_MAC_VLAN, IPV6, NOF, ICMP, PAY4),

	/* rest of the entries are unused */
};
EXPORT_SYMBOL(iecm_ptype_lookup);

/**
 * iecm_tx_buf_rel - Release a Tx buffer
 * @tx_q: the queue that owns the buffer
 * @tx_buf: the buffer to free
 */
void iecm_tx_buf_rel(struct iecm_queue *tx_q, struct iecm_tx_buf *tx_buf)
{
	if (tx_buf->skb) {
		if (dma_unmap_len(tx_buf, len))
			dma_unmap_single(tx_q->dev,
					 dma_unmap_addr(tx_buf, dma),
					 dma_unmap_len(tx_buf, len),
					 DMA_TO_DEVICE);
		dev_kfree_skb_any(tx_buf->skb);
	} else if (dma_unmap_len(tx_buf, len)) {
		dma_unmap_page(tx_q->dev,
			       dma_unmap_addr(tx_buf, dma),
			       dma_unmap_len(tx_buf, len),
			       DMA_TO_DEVICE);
	}

	tx_buf->next_to_watch = NULL;
	tx_buf->skb = NULL;
	dma_unmap_len_set(tx_buf, len, 0);
}

/**
 * iecm_tx_buf_rel_all - Free any empty Tx buffers
 * @txq: queue to be cleaned
 */
static void iecm_tx_buf_rel_all(struct iecm_queue *txq)
{
	u16 i;

	/* Buffers already cleared, nothing to do */
	if (!txq->tx_buf)
		return;

	/* Free all the Tx buffer sk_buffs */
	for (i = 0; i < txq->desc_count; i++)
		iecm_tx_buf_rel(txq, &txq->tx_buf[i]);

	kfree(txq->tx_buf);
	txq->tx_buf = NULL;

	if (txq->buf_stack.bufs) {
		for (i = 0; i < txq->buf_stack.size; i++) {
			iecm_tx_buf_rel(txq, txq->buf_stack.bufs[i]);
			kfree(txq->buf_stack.bufs[i]);
		}
		kfree(txq->buf_stack.bufs);
		txq->buf_stack.bufs = NULL;
	}
}

/**
 * iecm_tx_desc_rel - Free Tx resources per queue
 * @txq: Tx descriptor ring for a specific queue
 * @bufq: buffer q or completion q
 *
 * Free all transmit software resources
 */
static void iecm_tx_desc_rel(struct iecm_queue *txq, bool bufq)
{
	if (bufq) {
		iecm_tx_buf_rel_all(txq);
		netdev_tx_reset_queue(netdev_get_tx_queue(txq->vport->netdev,
							  txq->idx));
	}

	if (txq->desc_ring) {
		dmam_free_coherent(txq->dev, txq->size,
				   txq->desc_ring, txq->dma);
		txq->desc_ring = NULL;
		txq->next_to_alloc = 0;
		txq->next_to_use = 0;
		txq->next_to_clean = 0;
	}
}

/**
 * iecm_tx_desc_rel_all - Free Tx Resources for All Queues
 * @vport: virtual port structure
 *
 * Free all transmit software resources
 */
static void iecm_tx_desc_rel_all(struct iecm_vport *vport)
{
	int i, j;

	if (!vport->txq_grps)
		return;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *txq_grp = &vport->txq_grps[i];

		for (j = 0; j < txq_grp->num_txq; j++)
			iecm_tx_desc_rel(txq_grp->txqs[j], true);
		if (iecm_is_queue_model_split(vport->txq_model))
			iecm_tx_desc_rel(txq_grp->complq, false);
	}
}

/**
 * iecm_tx_buf_alloc_all - Allocate memory for all buffer resources
 * @tx_q: queue for which the buffers are allocated
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_tx_buf_alloc_all(struct iecm_queue *tx_q)
{
	int buf_size;
	int i = 0;

	/* Allocate book keeping buffers only. Buffers to be supplied to HW
	 * are allocated by kernel network stack and received as part of skb
	 */
	buf_size = sizeof(struct iecm_tx_buf) * tx_q->desc_count;
	tx_q->tx_buf = kzalloc(buf_size, GFP_KERNEL);
	if (!tx_q->tx_buf)
		return -ENOMEM;

	/* Initialize tx buf stack for out-of-order completions if
	 * flow scheduling offload is enabled
	 */
	tx_q->buf_stack.bufs =
		kcalloc(tx_q->desc_count, sizeof(struct iecm_tx_buf *),
			GFP_KERNEL);
	if (!tx_q->buf_stack.bufs)
		return -ENOMEM;

	tx_q->buf_stack.size = tx_q->desc_count;
	tx_q->buf_stack.top = tx_q->desc_count;

	for (i = 0; i < tx_q->desc_count; i++) {
		tx_q->buf_stack.bufs[i] = kzalloc(sizeof(*tx_q->buf_stack.bufs[i]),
						  GFP_KERNEL);
		if (!tx_q->buf_stack.bufs[i])
			return -ENOMEM;
	}

	return 0;
}

/**
 * iecm_tx_desc_alloc - Allocate the Tx descriptors
 * @tx_q: the tx ring to set up
 * @bufq: buffer or completion queue
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_tx_desc_alloc(struct iecm_queue *tx_q, bool bufq)
{
	struct device *dev = tx_q->dev;
	int err = 0;

	if (bufq) {
		err = iecm_tx_buf_alloc_all(tx_q);
		if (err)
			goto err_alloc;
		tx_q->size = tx_q->desc_count *
				sizeof(struct iecm_base_tx_desc);
	} else {
		tx_q->size = tx_q->desc_count *
				sizeof(struct iecm_splitq_tx_compl_desc);
	}

	/* Allocate descriptors also round up to nearest 4K */
	tx_q->size = ALIGN(tx_q->size, 4096);
	tx_q->desc_ring = dmam_alloc_coherent(dev, tx_q->size, &tx_q->dma,
					      GFP_KERNEL);
	if (!tx_q->desc_ring) {
		dev_info(dev, "Unable to allocate memory for the Tx descriptor ring, size=%d\n",
			 tx_q->size);
		err = -ENOMEM;
		goto err_alloc;
	}

	tx_q->next_to_alloc = 0;
	tx_q->next_to_use = 0;
	tx_q->next_to_clean = 0;
	set_bit(__IECM_Q_GEN_CHK, tx_q->flags);

err_alloc:
	if (err)
		iecm_tx_desc_rel(tx_q, bufq);
	return err;
}

/**
 * iecm_tx_desc_alloc_all - allocate all queues Tx resources
 * @vport: virtual port private structure
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_tx_desc_alloc_all(struct iecm_vport *vport)
{
	struct pci_dev *pdev = vport->adapter->pdev;
	int err = 0;
	int i, j;

	/* Setup buffer queues. In single queue model buffer queues and
	 * completion queues will be same
	 */
	for (i = 0; i < vport->num_txq_grp; i++) {
		for (j = 0; j < vport->txq_grps[i].num_txq; j++) {
			err = iecm_tx_desc_alloc(vport->txq_grps[i].txqs[j],
						 true);
			if (err) {
				dev_err(&pdev->dev,
					"Allocation for Tx Queue %u failed\n",
					i);
				goto err_out;
			}
		}

		if (iecm_is_queue_model_split(vport->txq_model)) {
			/* Setup completion queues */
			err = iecm_tx_desc_alloc(vport->txq_grps[i].complq,
						 false);
			if (err) {
				dev_err(&pdev->dev,
					"Allocation for Tx Completion Queue %u failed\n",
					i);
				goto err_out;
			}
		}
	}
err_out:
	if (err)
		iecm_tx_desc_rel_all(vport);
	return err;
}

/**
 * iecm_rx_page_rel - Release an rx buffer page
 * @rxq: the queue that owns the buffer
 * @page_info: pointer to page metadata of page to be freed
 */
static void iecm_rx_page_rel(struct iecm_queue *rxq,
			     struct iecm_page_info *page_info)
{
	if (!page_info->page)
		return;

	/* free resources associated with mapping */
	dma_unmap_page_attrs(rxq->dev, page_info->dma, PAGE_SIZE,
			     DMA_FROM_DEVICE, IECM_RX_DMA_ATTR);

	__page_frag_cache_drain(page_info->page, page_info->pagecnt_bias);

	page_info->page = NULL;
	page_info->page_offset = 0;
}

/**
 * iecm_rx_buf_rel - Release a rx buffer
 * @rxq: the queue that owns the buffer
 * @rx_buf: the buffer to free
 */
static void iecm_rx_buf_rel(struct iecm_queue *rxq,
			    struct iecm_rx_buf *rx_buf)
{
	iecm_rx_page_rel(rxq, &rx_buf->page_info[0]);
#if (PAGE_SIZE < 8192)
	if (rx_buf->buf_size > IECM_RX_BUF_2048)
		iecm_rx_page_rel(rxq, &rx_buf->page_info[1]);

#endif
	if (rx_buf->skb) {
		dev_kfree_skb_any(rx_buf->skb);
		rx_buf->skb = NULL;
	}
}

/**
 * iecm_rx_hdr_buf_rel_all - Release header buffer memory
 * @rxq: queue to use
 */
static void iecm_rx_hdr_buf_rel_all(struct iecm_queue *rxq)
{
	struct iecm_hw *hw = &rxq->vport->adapter->hw;
	int i;

	if (!rxq)
		return;

	if (rxq->rx_buf.hdr_buf) {
		for (i = 0; i < rxq->desc_count; i++) {
			struct iecm_dma_mem *hbuf = rxq->rx_buf.hdr_buf[i];

			if (hbuf) {
				iecm_free_dma_mem(hw, hbuf);
				kfree(hbuf);
			}
			rxq->rx_buf.hdr_buf[i] = NULL;
		}
		kfree(rxq->rx_buf.hdr_buf);
		rxq->rx_buf.hdr_buf = NULL;
	}

	for (i = 0; i < rxq->hbuf_pages.nr_pages; i++)
		iecm_rx_page_rel(rxq, &rxq->hbuf_pages.pages[i]);

	kfree(rxq->hbuf_pages.pages);
}

/**
 * iecm_rx_buf_rel_all - Free all Rx buffer resources for a queue
 * @rxq: queue to be cleaned
 */
static void iecm_rx_buf_rel_all(struct iecm_queue *rxq)
{
	u16 i;

	/* queue already cleared, nothing to do */
	if (!rxq->rx_buf.buf)
		return;

	/* Free all the bufs allocated and given to hw on Rx queue */
	for (i = 0; i < rxq->desc_count; i++)
		iecm_rx_buf_rel(rxq, &rxq->rx_buf.buf[i]);
	if (rxq->rx_hsplit_en)
		iecm_rx_hdr_buf_rel_all(rxq);

	kfree(rxq->rx_buf.buf);
	rxq->rx_buf.buf = NULL;
	kfree(rxq->rx_buf.hdr_buf);
	rxq->rx_buf.hdr_buf = NULL;
}

/**
 * iecm_rx_desc_rel - Free a specific Rx q resources
 * @rxq: queue to clean the resources from
 * @bufq: buffer q or completion q
 * @q_model: single or split q model
 *
 * Free a specific rx queue resources
 */
static void iecm_rx_desc_rel(struct iecm_queue *rxq, bool bufq, s32 q_model)
{
	if (!rxq)
		return;

	if (!bufq && iecm_is_queue_model_split(q_model) && rxq->skb) {
		dev_kfree_skb_any(rxq->skb);
		rxq->skb = NULL;
	}

	if (bufq || !iecm_is_queue_model_split(q_model))
		iecm_rx_buf_rel_all(rxq);

	if (rxq->desc_ring) {
		dmam_free_coherent(rxq->dev, rxq->size,
				   rxq->desc_ring, rxq->dma);
		rxq->desc_ring = NULL;
		rxq->next_to_alloc = 0;
		rxq->next_to_clean = 0;
		rxq->next_to_use = 0;
	}
}

/**
 * iecm_rx_desc_rel_all - Free Rx Resources for All Queues
 * @vport: virtual port structure
 *
 * Free all rx queues resources
 */
static void iecm_rx_desc_rel_all(struct iecm_vport *vport)
{
	struct iecm_rxq_group *rx_qgrp;
	struct iecm_queue *q;
	int i, j, num_rxq;

	if (!vport->rxq_grps)
		return;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
			for (j = 0; j < num_rxq; j++) {
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
				iecm_rx_desc_rel(q, false,
						 vport->rxq_model);
			}

			if (!rx_qgrp->splitq.bufq_sets)
				continue;
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				struct iecm_bufq_set *bufq_set =
					&rx_qgrp->splitq.bufq_sets[j];

				q = &bufq_set->bufq;
				iecm_rx_desc_rel(q, true, vport->rxq_model);
			}
		} else {
			for (j = 0; j < rx_qgrp->singleq.num_rxq; j++) {
				q = rx_qgrp->singleq.rxqs[j];
				iecm_rx_desc_rel(q, false,
						 vport->rxq_model);
			}
		}
	}
}

/**
 * iecm_rx_buf_hw_update - Store the new tail and head values
 * @rxq: queue to bump
 * @val: new head index
 */
void iecm_rx_buf_hw_update(struct iecm_queue *rxq, u32 val)
{
	rxq->next_to_use = val;

	if (unlikely(!rxq->tail))
		return;
	/* writel has an implicit memory barrier */
	writel(val, rxq->tail);
}

/**
 * iecm_alloc_page - allocate page to back RX buffer
 * @rxbufq: pointer to queue struct
 * @page_info: pointer to page metadata struct
 */
static int
iecm_alloc_page(struct iecm_queue *rxbufq, struct iecm_page_info *page_info)
{
	/* alloc new page for storage */
	page_info->page = alloc_page(GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!page_info->page))
		return -ENOMEM;

	/* map page for use */
	page_info->dma = dma_map_page_attrs(rxbufq->dev, page_info->page,
					    0, PAGE_SIZE, DMA_FROM_DEVICE,
					    IECM_RX_DMA_ATTR);

	/* if mapping failed free memory back to system since
	 * there isn't much point in holding memory we can't use
	 */
	if (dma_mapping_error(rxbufq->dev, page_info->dma)) {
		__free_pages(page_info->page, 0);
		return -ENOMEM;
	}

	page_info->page_offset = 0;

	/* initialize pagecnt_bias to claim we fully own page */
	page_ref_add(page_info->page, USHRT_MAX - 1);
	page_info->pagecnt_bias = USHRT_MAX;

	return 0;
}

/**
 * iecm_init_rx_buf_hw_alloc - allocate initial RX buffer pages
 * @rxbufq: ring to use; equivalent to rxq when operating in singleq mode
 * @buf: rx_buffer struct to modify
 *
 * Returns true if the page was successfully allocated or
 * reused.
 */
bool iecm_init_rx_buf_hw_alloc(struct iecm_queue *rxbufq, struct iecm_rx_buf *buf)
{
	if (iecm_alloc_page(rxbufq, &buf->page_info[0]))
		return false;

#if (PAGE_SIZE < 8192)
	if (rxbufq->rx_buf_size > IECM_RX_BUF_2048)
		if (iecm_alloc_page(rxbufq, &buf->page_info[1]))
			return false;
#endif

	buf->page_indx = 0;
	buf->buf_size = rxbufq->rx_buf_size;

	return true;
}

/**
 * iecm_rx_hdr_buf_alloc_all - Allocate memory for header buffers
 * @rxq: ring to use
 *
 * Returns 0 on success, negative on failure.
 */
static int iecm_rx_hdr_buf_alloc_all(struct iecm_queue *rxq)
{
	struct iecm_page_info *page_info;
	int nr_pages, offset;
	int i, j = 0;

	rxq->rx_buf.hdr_buf = kcalloc(rxq->desc_count,
				      sizeof(struct iecm_dma_mem *),
				      GFP_KERNEL);
	if (!rxq->rx_buf.hdr_buf)
		return -ENOMEM;

	for (i = 0; i < rxq->desc_count; i++) {
		rxq->rx_buf.hdr_buf[i] = kcalloc(1,
						 sizeof(struct iecm_dma_mem),
						 GFP_KERNEL);
		if (!rxq->rx_buf.hdr_buf[i])
			goto unroll_buf_alloc;
	}

	/* Determine the number of pages necessary to back the total number of header buffers */
	nr_pages = (rxq->desc_count * rxq->rx_hbuf_size) / PAGE_SIZE;
	rxq->hbuf_pages.pages = kcalloc(nr_pages,
					sizeof(struct iecm_page_info),
					GFP_KERNEL);
	if (!rxq->hbuf_pages.pages)
		goto unroll_buf_alloc;

	rxq->hbuf_pages.nr_pages = nr_pages;
	for (i = 0; i < nr_pages; i++) {
		if (iecm_alloc_page(rxq, &rxq->hbuf_pages.pages[i]))
			goto unroll_buf_alloc;
	}

	page_info = &rxq->hbuf_pages.pages[0];
	for (i = 0, offset = 0; i < rxq->desc_count; i++, offset += rxq->rx_hbuf_size) {
		struct iecm_dma_mem *hbuf = rxq->rx_buf.hdr_buf[i];

		/* Move to next page */
		if (offset >= PAGE_SIZE) {
			offset = 0;
			page_info = &rxq->hbuf_pages.pages[++j];
		}

		hbuf->va = page_address(page_info->page) + offset;
		hbuf->pa = page_info->dma + offset;
		hbuf->size = rxq->rx_hbuf_size;
	}

	return 0;
unroll_buf_alloc:
	iecm_rx_hdr_buf_rel_all(rxq);
	return -ENOMEM;
}

/**
 * iecm_rx_buf_hw_alloc_all - Allocate receive buffers
 * @rxbufq: queue for which the hw buffers are allocated; equivalent to rxq
 * when operating in singleq mode
 * @alloc_count: number of buffers to allocate
 *
 * Returns false if all allocations were successful, true if any fail
 */
static bool
iecm_rx_buf_hw_alloc_all(struct iecm_queue *rxbufq, u16 alloc_count)
{
	u16 nta = rxbufq->next_to_alloc;
	struct iecm_rx_buf *buf;

	if (!alloc_count)
		return false;

	buf = &rxbufq->rx_buf.buf[nta];

	do {
		if (!iecm_init_rx_buf_hw_alloc(rxbufq, buf))
			break;

		buf++;
		nta++;
		if (unlikely(nta == rxbufq->desc_count)) {
			buf = rxbufq->rx_buf.buf;
			nta = 0;
		}

		alloc_count--;
	} while (alloc_count);

	return !!alloc_count;
}

/**
 * iecm_rx_post_buf_desc - Post buffer to bufq descriptor ring
 * @bufq: buffer queue to post to
 * @buf_id: buffer id to post
 */
static void iecm_rx_post_buf_desc(struct iecm_queue *bufq, u16 buf_id)
{
	struct virtchnl2_splitq_rx_buf_desc *splitq_rx_desc = NULL;
	struct iecm_page_info *page_info;
	u16 nta = bufq->next_to_alloc;
	struct iecm_rx_buf *buf;

	splitq_rx_desc = IECM_SPLITQ_RX_BUF_DESC(bufq, nta);
	buf = &bufq->rx_buf.buf[buf_id];
	page_info = &buf->page_info[buf->page_indx];
	if (bufq->rx_hsplit_en)
		splitq_rx_desc->hdr_addr = cpu_to_le64(bufq->rx_buf.hdr_buf[buf_id]->pa);

	splitq_rx_desc->pkt_addr = cpu_to_le64(page_info->dma +
					       page_info->page_offset);
	splitq_rx_desc->qword0.buf_id = cpu_to_le16(buf_id);

	nta++;
	if (unlikely(nta == bufq->desc_count))
		nta = 0;
	bufq->next_to_alloc = nta;
}

/**
 * iecm_rx_post_init_bufs - Post initial buffers to bufq
 * @bufq: buffer queue to post working set to
 * @working_set: number of buffers to put in working set
 */
static void iecm_rx_post_init_bufs(struct iecm_queue *bufq,
				   u16 working_set)
{
	int i;

	for (i = 0; i < working_set; i++)
		iecm_rx_post_buf_desc(bufq, i);

	iecm_rx_buf_hw_update(bufq, bufq->next_to_alloc & ~(bufq->rx_buf_stride - 1));
}

/**
 * iecm_rx_buf_alloc_all - Allocate memory for all buffer resources
 * @rxbufq: queue for which the buffers are allocated; equivalent to
 * rxq when operating in singleq mode
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_rx_buf_alloc_all(struct iecm_queue *rxbufq)
{
	int err = 0;

	/* Allocate book keeping buffers */
	rxbufq->rx_buf.buf = kcalloc(rxbufq->desc_count,
				     sizeof(struct iecm_rx_buf), GFP_KERNEL);
	if (!rxbufq->rx_buf.buf) {
		err = -ENOMEM;
		goto rx_buf_alloc_all_out;
	}

	if (rxbufq->rx_hsplit_en) {
		err = iecm_rx_hdr_buf_alloc_all(rxbufq);
		if (err)
			goto rx_buf_alloc_all_out;
	}

	/* Allocate buffers to be given to HW.	 */
	if (iecm_is_queue_model_split(rxbufq->vport->rxq_model)) {
		if (iecm_rx_buf_hw_alloc_all(rxbufq, rxbufq->desc_count - 1))
			err = -ENOMEM;
	} else {
		if (iecm_rx_singleq_buf_hw_alloc_all(rxbufq, rxbufq->desc_count - 1))
			err = -ENOMEM;
	}

rx_buf_alloc_all_out:
	if (err)
		iecm_rx_buf_rel_all(rxbufq);
	return err;
}

/**
 * iecm_rx_desc_alloc - Allocate queue Rx resources
 * @rxq: Rx queue for which the resources are setup
 * @bufq: buffer or completion queue
 * @q_model: single or split queue model
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_rx_desc_alloc(struct iecm_queue *rxq, bool bufq, s32 q_model)
{
	struct device *dev = rxq->dev;

	/* As both single and split descriptors are 32 byte, memory size
	 * will be same for all three singleq_base rx, buf., splitq_base
	 * rx. So pick anyone of them for size
	 */
	if (bufq) {
		rxq->size = rxq->desc_count *
			sizeof(struct virtchnl2_splitq_rx_buf_desc);
	} else {
		rxq->size = rxq->desc_count *
			sizeof(union virtchnl2_rx_desc);
	}

	/* Allocate descriptors and also round up to nearest 4K */
	rxq->size = ALIGN(rxq->size, 4096);
	rxq->desc_ring = dmam_alloc_coherent(dev, rxq->size,
					     &rxq->dma, GFP_KERNEL);
	if (!rxq->desc_ring) {
		dev_info(dev, "Unable to allocate memory for the Rx descriptor ring, size=%d\n",
			 rxq->size);
		return -ENOMEM;
	}

	rxq->next_to_alloc = 0;
	rxq->next_to_clean = 0;
	rxq->next_to_use = 0;
	set_bit(__IECM_Q_GEN_CHK, rxq->flags);

	/* Allocate buffers for a rx queue if the q_model is single OR if it
	 * is a buffer queue in split queue model
	 */
	if (bufq || !iecm_is_queue_model_split(q_model)) {
		int err = 0;

		err = iecm_rx_buf_alloc_all(rxq);
		if (err) {
			iecm_rx_desc_rel(rxq, bufq, q_model);
			return err;
		}
	}
	return 0;
}

/**
 * iecm_rx_desc_alloc_all - allocate all RX queues resources
 * @vport: virtual port structure
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_rx_desc_alloc_all(struct iecm_vport *vport)
{
	struct device *dev = &vport->adapter->pdev->dev;
	struct iecm_rxq_group *rx_qgrp;
	int i, j, num_rxq, working_set;
	struct iecm_queue *q;
	int err = 0;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];
		if (iecm_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			if (iecm_is_queue_model_split(vport->rxq_model))
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];
			err = iecm_rx_desc_alloc(q, false, vport->rxq_model);
			if (err) {
				dev_err(dev, "Memory allocation for Rx Queue %u failed\n",
					i);
				goto err_out;
			}
		}

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				err = iecm_rx_desc_alloc(q, true,
							 vport->rxq_model);
				if (err) {
					dev_err(dev, "Memory allocation for Rx Buffer Queue %u failed\n",
						i);
					goto err_out;
				}

				working_set = IECM_RX_BUFQ_WORKING_SET(q);
				iecm_rx_post_init_bufs(q, working_set);
			}
		}
	}
err_out:
	if (err)
		iecm_rx_desc_rel_all(vport);
	return err;
}

/**
 * iecm_txq_group_rel - Release all resources for txq groups
 * @vport: vport to release txq groups on
 */
static void iecm_txq_group_rel(struct iecm_vport *vport)
{
	struct iecm_txq_group *txq_grp;
	int i, j, num_txq;

	if (vport->txq_grps) {
		for (i = 0; i < vport->num_txq_grp; i++) {
			txq_grp = &vport->txq_grps[i];
			num_txq = txq_grp->num_txq;

			for (j = 0; j < num_txq; j++) {
				kfree(txq_grp->txqs[j]);
				txq_grp->txqs[j] = NULL;
			}
			kfree(txq_grp->complq);
			txq_grp->complq = NULL;
		}
		kfree(vport->txq_grps);
		vport->txq_grps = NULL;
	}
}

/**
 * iecm_rxq_sw_queue_rel - Release software queue resources
 * @rx_qgrp: rx queue group with software queues
 */
static void iecm_rxq_sw_queue_rel(struct iecm_rxq_group *rx_qgrp)
{
	int i, j;

	for (i = 0; i < rx_qgrp->vport->num_bufqs_per_qgrp; i++) {
		struct iecm_bufq_set *bufq_set = &rx_qgrp->splitq.bufq_sets[i];

		for (j = 0; j < bufq_set->num_refillqs; j++) {
			kfree(bufq_set->refillqs[j].ring);
			bufq_set->refillqs[j].ring = NULL;
		}
		kfree(bufq_set->refillqs);
		bufq_set->refillqs = NULL;
	}
}

/**
 * iecm_rxq_group_rel - Release all resources for rxq groups
 * @vport: vport to release rxq groups on
 */
static void iecm_rxq_group_rel(struct iecm_vport *vport)
{
	if (vport->rxq_grps) {
		int i;

		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			int j, num_rxq;

			if (iecm_is_queue_model_split(vport->rxq_model)) {
				num_rxq = rx_qgrp->splitq.num_rxq_sets;
				for (j = 0; j < num_rxq; j++) {
					kfree(rx_qgrp->splitq.rxq_sets[j]);
					rx_qgrp->splitq.rxq_sets[j] = NULL;
				}
				iecm_rxq_sw_queue_rel(rx_qgrp);
				kfree(rx_qgrp->splitq.bufq_sets);
				rx_qgrp->splitq.bufq_sets = NULL;
			} else {
				num_rxq = rx_qgrp->singleq.num_rxq;
				for (j = 0; j < num_rxq; j++) {
					kfree(rx_qgrp->singleq.rxqs[j]);
					rx_qgrp->singleq.rxqs[j] = NULL;
				}
			}
		}
		kfree(vport->rxq_grps);
		vport->rxq_grps = NULL;
	}
}

/**
 * iecm_vport_queue_grp_rel_all - Release all queue groups
 * @vport: vport to release queue groups for
 */
static void iecm_vport_queue_grp_rel_all(struct iecm_vport *vport)
{
	iecm_txq_group_rel(vport);
	iecm_rxq_group_rel(vport);
}

/**
 * iecm_vport_queues_rel - Free memory for all queues
 * @vport: virtual port
 *
 * Free the memory allocated for queues associated to a vport
 */
void iecm_vport_queues_rel(struct iecm_vport *vport)
{
	iecm_tx_desc_rel_all(vport);
	iecm_rx_desc_rel_all(vport);
	iecm_vport_queue_grp_rel_all(vport);

	kfree(vport->txqs);
	vport->txqs = NULL;
}

/**
 * iecm_vport_init_fast_path_txqs - Initialize fast path txq array
 * @vport: vport to init txqs on
 *
 * We get a queue index from skb->queue_mapping and we need a fast way to
 * dereference the queue from queue groups.  This allows us to quickly pull a
 * txq based on a queue index.
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_vport_init_fast_path_txqs(struct iecm_vport *vport)
{
	int i, j, k = 0;

	vport->txqs = kcalloc(vport->num_txq, sizeof(struct iecm_queue *),
			      GFP_KERNEL);

	if (!vport->txqs)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *tx_grp = &vport->txq_grps[i];

		for (j = 0; j < tx_grp->num_txq; j++, k++) {
			vport->txqs[k] = tx_grp->txqs[j];
			vport->txqs[k]->idx = k;
		}
	}
	return 0;
}

/**
 * iecm_vport_init_num_qs - Initialize number of queues
 * @vport: vport to initialize queues
 * @vport_msg: data to be filled into vport
 */
void iecm_vport_init_num_qs(struct iecm_vport *vport, struct virtchnl2_create_vport *vport_msg)
{
	vport->num_txq = le16_to_cpu(vport_msg->num_tx_q);
	vport->num_rxq = le16_to_cpu(vport_msg->num_rx_q);
	/* number of txqs and rxqs in config data will be zeros only in the
	 * driver load path and we dont update them there after
	 */
	if (!vport->adapter->config_data.num_req_tx_qs &&
	    !vport->adapter->config_data.num_req_rx_qs) {
		vport->adapter->config_data.num_req_tx_qs =
					le16_to_cpu(vport_msg->num_tx_q);
		vport->adapter->config_data.num_req_rx_qs =
					le16_to_cpu(vport_msg->num_rx_q);
	}

	if (iecm_is_queue_model_split(vport->txq_model))
		vport->num_complq = le16_to_cpu(vport_msg->num_tx_complq);
	if (iecm_is_queue_model_split(vport->rxq_model))
		vport->num_bufq = le16_to_cpu(vport_msg->num_rx_bufq);
}

/**
 * iecm_vport_calc_num_q_desc - Calculate number of queue groups
 * @vport: vport to calculate q groups for
 */
void iecm_vport_calc_num_q_desc(struct iecm_vport *vport)
{
	int num_req_txq_desc = vport->adapter->config_data.num_req_txq_desc;
	int num_req_rxq_desc = vport->adapter->config_data.num_req_rxq_desc;
	int num_bufqs = vport->num_bufqs_per_qgrp;
	int i = 0;

	vport->complq_desc_count = 0;
	if (num_req_txq_desc) {
		vport->txq_desc_count = num_req_txq_desc;
		if (iecm_is_queue_model_split(vport->txq_model)) {
			vport->complq_desc_count = num_req_txq_desc;
			if (vport->complq_desc_count < IECM_MIN_TXQ_COMPLQ_DESC)
				vport->complq_desc_count =
					IECM_MIN_TXQ_COMPLQ_DESC;
		}
	} else {
		vport->txq_desc_count =
			IECM_DFLT_TX_Q_DESC_COUNT;
		if (iecm_is_queue_model_split(vport->txq_model)) {
			vport->complq_desc_count =
				IECM_DFLT_TX_COMPLQ_DESC_COUNT;
		}
	}

	if (num_req_rxq_desc)
		vport->rxq_desc_count = num_req_rxq_desc;
	else
		vport->rxq_desc_count = IECM_DFLT_RX_Q_DESC_COUNT;

	for (i = 0; i < num_bufqs; i++) {
		if (!vport->bufq_desc_count[i])
			vport->bufq_desc_count[i] =
				IECM_RX_BUFQ_DESC_COUNT(vport->rxq_desc_count,
							num_bufqs);
	}
}
EXPORT_SYMBOL(iecm_vport_calc_num_q_desc);

/**
 * iecm_vport_calc_total_qs - Calculate total number of queues
 * @adapter: private data struct
 * @vport_msg: message to fill with data
 */
void iecm_vport_calc_total_qs(struct iecm_adapter *adapter,
			      struct virtchnl2_create_vport *vport_msg)
{
	unsigned int num_req_tx_qs = adapter->config_data.num_req_tx_qs;
	unsigned int num_req_rx_qs = adapter->config_data.num_req_rx_qs;
	int dflt_splitq_txq_grps, dflt_singleq_txqs;
	int dflt_splitq_rxq_grps, dflt_singleq_rxqs;
	int num_txq_grps, num_rxq_grps;
	int num_cpus;
	u16 max_q;

	/* Restrict num of queues to cpus online as a default configuration to
	 * give best performance. User can always override to a max number
	 * of queues via ethtool.
	 */
	num_cpus = num_online_cpus();
	max_q = adapter->max_queue_limit;

	dflt_splitq_txq_grps = min_t(int, max_q, num_cpus);
	dflt_singleq_txqs = min_t(int, max_q, num_cpus);
	dflt_splitq_rxq_grps = min_t(int, max_q, num_cpus);
	dflt_singleq_rxqs = min_t(int, max_q, num_cpus);

	if (iecm_is_queue_model_split(le16_to_cpu(vport_msg->txq_model))) {
		num_txq_grps = num_req_tx_qs ? num_req_tx_qs : dflt_splitq_txq_grps;
		vport_msg->num_tx_complq = cpu_to_le16(num_txq_grps *
						       IECM_COMPLQ_PER_GROUP);
		vport_msg->num_tx_q = cpu_to_le16(num_txq_grps *
						  IECM_DFLT_SPLITQ_TXQ_PER_GROUP);
	} else {
		num_txq_grps = IECM_DFLT_SINGLEQ_TX_Q_GROUPS;
		vport_msg->num_tx_q =
				cpu_to_le16(num_txq_grps *
					    (num_req_tx_qs ? num_req_tx_qs :
					    dflt_singleq_txqs));
		vport_msg->num_tx_complq = 0;
	}
	if (iecm_is_queue_model_split(le16_to_cpu(vport_msg->rxq_model))) {
		num_rxq_grps = num_req_rx_qs ? num_req_rx_qs : dflt_splitq_rxq_grps;
		vport_msg->num_rx_bufq =
					cpu_to_le16(num_rxq_grps *
						    IECM_MAX_BUFQS_PER_RXQ_GRP);

		vport_msg->num_rx_q = cpu_to_le16(num_rxq_grps *
						  IECM_DFLT_SPLITQ_RXQ_PER_GROUP);
	} else {
		num_rxq_grps = IECM_DFLT_SINGLEQ_RX_Q_GROUPS;
		vport_msg->num_rx_bufq = 0;
		vport_msg->num_rx_q =
				cpu_to_le16(num_rxq_grps *
					    (num_req_rx_qs ? num_req_rx_qs :
					    dflt_singleq_rxqs));
	}
}

/**
 * iecm_vport_calc_num_q_groups - Calculate number of queue groups
 * @vport: vport to calculate q groups for
 */
void iecm_vport_calc_num_q_groups(struct iecm_vport *vport)
{
	if (iecm_is_queue_model_split(vport->txq_model))
		vport->num_txq_grp = vport->num_txq;
	else
		vport->num_txq_grp = IECM_DFLT_SINGLEQ_TX_Q_GROUPS;

	if (iecm_is_queue_model_split(vport->rxq_model))
		vport->num_rxq_grp = vport->num_rxq;
	else
		vport->num_rxq_grp = IECM_DFLT_SINGLEQ_RX_Q_GROUPS;
}
EXPORT_SYMBOL(iecm_vport_calc_num_q_groups);

/**
 * iecm_vport_calc_numq_per_grp - Calculate number of queues per group
 * @vport: vport to calculate queues for
 * @num_txq: int return parameter
 * @num_rxq: int return parameter
 */
static void iecm_vport_calc_numq_per_grp(struct iecm_vport *vport,
					 int *num_txq, int *num_rxq)
{
	if (iecm_is_queue_model_split(vport->txq_model))
		*num_txq = IECM_DFLT_SPLITQ_TXQ_PER_GROUP;
	else
		*num_txq = vport->num_txq;

	if (iecm_is_queue_model_split(vport->rxq_model))
		*num_rxq = IECM_DFLT_SPLITQ_RXQ_PER_GROUP;
	else
		*num_rxq = vport->num_rxq;
}

/**
 * iecm_vport_calc_num_q_vec - Calculate total number of vectors required for
 * this vport
 * @vport: virtual port
 *
 */
void iecm_vport_calc_num_q_vec(struct iecm_vport *vport)
{
	if (iecm_is_queue_model_split(vport->txq_model))
		vport->num_q_vectors = vport->num_txq_grp;
	else
		vport->num_q_vectors = vport->num_txq;
}
EXPORT_SYMBOL(iecm_vport_calc_num_q_vec);

/**
 * iecm_rxq_set_descids - set the descids supported by this queue
 * @vport: virtual port data structure
 * @q: rx queue for which descids are set
 *
 */
static void iecm_rxq_set_descids(struct iecm_vport *vport, struct iecm_queue *q)
{
	if (vport->rxq_model == VIRTCHNL2_QUEUE_MODEL_SPLIT) {
		q->rxdids = VIRTCHNL2_RXDID_1_FLEX_SPLITQ_M;
	} else {
		if (vport->base_rxd)
			q->rxdids = VIRTCHNL2_RXDID_1_32B_BASE_M;
		else
			q->rxdids = VIRTCHNL2_RXDID_2_FLEX_SQ_NIC_M;
	}
}

/**
 * iecm_set_vlan_tag_loc - set the tag location for a tx/rx queue
 * @adapter: adapter structure
 * @q: tx/rx queue to set tag location for
 *
 */
static void iecm_set_vlan_tag_loc(struct iecm_adapter *adapter,
				  struct iecm_queue *q)
{
	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_VLAN)) {
		struct virtchnl_vlan_supported_caps *insertion_support;

		insertion_support =
				&adapter->vlan_caps->offloads.insertion_support;
		if (insertion_support->outer) {
			if (insertion_support->outer &
			    VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1)
				set_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG1,
					q->flags);
			else if (insertion_support->outer &
				 VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2)
				set_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG2,
					q->flags);
		} else if (insertion_support->inner) {
			if (insertion_support->inner &
			    VIRTCHNL_VLAN_TAG_LOCATION_L2TAG1)
				set_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG1,
					q->flags);
			else if (insertion_support->inner &
				 VIRTCHNL_VLAN_TAG_LOCATION_L2TAG2)
				set_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG2,
					q->flags);
		}
	} else if (iecm_is_cap_ena(adapter, IECM_BASE_CAPS,
				   VIRTCHNL2_CAP_VLAN)) {
		set_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG1, q->flags);
	}
}

/**
 * iecm_txq_group_alloc - Allocate all txq group resources
 * @vport: vport to allocate txq groups for
 * @num_txq: number of txqs to allocate for each group
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_txq_group_alloc(struct iecm_vport *vport, int num_txq)
{
	int err = 0, i;

	vport->txq_grps = kcalloc(vport->num_txq_grp,
				  sizeof(*vport->txq_grps), GFP_KERNEL);
	if (!vport->txq_grps)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];
		int j;

		tx_qgrp->vport = vport;
		tx_qgrp->num_txq = num_txq;

		for (j = 0; j < tx_qgrp->num_txq; j++) {
			tx_qgrp->txqs[j] = kzalloc(sizeof(*tx_qgrp->txqs[j]),
						   GFP_KERNEL);
			if (!tx_qgrp->txqs[j]) {
				err = -ENOMEM;
				goto err_alloc;
			}
		}

		for (j = 0; j < tx_qgrp->num_txq; j++) {
			struct iecm_queue *q = tx_qgrp->txqs[j];

			q->dev = &vport->adapter->pdev->dev;
			q->desc_count = vport->txq_desc_count;
			q->tx_max_bufs =
				vport->adapter->dev_ops.vc_ops.get_max_tx_bufs(vport->adapter);
			q->vport = vport;
			q->txq_grp = tx_qgrp;
			hash_init(q->sched_buf_hash);

			if (!iecm_is_cap_ena(vport->adapter,
					     IECM_OTHER_CAPS,
					     VIRTCHNL2_CAP_SPLITQ_QSCHED))
				set_bit(__IECM_Q_FLOW_SCH_EN, q->flags);
			iecm_set_vlan_tag_loc(vport->adapter, q);
		}

		if (!iecm_is_queue_model_split(vport->txq_model))
			continue;

		tx_qgrp->complq = kcalloc(IECM_COMPLQ_PER_GROUP,
					  sizeof(*tx_qgrp->complq),
					  GFP_KERNEL);
		if (!tx_qgrp->complq) {
			err = -ENOMEM;
			goto err_alloc;
		}

		tx_qgrp->complq->dev = &vport->adapter->pdev->dev;
		tx_qgrp->complq->desc_count = vport->complq_desc_count;
		tx_qgrp->complq->vport = vport;
		tx_qgrp->complq->txq_grp = tx_qgrp;
	}

err_alloc:
	if (err)
		iecm_txq_group_rel(vport);
	return err;
}

/**
 * iecm_rxq_group_alloc - Allocate all rxq group resources
 * @vport: vport to allocate rxq groups for
 * @num_rxq: number of rxqs to allocate for each group
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_rxq_group_alloc(struct iecm_vport *vport, int num_rxq)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_queue *q;
	int i, k, err = 0;

	vport->rxq_grps = kcalloc(vport->num_rxq_grp,
				  sizeof(struct iecm_rxq_group), GFP_KERNEL);
	if (!vport->rxq_grps)
		return -ENOMEM;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		int j;

		rx_qgrp->vport = vport;
		if (iecm_is_queue_model_split(vport->rxq_model)) {
			rx_qgrp->splitq.num_rxq_sets = num_rxq;

			for (j = 0; j < num_rxq; j++) {
				rx_qgrp->splitq.rxq_sets[j] =
					kzalloc(sizeof(struct iecm_rxq_set),
						GFP_KERNEL);
				if (!rx_qgrp->splitq.rxq_sets[j]) {
					err = -ENOMEM;
					goto err_alloc;
				}
			}

			rx_qgrp->splitq.bufq_sets = kcalloc(vport->num_bufqs_per_qgrp,
							    sizeof(struct iecm_bufq_set),
							    GFP_KERNEL);
			if (!rx_qgrp->splitq.bufq_sets) {
				err = -ENOMEM;
				goto err_alloc;
			}

			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				struct iecm_bufq_set *bufq_set =
					&rx_qgrp->splitq.bufq_sets[j];
				int swq_size = sizeof(struct iecm_sw_queue);

				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				q->dev = &adapter->pdev->dev;
				q->desc_count = vport->bufq_desc_count[j];
				q->vport = vport;
				q->rxq_grp = rx_qgrp;
				q->idx = j;
				q->rx_buf_size = vport->bufq_size[j];
				q->rx_buffer_low_watermark = IECM_LOW_WATERMARK;
				q->rx_buf_stride = IECM_RX_BUF_STRIDE;

				if (test_bit(__IECM_PRIV_FLAGS_HDR_SPLIT,
					     adapter->config_data.user_flags)) {
					q->rx_hsplit_en = true;
					q->rx_hbuf_size = IECM_HDR_BUF_SIZE;
				}

				bufq_set->num_refillqs = num_rxq;
				bufq_set->refillqs = kcalloc(num_rxq,
							     swq_size,
							     GFP_KERNEL);
				if (!bufq_set->refillqs) {
					err = -ENOMEM;
					goto err_alloc;
				}
				for (k = 0; k < bufq_set->num_refillqs; k++) {
					struct iecm_sw_queue *refillq =
						&bufq_set->refillqs[k];

					refillq->dev =
						&vport->adapter->pdev->dev;
					refillq->buf_size = q->rx_buf_size;
					refillq->desc_count =
						vport->bufq_desc_count[j];
					set_bit(__IECM_Q_GEN_CHK,
						refillq->flags);
					set_bit(__IECM_RFLQ_GEN_CHK,
						refillq->flags);
					refillq->ring = kcalloc(refillq->desc_count,
								sizeof(u16),
								GFP_KERNEL);
					if (!refillq->ring) {
						err = -ENOMEM;
						goto err_alloc;
					}
				}
			}
		} else {
			rx_qgrp->singleq.num_rxq = num_rxq;
			for (j = 0; j < num_rxq; j++) {
				rx_qgrp->singleq.rxqs[j] =
					kzalloc(sizeof(*rx_qgrp->singleq.rxqs[j]), GFP_KERNEL);
				if (!rx_qgrp->singleq.rxqs[j]) {
					err = -ENOMEM;
					goto err_alloc;
				}
			}
		}

		for (j = 0; j < num_rxq; j++) {
			if (iecm_is_queue_model_split(vport->rxq_model)) {
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
				rx_qgrp->splitq.rxq_sets[j]->refillq0 =
				      &rx_qgrp->splitq.bufq_sets[0].refillqs[j];
				rx_qgrp->splitq.rxq_sets[j]->refillq1 =
				      &rx_qgrp->splitq.bufq_sets[1].refillqs[j];

				if (test_bit(__IECM_PRIV_FLAGS_HDR_SPLIT,
					     adapter->config_data.user_flags)) {
					q->rx_hsplit_en = true;
					q->rx_hbuf_size = IECM_HDR_BUF_SIZE;
				}
			} else {
				q = rx_qgrp->singleq.rxqs[j];
			}
			q->dev = &adapter->pdev->dev;
			q->desc_count = vport->rxq_desc_count;
			q->vport = vport;
			q->rxq_grp = rx_qgrp;
			q->idx = (i * num_rxq) + j;
			/* In splitq mode, RXQ buffer size should be
			 * set to that of the first buffer queue
			 * associated with this RXQ
			 */
			q->rx_buf_size = vport->bufq_size[0];
			q->rx_buffer_low_watermark = IECM_LOW_WATERMARK;
			q->rx_max_pkt_size = vport->netdev->mtu +
							IECM_PACKET_HDR_PAD;
			iecm_rxq_set_descids(vport, q);
			iecm_set_vlan_tag_loc(adapter, q);
		}
	}
err_alloc:
	if (err)
		iecm_rxq_group_rel(vport);
	return err;
}

/**
 * iecm_vport_queue_grp_alloc_all - Allocate all queue groups/resources
 * @vport: vport with qgrps to allocate
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_vport_queue_grp_alloc_all(struct iecm_vport *vport)
{
	int num_txq, num_rxq;
	int err;

	iecm_vport_calc_numq_per_grp(vport, &num_txq, &num_rxq);

	err = iecm_txq_group_alloc(vport, num_txq);
	if (err)
		goto err_out;

	err = iecm_rxq_group_alloc(vport, num_rxq);
err_out:
	if (err)
		iecm_vport_queue_grp_rel_all(vport);
	return err;
}

/**
 * iecm_vport_queues_alloc - Allocate memory for all queues
 * @vport: virtual port
 *
 * Allocate memory for queues associated with a vport.  Returns 0 on success,
 * negative on failure.
 */
int iecm_vport_queues_alloc(struct iecm_vport *vport)
{
	int err;
	int i;

	err = iecm_vport_queue_grp_alloc_all(vport);
	if (err)
		goto err_out;

	err = iecm_tx_desc_alloc_all(vport);
	if (err)
		goto err_out;

	err = iecm_rx_desc_alloc_all(vport);
	if (err)
		goto err_out;

	err = iecm_vport_init_fast_path_txqs(vport);
	if (err)
		goto err_out;

	/* Initialize flow scheduling for queues that were requested
	 * before the interface was brought up
	 */
	for (i = 0; i < vport->num_txq; i++) {
		if (test_bit(i, vport->adapter->config_data.etf_qenable)) {
			set_bit(__IECM_Q_FLOW_SCH_EN, vport->txqs[i]->flags);
			set_bit(__IECM_Q_ETF_EN, vport->txqs[i]->flags);
		}
	}

	return 0;
err_out:
	iecm_vport_queues_rel(vport);
	return err;
}

/**
 * iecm_vport_intr_clean_queues - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 *
 */
irqreturn_t
iecm_vport_intr_clean_queues(int __always_unused irq, void *data)
{
	struct iecm_q_vector *q_vector = (struct iecm_q_vector *)data;

	q_vector->total_events++;
	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * iecm_vport_intr_napi_del_all - Unregister napi for all q_vectors in vport
 * @vport: virtual port structure
 *
 */
static void iecm_vport_intr_napi_del_all(struct iecm_vport *vport)
{
	u16 v_idx;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[v_idx];

		netif_napi_del(&q_vector->napi);
	}
}

/**
 * iecm_vport_intr_napi_dis_all - Disable NAPI for all q_vectors in the vport
 * @vport: main vport structure
 */
static void iecm_vport_intr_napi_dis_all(struct iecm_vport *vport)
{
	int q_idx;

	if (!vport->netdev)
		return;

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[q_idx];

		napi_disable(&q_vector->napi);
	}
}

/**
 * iecm_vport_intr_rel - Free memory allocated for interrupt vectors
 * @vport: virtual port
 *
 * Free the memory allocated for interrupt vectors  associated to a vport
 */
void iecm_vport_intr_rel(struct iecm_vport *vport)
{
	int i, j, v_idx;

	if (!vport->netdev)
		return;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[v_idx];

		kfree(q_vector->bufq);
		q_vector->bufq = NULL;
		kfree(q_vector->tx);
		q_vector->tx = NULL;
		kfree(q_vector->rx);
		q_vector->rx = NULL;
	}

	/* Clean up the mapping of queues to vectors */
	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < rx_qgrp->splitq.num_rxq_sets; j++)
				rx_qgrp->splitq.rxq_sets[j]->rxq.q_vector =
									   NULL;
		} else {
			for (j = 0; j < rx_qgrp->singleq.num_rxq; j++)
				rx_qgrp->singleq.rxqs[j]->q_vector = NULL;
		}
	}

	if (iecm_is_queue_model_split(vport->txq_model)) {
		for (i = 0; i < vport->num_txq_grp; i++)
			vport->txq_grps[i].complq->q_vector = NULL;
	} else {
		for (i = 0; i < vport->num_txq_grp; i++) {
			for (j = 0; j < vport->txq_grps[i].num_txq; j++)
				vport->txq_grps[i].txqs[j]->q_vector = NULL;
		}
	}

	kfree(vport->q_vectors);
	vport->q_vectors = NULL;
}

/**
 * iecm_vport_intr_rel_irq - Free the IRQ association with the OS
 * @vport: main vport structure
 */
static void iecm_vport_intr_rel_irq(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	int vector;

	for (vector = 0; vector < vport->num_q_vectors; vector++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[vector];
		int irq_num, vidx;

		/* free only the irqs that were actually requested */
		if (!q_vector)
			continue;

		vidx = vector + vport->q_vector_base;
		irq_num = adapter->msix_entries[vidx].vector;

		/* clear the affinity_mask in the IRQ descriptor */
		irq_set_affinity_hint(irq_num, NULL);
		free_irq(irq_num, q_vector);
	}
}

/**
 * iecm_vport_intr_dis_irq_all - Disable each interrupt
 * @vport: main vport structure
 */
void iecm_vport_intr_dis_irq_all(struct iecm_vport *vport)
{
	struct iecm_q_vector *q_vector = vport->q_vectors;
	struct iecm_hw *hw = &vport->adapter->hw;
	int q_idx;

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++)
		wr32(hw, q_vector[q_idx].intr_reg.dyn_ctl, 0);
}

/**
 * iecm_vport_intr_buildreg_itr - Enable default interrupt generation settings
 * @q_vector: pointer to q_vector
 * @type: itr index
 * @itr: itr value
 */
static u32 iecm_vport_intr_buildreg_itr(struct iecm_q_vector *q_vector,
					const int type, u16 itr)
{
	u32 itr_val;

	itr &= IECM_ITR_MASK;
	/* Don't clear PBA because that can cause lost interrupts that
	 * came in while we were cleaning/polling
	 */
	itr_val = q_vector->intr_reg.dyn_ctl_intena_m |
		  (type << q_vector->intr_reg.dyn_ctl_itridx_s) |
		  (itr << (q_vector->intr_reg.dyn_ctl_intrvl_s - 1));

	return itr_val;
}

/**
 * iecm_net_dim - Update net DIM algorithm
 * @q_vector: the vector associated with the interrupt
 *
 * Create a DIM sample and notify net_dim() so that it can possibly decide
 * a new ITR value based on incoming packets, bytes, and interrupts.
 *
 * This function is a no-op if the queue is not configured to dynamic ITR.
 */
static void iecm_net_dim(struct iecm_q_vector *q_vector)
{
	if (IECM_ITR_IS_DYNAMIC(q_vector->tx_intr_mode)) {
		struct dim_sample dim_sample = {};
		u64 packets = 0, bytes = 0;
		int i;

		for (i = 0; i < q_vector->num_txq; i++) {
			packets += q_vector->tx[i]->q_stats.tx.packets;
			bytes += q_vector->tx[i]->q_stats.tx.bytes;
		}

		dim_update_sample(q_vector->total_events, packets, bytes,
				  &dim_sample);
		net_dim(&q_vector->tx_dim, dim_sample);
	}

	if (IECM_ITR_IS_DYNAMIC(q_vector->rx_intr_mode)) {
		struct dim_sample dim_sample = {};
		u64 packets = 0, bytes = 0;
		int i;

		for (i = 0; i < q_vector->num_rxq; i++) {
			packets += q_vector->rx[i]->q_stats.rx.packets;
			bytes += q_vector->rx[i]->q_stats.rx.bytes;
		}

		dim_update_sample(q_vector->total_events, packets, bytes,
				  &dim_sample);
		net_dim(&q_vector->rx_dim, dim_sample);
	}
}

/**
 * iecm_vport_intr_update_itr_ena_irq - Update itr and re-enable MSIX interrupt
 * @q_vector: q_vector for which itr is being updated and interrupt enabled
 *
 * Update the net_dim() algorithm and re-enable the interrupt associated with
 * this vector.
 */
void iecm_vport_intr_update_itr_ena_irq(struct iecm_q_vector *q_vector)
{
	struct iecm_hw *hw = &q_vector->vport->adapter->hw;
	u32 intval;

	/* net_dim() updates ITR out-of-band using a work item */
	iecm_net_dim(q_vector);

	intval = iecm_vport_intr_buildreg_itr(q_vector,
					      VIRTCHNL2_ITR_IDX_NO_ITR, 0);

	wr32(hw, q_vector->intr_reg.dyn_ctl, intval);
}

/**
 * iecm_vport_intr_req_irq - get MSI-X vectors from the OS for the vport
 * @vport: main vport structure
 * @basename: name for the vector
 */
static int
iecm_vport_intr_req_irq(struct iecm_vport *vport, char *basename)
{
	struct iecm_adapter *adapter = vport->adapter;
	int vector, err, irq_num, vidx;

	for (vector = 0; vector < vport->num_q_vectors; vector++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[vector];

		vidx = vector + vport->q_vector_base;
		irq_num = adapter->msix_entries[vidx].vector;

		snprintf(q_vector->name, sizeof(q_vector->name) - 1,
			 "%s-%s-%d", basename, "TxRx", vidx);

		err = request_irq(irq_num, vport->irq_q_handler, 0,
				  q_vector->name, q_vector);
		if (err) {
			netdev_err(vport->netdev,
				   "Request_irq failed, error: %d\n", err);
			goto free_q_irqs;
		}
		/* assign the mask for this irq */
		irq_set_affinity_hint(irq_num, &q_vector->affinity_mask);
	}

	return 0;

free_q_irqs:
	while (vector) {
		vector--;
		vidx = vector + vport->q_vector_base;
		irq_num = adapter->msix_entries[vidx].vector;
		free_irq(irq_num, &vport->q_vectors[vector]);
	}
	return err;
}

/**
 * iecm_vport_intr_write_itr - Write ITR value to the ITR register
 * @q_vector: q_vector structure
 * @itr: Interrupt throttling rate
 * @tx: Tx or Rx ITR
 */
void iecm_vport_intr_write_itr(struct iecm_q_vector *q_vector, u16 itr, bool tx)
{
	struct iecm_hw *hw = &q_vector->vport->adapter->hw;
	struct iecm_intr_reg *intr_reg;

	if (tx && !q_vector->tx)
		return;
	else if (!tx && !q_vector->rx)
		return;

	intr_reg = &q_vector->intr_reg;
	wr32(hw, tx ? intr_reg->tx_itr : intr_reg->rx_itr,
	     ITR_REG_ALIGN(itr) >> IECM_ITR_GRAN_S);
}

/**
 * iecm_vport_intr_ena_irq_all - Enable IRQ for the given vport
 * @vport: main vport structure
 */
void iecm_vport_intr_ena_irq_all(struct iecm_vport *vport)
{
	int q_idx;

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[q_idx];

		if (q_vector->num_txq || q_vector->num_rxq) {
			/* Write the default ITR values */
			iecm_vport_intr_write_itr(q_vector,
						  q_vector->rx_itr_value,
						  false);
			iecm_vport_intr_write_itr(q_vector,
						  q_vector->tx_itr_value,
						  true);
			iecm_vport_intr_update_itr_ena_irq(q_vector);
		}
	}
}

/**
 * iecm_vport_intr_deinit - Release all vector associations for the vport
 * @vport: main vport structure
 */
void iecm_vport_intr_deinit(struct iecm_vport *vport)
{
	iecm_vport_intr_napi_dis_all(vport);
	iecm_vport_intr_napi_del_all(vport);
	iecm_vport_intr_dis_irq_all(vport);
	iecm_vport_intr_rel_irq(vport);
}

/**
 * iecm_tx_dim_work - Call back from the stack
 * @work: work queue structure
 */
static void iecm_tx_dim_work(struct work_struct *work)
{
	struct iecm_q_vector *q_vector;
	struct iecm_vport *vport;
	struct dim *dim;
	u16 itr;

	dim = container_of(work, struct dim, work);
	q_vector = container_of(dim, struct iecm_q_vector, tx_dim);
	vport = q_vector->vport;

	if (dim->profile_ix >= ARRAY_SIZE(vport->tx_itr_profile))
		dim->profile_ix = ARRAY_SIZE(vport->tx_itr_profile) - 1;

	/* look up the values in our local table */
	itr = vport->tx_itr_profile[dim->profile_ix];

	iecm_vport_intr_write_itr(q_vector, itr, true);

	dim->state = DIM_START_MEASURE;
}

/**
 * iecm_rx_dim_work - Call back from the stack
 * @work: work queue structure
 */
static void iecm_rx_dim_work(struct work_struct *work)
{
	struct iecm_q_vector *q_vector;
	struct iecm_vport *vport;
	struct dim *dim;
	u16 itr;

	dim = container_of(work, struct dim, work);
	q_vector = container_of(dim, struct iecm_q_vector, rx_dim);
	vport = q_vector->vport;

	if (dim->profile_ix >= ARRAY_SIZE(vport->rx_itr_profile))
		dim->profile_ix = ARRAY_SIZE(vport->rx_itr_profile) - 1;

	/* look up the values in our local table */
	itr = vport->rx_itr_profile[dim->profile_ix];

	iecm_vport_intr_write_itr(q_vector, itr, false);

	dim->state = DIM_START_MEASURE;
}

/**
 * iecm_vport_intr_napi_ena_all - Enable NAPI for all q_vectors in the vport
 * @vport: main vport structure
 */
static void
iecm_vport_intr_napi_ena_all(struct iecm_vport *vport)
{
	int q_idx;

	if (!vport->netdev)
		return;

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[q_idx];

		INIT_WORK(&q_vector->tx_dim.work, iecm_tx_dim_work);
		q_vector->tx_dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;

		INIT_WORK(&q_vector->rx_dim.work, iecm_rx_dim_work);
		q_vector->rx_dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;

		napi_enable(&q_vector->napi);
	}
}

/**
 * iecm_vport_splitq_napi_poll - NAPI handler
 * @napi: struct from which you get q_vector
 * @budget: budget provided by stack
 */
static int iecm_vport_splitq_napi_poll(struct napi_struct *napi, int budget)
{
	/* stub */
	return 0;
}

/**
 * iecm_vport_intr_map_vector_to_qs - Map vectors to queues
 * @vport: virtual port
 *
 * Mapping for vectors to queues
 */
static void iecm_vport_intr_map_vector_to_qs(struct iecm_vport *vport)
{
	int num_txq_grp = vport->num_txq_grp, bufq_vidx = 0;
	int i, j, qv_idx = 0, num_rxq, num_txq, q_index;
	struct iecm_rxq_group *rx_qgrp;
	struct iecm_txq_group *tx_qgrp;
	struct iecm_queue *q, *bufq;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];
		if (iecm_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			if (qv_idx >= vport->num_q_vectors)
				qv_idx = 0;

			if (iecm_is_queue_model_split(vport->rxq_model))
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];
			q->q_vector = &vport->q_vectors[qv_idx];
			q_index = q->q_vector->num_rxq;
			q->q_vector->rx[q_index] = q;
			q->q_vector->num_rxq++;
			qv_idx++;
		}

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
				bufq->q_vector = &vport->q_vectors[bufq_vidx];
				q_index = bufq->q_vector->num_bufq;
				bufq->q_vector->bufq[q_index] = bufq;
				bufq->q_vector->num_bufq++;
			}
			if (++bufq_vidx >= vport->num_q_vectors)
				bufq_vidx = 0;
		}
	}
	qv_idx = 0;
	for (i = 0; i < num_txq_grp; i++) {
		tx_qgrp = &vport->txq_grps[i];
		num_txq = tx_qgrp->num_txq;

		if (iecm_is_queue_model_split(vport->txq_model)) {
			if (qv_idx >= vport->num_q_vectors)
				qv_idx = 0;

			q = tx_qgrp->complq;
			q->q_vector = &vport->q_vectors[qv_idx];
			q_index = q->q_vector->num_txq;
			q->q_vector->tx[q_index] = q;
			q->q_vector->num_txq++;
			qv_idx++;
		} else {
			for (j = 0; j < num_txq; j++) {
				if (qv_idx >= vport->num_q_vectors)
					qv_idx = 0;

				q = tx_qgrp->txqs[j];
				q->q_vector = &vport->q_vectors[qv_idx];
				q_index = q->q_vector->num_txq;
				q->q_vector->tx[q_index] = q;
				q->q_vector->num_txq++;

				qv_idx++;
			}
		}
	}
}

/**
 * iecm_vport_intr_init_vec_idx - Initialize the vector indexes
 * @vport: virtual port
 *
 * Initialize vector indexes with values returened over mailbox
 */
static int iecm_vport_intr_init_vec_idx(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_q_vector *q_vector;
	int i;

	if (adapter->req_vec_chunks) {
		struct virtchnl2_vector_chunks *vchunks;
		struct virtchnl2_alloc_vectors *ac;
		u16 vecids[IECM_MAX_VECIDS];
		int num_ids;

		ac = adapter->req_vec_chunks;
		vchunks = &ac->vchunks;

		num_ids = iecm_get_vec_ids(adapter, vecids, IECM_MAX_VECIDS,
					   vchunks);

		if (num_ids < adapter->num_msix_entries)
			return -EFAULT;

		for (i = 0; i < vport->num_q_vectors; i++) {
			q_vector = &vport->q_vectors[i];
			q_vector->v_idx = vecids[i + vport->q_vector_base];
		}
	} else {
		for (i = 0; i < vport->num_q_vectors; i++) {
			q_vector = &vport->q_vectors[i];
			q_vector->v_idx = i + vport->q_vector_base;
		}
	}

	return 0;
}

/**
 * iecm_vport_intr_napi_add_all- Register napi handler for all qvectors
 * @vport: virtual port structure
 */
static void iecm_vport_intr_napi_add_all(struct iecm_vport *vport)
{
	u16 v_idx;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[v_idx];

		if (vport->netdev) {
			if (iecm_is_queue_model_split(vport->txq_model))
				netif_napi_add(vport->netdev, &q_vector->napi,
					       iecm_vport_splitq_napi_poll,
					       NAPI_POLL_WEIGHT);
			else
				netif_napi_add(vport->netdev, &q_vector->napi,
					       iecm_vport_singleq_napi_poll,
					       NAPI_POLL_WEIGHT);
		}

		/* only set affinity_mask if the CPU is online */
		if (cpu_online(v_idx))
			cpumask_set_cpu(v_idx, &q_vector->affinity_mask);
	}
}

/**
 * iecm_vport_intr_alloc - Allocate memory for interrupt vectors
 * @vport: virtual port
 *
 * We allocate one q_vector per queue interrupt. If allocation fails we
 * return -ENOMEM.
 */
int iecm_vport_intr_alloc(struct iecm_vport *vport)
{
	int txqs_per_vector, rxqs_per_vector, bufqs_per_vector;
	struct iecm_q_vector *q_vector;
	int v_idx, err;

	vport->q_vectors = kcalloc(vport->num_q_vectors,
				   sizeof(struct iecm_q_vector), GFP_KERNEL);

	if (!vport->q_vectors)
		return -ENOMEM;

	txqs_per_vector = DIV_ROUND_UP(vport->num_txq, vport->num_q_vectors);
	rxqs_per_vector = DIV_ROUND_UP(vport->num_rxq, vport->num_q_vectors);
	bufqs_per_vector = DIV_ROUND_UP(vport->num_bufqs_per_qgrp *
					vport->num_rxq_grp,
					vport->num_q_vectors);

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		q_vector = &vport->q_vectors[v_idx];
		q_vector->vport = vport;

		q_vector->tx_itr_value = IECM_ITR_TX_DEF;
		q_vector->tx_intr_mode = IECM_ITR_DYNAMIC;
		q_vector->tx_itr_idx = VIRTCHNL2_ITR_IDX_1;

		q_vector->rx_itr_value = IECM_ITR_RX_DEF;
		q_vector->rx_intr_mode = IECM_ITR_DYNAMIC;
		q_vector->rx_itr_idx = VIRTCHNL2_ITR_IDX_0;

		q_vector->tx = kcalloc(txqs_per_vector,
				       sizeof(struct iecm_queue *),
				       GFP_KERNEL);
		if (!q_vector->tx) {
			err = -ENOMEM;
			goto error;
		}

		q_vector->rx = kcalloc(rxqs_per_vector,
				       sizeof(struct iecm_queue *),
				       GFP_KERNEL);
		if (!q_vector->rx) {
			err = -ENOMEM;
			goto error;
		}

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			q_vector->bufq = kcalloc(bufqs_per_vector,
						 sizeof(struct iecm_queue *),
						 GFP_KERNEL);
			if (!q_vector->bufq) {
				err = -ENOMEM;
				goto error;
			}
		}
	}

	return 0;

error:
	iecm_vport_intr_rel(vport);
	return err;
}

/**
 * iecm_vport_intr_init - Setup all vectors for the given vport
 * @vport: virtual port
 *
 * Returns 0 on success or negative on failure
 */
int iecm_vport_intr_init(struct iecm_vport *vport)
{
	char int_name[IECM_INT_NAME_STR_LEN];
	int err = 0;

	err = iecm_vport_intr_init_vec_idx(vport);
	if (err)
		goto handle_err;

	iecm_vport_intr_map_vector_to_qs(vport);
	iecm_vport_intr_napi_add_all(vport);
	iecm_vport_intr_napi_ena_all(vport);

	err = vport->adapter->dev_ops.reg_ops.intr_reg_init(vport);
	if (err)
		goto unroll_vectors_alloc;

	snprintf(int_name, sizeof(int_name) - 1, "%s-%s",
		 dev_driver_string(&vport->adapter->pdev->dev),
		 vport->netdev->name);

	err = iecm_vport_intr_req_irq(vport, int_name);
	if (err)
		goto unroll_vectors_alloc;

	iecm_vport_intr_ena_irq_all(vport);
	goto handle_err;
unroll_vectors_alloc:
	iecm_vport_intr_napi_dis_all(vport);
	iecm_vport_intr_napi_del_all(vport);
handle_err:
	return err;
}

/**
 * iecm_config_rss - Prepare for RSS
 * @vport: virtual port
 *
 * Return 0 on success, negative on failure
 */
int iecm_config_rss(struct iecm_vport *vport)
{
	int err;

	err = vport->adapter->dev_ops.vc_ops.get_set_rss_key(vport, false);
	if (!err)
		err = vport->adapter->dev_ops.vc_ops.get_set_rss_lut(vport,
								     false);

	return err;
}

/**
 * iecm_fill_dflt_rss_lut - Fill the indirection table with the default values
 * @vport: virtual port structure
 */
void iecm_fill_dflt_rss_lut(struct iecm_vport *vport)
{
	u16 num_active_rxq = vport->num_rxq;
	int i;

	for (i = 0; i < vport->adapter->rss_data.rss_lut_size; i++)
		vport->adapter->rss_data.rss_lut[i] = i % num_active_rxq;
}

/**
 * iecm_init_rss - Prepare for RSS
 * @vport: virtual port
 *
 * Return 0 on success, negative on failure
 */
int iecm_init_rss(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	u32 lut_size;

	adapter->rss_data.rss_key = kzalloc(adapter->rss_data.rss_key_size,
					    GFP_KERNEL);
	if (!adapter->rss_data.rss_key)
		return -ENOMEM;

	lut_size = adapter->rss_data.rss_lut_size * sizeof(u32);
	adapter->rss_data.rss_lut = kzalloc(lut_size, GFP_KERNEL);
	if (!adapter->rss_data.rss_lut) {
		kfree(adapter->rss_data.rss_key);
		adapter->rss_data.rss_key = NULL;
		return -ENOMEM;
	}

	/* Initialize default rss key */
	netdev_rss_key_fill((void *)adapter->rss_data.rss_key,
			    adapter->rss_data.rss_key_size);

	/* Initialize default rss lut */
	if (adapter->rss_data.rss_lut_size % vport->num_rxq) {
		u32 dflt_qid;
		int i;

		/* Set all entries to a default RX queue if the algorithm below
		 * won't fill all entries
		 */
		if (iecm_is_queue_model_split(vport->rxq_model))
			dflt_qid =
				vport->rxq_grps[0].splitq.rxq_sets[0]->rxq.q_id;
		else
			dflt_qid =
				vport->rxq_grps[0].singleq.rxqs[0]->q_id;

		for (i = 0; i < adapter->rss_data.rss_lut_size; i++)
			adapter->rss_data.rss_lut[i] = dflt_qid;
	}

	/* Fill the default RSS lut values*/
	iecm_fill_dflt_rss_lut(vport);

	return iecm_config_rss(vport);
}

/**
 * iecm_deinit_rss - Prepare for RSS
 * @vport: virtual port
 *
 */
void iecm_deinit_rss(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;

	kfree(adapter->rss_data.rss_key);
	adapter->rss_data.rss_key = NULL;
	kfree(adapter->rss_data.rss_lut);
	adapter->rss_data.rss_lut = NULL;
}
