// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "iecm.h"

/**
 * iecm_tx_singleq_csum - Enable tx checksum offloads
 * @first: pointer to first descriptor
 * @off: pointer to struct that holds offload parameters
 *
 * Returns 0 or error (negative) if checksum offload
 */
static
int iecm_tx_singleq_csum(struct iecm_tx_buf *first,
			 struct iecm_tx_offload_params *off)
{
	u32 l4_len = 0, l3_len = 0, l2_len = 0;
	struct sk_buff *skb = first->skb;
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		unsigned char *hdr;
	} l4;
	__be16 frag_off, protocol;
	unsigned char *exthdr;
	u32 offset, cmd = 0;
	u8 l4_proto = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (skb->encapsulation)
		return -1;

	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute outer L2 header size */
	l2_len = ip.hdr - skb->data;
	offset = (l2_len / 2) << IECM_TX_DESC_LEN_MACLEN_S;

	/* Enable IP checksum offloads */
	protocol = vlan_get_protocol(skb);
	if (protocol == htons(ETH_P_IP)) {
		l4_proto = ip.v4->protocol;
		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		if (first->tx_flags & IECM_TX_FLAGS_TSO)
			cmd |= IECM_TX_DESC_CMD_IIPT_IPV4_CSUM;
		else
			cmd |= IECM_TX_DESC_CMD_IIPT_IPV4;

	} else if (protocol == htons(ETH_P_IPV6)) {
		cmd |= IECM_TX_DESC_CMD_IIPT_IPV6;
		exthdr = ip.hdr + sizeof(struct ipv6hdr);
		l4_proto = ip.v6->nexthdr;
		if (l4.hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data, &l4_proto,
					 &frag_off);
	} else {
		return -1;
	}

	/* compute inner L3 header size */
	l3_len = l4.hdr - ip.hdr;
	offset |= (l3_len / 4) << IECM_TX_DESC_LEN_IPLEN_S;

	/* Enable L4 checksum offloads */
	switch (l4_proto) {
	case IPPROTO_TCP:
		/* enable checksum offloads */
		cmd |= IECM_TX_DESC_CMD_L4T_EOFT_TCP;
		l4_len = l4.tcp->doff;
		offset |= l4_len << IECM_TX_DESC_LEN_L4_LEN_S;
		break;
	case IPPROTO_UDP:
		/* enable UDP checksum offload */
		cmd |= IECM_TX_DESC_CMD_L4T_EOFT_UDP;
		l4_len = (sizeof(struct udphdr) >> 2);
		offset |= l4_len << IECM_TX_DESC_LEN_L4_LEN_S;
		break;
	case IPPROTO_SCTP:
		/* enable SCTP checksum offload */
		cmd |= IECM_TX_DESC_CMD_L4T_EOFT_SCTP;
		l4_len = sizeof(struct sctphdr) >> 2;
		offset |= l4_len << IECM_TX_DESC_LEN_L4_LEN_S;
		break;

	default:
		if (first->tx_flags & IECM_TX_FLAGS_TSO)
			return -1;
		skb_checksum_help(skb);
		return 0;
	}

	off->td_cmd |= cmd;
	off->hdr_offsets |= offset;
	return 1;
}

/**
 * iecm_tx_singleq_map - Build the Tx base descriptor
 * @tx_q: queue to send buffer on
 * @first: first buffer info buffer to use
 * @offloads: pointer to struct that holds offload parameters
 *
 * This function loops over the skb data pointed to by *first
 * and gets a physical address for each memory location and programs
 * it and the length into the transmit base mode descriptor.
 */
static void
iecm_tx_singleq_map(struct iecm_queue *tx_q, struct iecm_tx_buf *first,
		    struct iecm_tx_offload_params *offloads)
{
	u32 offsets = offloads->hdr_offsets;
	struct iecm_base_tx_desc *tx_desc;
	u64 td_cmd = offloads->td_cmd;
	unsigned int data_len, size;
	struct iecm_tx_buf *tx_buf;
	u16 i = tx_q->next_to_use;
	struct netdev_queue *nq;
	struct sk_buff *skb;
	skb_frag_t *frag;
	dma_addr_t dma;
	u64 td_tag = 0;

	skb = first->skb;

	data_len = skb->data_len;
	size = skb_headlen(skb);

	tx_desc = IECM_BASE_TX_DESC(tx_q, i);

	if (first->tx_flags & IECM_TX_FLAGS_VLAN_TAG) {
		td_cmd |= (u64)IECM_TX_DESC_CMD_IL2TAG1;
		td_tag = (first->tx_flags & IECM_TX_FLAGS_VLAN_MASK) >>
			 IECM_TX_FLAGS_VLAN_SHIFT;
	}

	dma = dma_map_single(tx_q->dev, skb->data, size, DMA_TO_DEVICE);

	tx_buf = first;

	/* write each descriptor with CRC bit */
	if (tx_q->vport->adapter->dev_ops.crc_enable)
		tx_q->vport->adapter->dev_ops.crc_enable(&td_cmd);

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		unsigned int max_data = IECM_TX_MAX_DESC_DATA_ALIGNED;

		if (dma_mapping_error(tx_q->dev, dma))
			goto dma_error;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buf, len, size);
		dma_unmap_addr_set(tx_buf, dma, dma);

		/* align size to end of page */
		max_data += -dma & (IECM_TX_MAX_READ_REQ_SIZE - 1);
		tx_desc->buf_addr = cpu_to_le64(dma);

		/* account for data chunks larger than the hardware
		 * can handle
		 */
		while (unlikely(size > IECM_TX_MAX_DESC_DATA)) {
			tx_desc->qw1 = iecm_tx_singleq_build_ctob(td_cmd,
								  offsets,
								  max_data,
								  td_tag);
			tx_desc++;
			i++;

			if (i == tx_q->desc_count) {
				tx_desc = IECM_BASE_TX_DESC(tx_q, 0);
				i = 0;
			}

			dma += max_data;
			size -= max_data;

			max_data = IECM_TX_MAX_DESC_DATA_ALIGNED;
			tx_desc->buf_addr = cpu_to_le64(dma);
		}

		if (likely(!data_len))
			break;
		tx_desc->qw1 = iecm_tx_singleq_build_ctob(td_cmd, offsets,
							  size, td_tag);
		tx_desc++;
		i++;

		if (i == tx_q->desc_count) {
			tx_desc = IECM_BASE_TX_DESC(tx_q, 0);
			i = 0;
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_q->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_buf = &tx_q->tx_buf[i];
	}

	/* record bytecount for BQL */
	nq = netdev_get_tx_queue(tx_q->vport->netdev, tx_q->idx);
	netdev_tx_sent_queue(nq, first->bytecount);

	/* record SW timestamp if HW timestamp is not available */
	skb_tx_timestamp(first->skb);

	/* write last descriptor with RS and EOP bits */
	td_cmd |= (u64)(IECM_TX_DESC_CMD_EOP | IECM_TX_DESC_CMD_RS);

	tx_desc->qw1 = iecm_tx_singleq_build_ctob(td_cmd, offsets, size, td_tag);

	i++;
	if (i == tx_q->desc_count)
		i = 0;

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	iecm_tx_buf_hw_update(tx_q, i, netdev_xmit_more());

	return;

dma_error:
	/* clear dma mappings for failed tx_buf map */
	for (;;) {
		tx_buf = &tx_q->tx_buf[i];
		iecm_tx_buf_rel(tx_q, tx_buf);
		if (tx_buf == first)
			break;
		if (i == 0)
			i = tx_q->desc_count;
		i--;
	}

	tx_q->next_to_use = i;
}

/**
 * iecm_tx_singleq_frame - Sends buffer on Tx ring using base descriptors
 * @skb: send buffer
 * @tx_q: queue to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
static netdev_tx_t
iecm_tx_singleq_frame(struct sk_buff *skb, struct iecm_queue *tx_q)
{
	struct iecm_tx_offload_params offload = {0};
	struct iecm_tx_buf *first;
	unsigned int count;
	int csum, tso;

	count = iecm_tx_desc_count_required(skb);

	if (iecm_chk_linearize(skb, tx_q->tx_max_bufs, count)) {
		if (__skb_linearize(skb)) {
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
		count = iecm_size_to_txd_count(skb->len);
		tx_q->vport->port_stats.tx_linearize++;
	}

	if (iecm_tx_maybe_stop(tx_q, count + IECM_TX_DESCS_PER_CACHE_LINE +
			       IECM_TX_DESCS_FOR_CTX)) {
		return NETDEV_TX_BUSY;
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_q->tx_buf[tx_q->next_to_use];
	first->skb = skb;
	first->bytecount = max_t(unsigned int, skb->len, ETH_ZLEN);
	first->gso_segs = 1;
	first->tx_flags = 0;

	iecm_tx_prepare_vlan_flags(tx_q, first, skb);

	tso = iecm_tso(first, &offload);
	if (tso < 0)
		goto out_drop;

	csum = iecm_tx_singleq_csum(first, &offload);
	if (csum < 0)
		goto out_drop;

	if (first->tx_flags & IECM_TX_FLAGS_TSO) {
		struct iecm_base_tx_ctx_desc *ctx_desc;
		int i = tx_q->next_to_use;
		u64 qw1 = (u64)IECM_TX_DESC_DTYPE_CTX |
			       IECM_TX_CTX_DESC_TSO << IECM_TXD_CTX_QW1_CMD_S;

		/* grab the next descriptor */
		ctx_desc = IECM_BASE_TX_CTX_DESC(tx_q, i);
		i++;
		tx_q->next_to_use = (i < tx_q->desc_count) ? i : 0;

		qw1 |= ((u64)offload.tso_len << IECM_TXD_CTX_QW1_TSO_LEN_S) &
			IECM_TXD_CTX_QW1_TSO_LEN_M;

		qw1 |= ((u64)offload.mss << IECM_TXD_CTX_QW1_MSS_S) &
			IECM_TXD_CTX_QW1_MSS_M;

		ctx_desc->qw0.rsvd0 = cpu_to_le32(0);
		ctx_desc->qw0.l2tag2 = cpu_to_le16(0);
		ctx_desc->qw0.rsvd1 = cpu_to_le16(0);
		ctx_desc->qw1 = cpu_to_le64(qw1);
	}

	iecm_tx_singleq_map(tx_q, first, &offload);

	return NETDEV_TX_OK;

out_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * iecm_tx_singleq_start - Selects the right Tx queue to send buffer
 * @skb: send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t iecm_tx_singleq_start(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	struct iecm_queue *tx_q;

	if (test_bit(__IECM_HR_RESET_IN_PROG, vport->adapter->flags))
		return NETDEV_TX_BUSY;

	tx_q = vport->txqs[skb->queue_mapping];

	/* hardware can't handle really short frames, hardware padding works
	 * beyond this point
	 */
	if (skb_put_padto(skb, IECM_TX_MIN_LEN))
		return NETDEV_TX_OK;

	return iecm_tx_singleq_frame(skb, tx_q);
}

/**
 * iecm_tx_singleq_clean - Reclaim resources from queue
 * @tx_q: Tx queue to clean
 * @napi_budget: Used to determine if we are in netpoll
 *
 */
static bool iecm_tx_singleq_clean(struct iecm_queue *tx_q, int napi_budget)
{
	unsigned int budget = tx_q->vport->compln_clean_budget;
	unsigned int total_bytes = 0, total_pkts = 0;
	struct iecm_base_tx_desc *tx_desc;
	s16 ntc = tx_q->next_to_clean;
	struct iecm_tx_buf *tx_buf;
	struct netdev_queue *nq;

	tx_desc = IECM_BASE_TX_DESC(tx_q, ntc);
	tx_buf = &tx_q->tx_buf[ntc];
	ntc -= tx_q->desc_count;

	do {
		struct iecm_base_tx_desc *eop_desc =
			(struct iecm_base_tx_desc *)tx_buf->next_to_watch;

		/* if next_to_watch is not set then no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		/* if the descriptor isn't done, no work yet to do */
		if (!(eop_desc->qw1 &
		      cpu_to_le64(IECM_TX_DESC_DTYPE_DESC_DONE)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buf->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buf->bytecount;
		total_pkts += tx_buf->gso_segs;

		/* free the skb */
		napi_consume_skb(tx_buf->skb, napi_budget);

		/* unmap skb header data */
		dma_unmap_single(tx_q->dev,
				 dma_unmap_addr(tx_buf, dma),
				 dma_unmap_len(tx_buf, len),
				 DMA_TO_DEVICE);

		/* clear tx_buf data */
		tx_buf->skb = NULL;
		dma_unmap_len_set(tx_buf, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buf++;
			tx_desc++;
			ntc++;
			if (unlikely(!ntc)) {
				ntc -= tx_q->desc_count;
				tx_buf = tx_q->tx_buf;
				tx_desc = IECM_BASE_TX_DESC(tx_q, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buf, len)) {
				dma_unmap_page(tx_q->dev,
					       dma_unmap_addr(tx_buf, dma),
					       dma_unmap_len(tx_buf, len),
					       DMA_TO_DEVICE);
				dma_unmap_len_set(tx_buf, len, 0);
			}
		}

		tx_buf++;
		tx_desc++;
		ntc++;
		if (unlikely(!ntc)) {
			ntc -= tx_q->desc_count;
			tx_buf = tx_q->tx_buf;
			tx_desc = IECM_BASE_TX_DESC(tx_q, 0);
		}
		/* update budget */
		budget--;
	} while (likely(budget));

	ntc += tx_q->desc_count;
	tx_q->next_to_clean = ntc;

	u64_stats_update_begin(&tx_q->stats_sync);
	tx_q->q_stats.tx.packets += total_pkts;
	tx_q->q_stats.tx.bytes += total_bytes;
	u64_stats_update_end(&tx_q->stats_sync);

	nq = netdev_get_tx_queue(tx_q->vport->netdev, tx_q->idx);
	netdev_tx_completed_queue(nq, total_pkts, total_bytes);

	if (unlikely(total_pkts && netif_carrier_ok(tx_q->vport->netdev) &&
		     (IECM_DESC_UNUSED(tx_q) >= IECM_TX_WAKE_THRESH))) {
		/* Make sure any other threads stopping queue after this see
		 * new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_q->vport->netdev, tx_q->idx) &&
		    tx_q->vport->adapter->state == __IECM_UP)
			netif_wake_subqueue(tx_q->vport->netdev, tx_q->idx);
	}

	return !!budget;
}

/**
 * iecm_tx_singleq_clean_all - Clean all Tx queues
 * @q_vec: queue vector
 * @budget: Used to determine if we are in netpoll
 *
 * Returns false if clean is not complete else returns true
 */
static bool
iecm_tx_singleq_clean_all(struct iecm_q_vector *q_vec, int budget)
{
	bool clean_complete = true;
	int i, budget_per_q;

	budget_per_q = max(budget / q_vec->num_txq, 1);
	for (i = 0; i < q_vec->num_txq; i++)
		clean_complete = iecm_tx_singleq_clean(q_vec->tx[i], budget_per_q);

	return clean_complete;
}

/**
 * iecm_rx_singleq_test_staterr - tests bits in Rx descriptor
 * status and error fields
 * @rx_desc: pointer to receive descriptor (in le64 format)
 * @stat_err_bits: value to mask
 *
 * This function does some fast chicanery in order to return the
 * value of the mask which is really only used for boolean tests.
 * The status_error_ptype_len doesn't need to be shifted because it begins
 * at offset zero.
 */
bool
iecm_rx_singleq_test_staterr(union virtchnl2_rx_desc *rx_desc,
			     const u64 stat_err_bits)
{
	return !!(rx_desc->base_wb.qword1.status_error_ptype_len &
		  cpu_to_le64(stat_err_bits));
}

/**
 * iecm_rx_singleq_is_non_eop - process handling of non-EOP buffers
 * @rxq: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 * @skb: Current socket buffer containing buffer in progress
 */
bool iecm_rx_singleq_is_non_eop(struct iecm_queue *rxq,
				union virtchnl2_rx_desc *rx_desc,
				struct sk_buff *skb)
{
	/* if we are the last buffer then there is nothing else to do */
#define IECM_RXD_EOF BIT(VIRTCHNL2_RX_BASE_DESC_STATUS_EOF_S)
	if (likely(iecm_rx_singleq_test_staterr(rx_desc, IECM_RXD_EOF)))
		return false;

	/* place skb in next buffer to be received */
	rxq->rx_buf.buf[rxq->next_to_clean].skb = skb;

	return true;
}

/**
 * iecm_rx_singleq_csum - Indicate in skb if checksum is good
 * @rxq: Rx descriptor ring packet is being transacted on
 * @skb: skb currently being received and modified
 * @csum_bits: descriptor csum bits
 * @ptype: the packet type decoded by hardware
 *
 * skb->protocol must be set before this function is called
 */
static void iecm_rx_singleq_csum(struct iecm_queue *rxq, struct sk_buff *skb,
				 struct iecm_rx_csum_decoded *csum_bits,
				 u16 ptype)
{
	struct iecm_rx_ptype_decoded decoded;
	bool ipv4, ipv6;

	/* Start with CHECKSUM_NONE and by default csum_level = 0 */
	skb->ip_summed = CHECKSUM_NONE;
	skb_checksum_none_assert(skb);

	/* check if Rx checksum is enabled */
	if (!(rxq->vport->netdev->features & NETIF_F_RXCSUM))
		return;

	/* check if HW has decoded the packet and checksum */
	if (!(csum_bits->l3l4p))
		return;

	decoded = rxq->vport->rx_ptype_lkup[ptype];
	if (!(decoded.known && decoded.outer_ip))
		return;

	ipv4 = (decoded.outer_ip == IECM_RX_PTYPE_OUTER_IP) &&
	       (decoded.outer_ip_ver == IECM_RX_PTYPE_OUTER_IPV4);
	ipv6 = (decoded.outer_ip == IECM_RX_PTYPE_OUTER_IP) &&
	       (decoded.outer_ip_ver == IECM_RX_PTYPE_OUTER_IPV6);

	if (ipv4 && (csum_bits->ipe || csum_bits->eipe))
		goto checksum_fail;
	else if (ipv6 && (csum_bits->ipv6exadd))
		goto checksum_fail;

	/* check for L4 errors and handle packets that were not able to be
	 * checksummed due to arrival speed
	 */
	if (csum_bits->l4e)
		goto checksum_fail;

	if (csum_bits->nat && csum_bits->eudpe)
		goto checksum_fail;

	/* Handle packets that were not able to be checksummed due to arrival
	 * speed, in this case the stack can compute the csum.
	 */
	if (csum_bits->pprs)
		return;

	/* If there is an outer header present that might contain a checksum
	 * we need to bump the checksum level by 1 to reflect the fact that
	 * we are indicating we validated the inner checksum.
	 */
	if (decoded.tunnel_type >= IECM_RX_PTYPE_TUNNEL_IP_GRENAT)
		skb->csum_level = 1;

	/* Only report checksum unnecessary for ICMP, TCP, UDP, or SCTP */
	switch (decoded.inner_prot) {
	case IECM_RX_PTYPE_INNER_PROT_ICMP:
	case IECM_RX_PTYPE_INNER_PROT_TCP:
	case IECM_RX_PTYPE_INNER_PROT_UDP:
	case IECM_RX_PTYPE_INNER_PROT_SCTP:
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	default:
		break;
	}
	return;

checksum_fail:
	rxq->vport->port_stats.rx_hw_csum_err++;
}

/**
 * iecm_rx_singleq_base_csum - Indicate in skb if hw indicated a good cksum
 * @rx_q: Rx completion queue
 * @skb: skb currently being received and modified
 * @rx_desc: the receive descriptor
 * @ptype: Rx packet type
 *
 * This function only operates on the VIRTCHNL2_RXDID_1_32B_BASE_M legacy 32byte
 * descriptor writeback format.
 **/
static void
iecm_rx_singleq_base_csum(struct iecm_queue *rx_q, struct sk_buff *skb,
			  union virtchnl2_rx_desc *rx_desc, u16 ptype)
{
	struct iecm_rx_csum_decoded csum_bits;
	u32 rx_error, rx_status;
	u64 qword;

	qword = le64_to_cpu(rx_desc->base_wb.qword1.status_error_ptype_len);

	rx_status = ((qword & VIRTCHNL2_RX_BASE_DESC_QW1_STATUS_M) >>
					VIRTCHNL2_RX_BASE_DESC_QW1_STATUS_S);
	rx_error = ((qword & VIRTCHNL2_RX_BASE_DESC_QW1_ERROR_M) >>
					VIRTCHNL2_RX_BASE_DESC_QW1_ERROR_S);

	csum_bits.ipe = !!(rx_error & BIT(VIRTCHNL2_RX_BASE_DESC_ERROR_IPE_S));
	csum_bits.eipe = !!(rx_error & BIT(VIRTCHNL2_RX_BASE_DESC_ERROR_EIPE_S));
	csum_bits.l4e = !!(rx_error & BIT(VIRTCHNL2_RX_BASE_DESC_ERROR_L4E_S));
	csum_bits.pprs = !!(rx_error & BIT(VIRTCHNL2_RX_BASE_DESC_ERROR_PPRS_S));
	csum_bits.l3l4p = !!(rx_status &
			     BIT(VIRTCHNL2_RX_BASE_DESC_STATUS_L3L4P_S));
	csum_bits.ipv6exadd = !!(rx_status &
				 BIT(VIRTCHNL2_RX_BASE_DESC_STATUS_IPV6EXADD_S));
	csum_bits.nat = 0;
	csum_bits.eudpe = 0;

	iecm_rx_singleq_csum(rx_q, skb, &csum_bits, ptype);
}

/**
 * iecm_rx_singleq_flex_csum - Indicate in skb if hw indicated a good cksum
 * @rx_q: Rx completion queue
 * @skb: skb currently being received and modified
 * @rx_desc: the receive descriptor
 * @ptype: Rx packet type
 *
 * This function only operates on the VIRTCHNL2_RXDID_2_FLEX_SQ_NIC flexible
 * descriptor writeback format.
 **/
static void
iecm_rx_singleq_flex_csum(struct iecm_queue *rx_q, struct sk_buff *skb,
			  union virtchnl2_rx_desc *rx_desc, u16 ptype)
{
	struct iecm_rx_csum_decoded csum_bits;
	u16 rx_status0, rx_status1;

	rx_status0 = le16_to_cpu(rx_desc->flex_nic_wb.status_error0);
	rx_status1 = le16_to_cpu(rx_desc->flex_nic_wb.status_error1);

	csum_bits.ipe = !!(rx_status0 &
			   BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_IPE_S));
	csum_bits.eipe = !!(rx_status0 &
			    BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_EIPE_S));
	csum_bits.l4e = !!(rx_status0 &
			   BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_L4E_S));
	csum_bits.eudpe = !!(rx_status0 &
			     BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_XSUM_EUDPE_S));
	csum_bits.l3l4p = !!(rx_status0 &
			     BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_L3L4P_S));
	csum_bits.ipv6exadd =
			!!(rx_status0 &
			   BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_IPV6EXADD_S));
	csum_bits.nat = !!(rx_status1 &
			   BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS1_NAT_S));
	csum_bits.pprs = 0;

	iecm_rx_singleq_csum(rx_q, skb, &csum_bits, ptype);
}

/**
 * iecm_rx_singleq_base_hash - set the hash value in the skb
 * @rx_q: Rx completion queue
 * @skb: skb currently being received and modified
 * @rx_desc: specific descriptor
 * @decoded: Decoded Rx packet type related fields
 *
 * This function only operates on the VIRTCHNL2_RXDID_1_32B_BASE_M legacy 32byte
 * descriptor writeback format.
 **/
static void
iecm_rx_singleq_base_hash(struct iecm_queue *rx_q, struct sk_buff *skb,
			  union virtchnl2_rx_desc *rx_desc,
			  struct iecm_rx_ptype_decoded *decoded)
{
	const __le64 rss_mask =
		cpu_to_le64((u64)VIRTCHNL2_RX_BASE_DESC_FLTSTAT_RSS_HASH <<
				VIRTCHNL2_RX_BASE_DESC_STATUS_FLTSTAT_S);
	u32 hash;

	if (!(rx_q->vport->netdev->features & NETIF_F_RXHASH))
		return;

	if ((rx_desc->base_wb.qword1.status_error_ptype_len & rss_mask) ==
								rss_mask) {
		hash = le32_to_cpu(rx_desc->base_wb.qword0.hi_dword.rss);
		skb_set_hash(skb, hash, iecm_ptype_to_htype(decoded));
	}
}

/**
 * iecm_rx_singleq_flex_hash - set the hash value in the skb
 * @rx_q: Rx completion queue
 * @skb: skb currently being received and modified
 * @rx_desc: specific descriptor
 * @decoded: Decoded Rx packet type related fields
 *
 * This function only operates on the VIRTCHNL2_RXDID_2_FLEX_SQ_NIC flexible
 * descriptor writeback format.
 **/
static void
iecm_rx_singleq_flex_hash(struct iecm_queue *rx_q, struct sk_buff *skb,
			  union virtchnl2_rx_desc *rx_desc,
			  struct iecm_rx_ptype_decoded *decoded)
{
	__le16 status0;

	if (!(rx_q->vport->netdev->features & NETIF_F_RXHASH))
		return;

	status0 = rx_desc->flex_nic_wb.status_error0;
	if (status0 &
		cpu_to_le16(BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_RSS_VALID_S))) {
		u32 hash = le32_to_cpu(rx_desc->flex_nic_wb.rss_hash);

		skb_set_hash(skb, hash, iecm_ptype_to_htype(decoded));
	}
}

/**
 * iecm_rx_singleq_process_skb_fields - Populate skb header fields from Rx
 * descriptor
 * @rx_q: Rx descriptor ring packet is being transacted on
 * @skb: pointer to current skb being populated
 * @rx_desc: descriptor for skb
 * @ptype: packet type
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, VLAN, protocol, and
 * other fields within the skb.
 */
void
iecm_rx_singleq_process_skb_fields(struct iecm_queue *rx_q, struct sk_buff *skb,
				   union virtchnl2_rx_desc *rx_desc,
				   u16 ptype)
{
	struct iecm_rx_ptype_decoded decoded =
					rx_q->vport->rx_ptype_lkup[ptype];

	/* modifies the skb - consumes the enet header */
	skb->protocol = eth_type_trans(skb, rx_q->vport->netdev);

	if (rx_q->rxdids == VIRTCHNL2_RXDID_1_32B_BASE_M) {
		iecm_rx_singleq_base_hash(rx_q, skb, rx_desc, &decoded);
		iecm_rx_singleq_base_csum(rx_q, skb, rx_desc, ptype);
	} else {
		iecm_rx_singleq_flex_hash(rx_q, skb, rx_desc, &decoded);
		iecm_rx_singleq_flex_csum(rx_q, skb, rx_desc, ptype);
	}
}

/**
 * iecm_rx_singleq_buf_hw_alloc_all - Replace used receive buffers
 * @rx_q: queue for which the hw buffers are allocated
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail
 */
bool iecm_rx_singleq_buf_hw_alloc_all(struct iecm_queue *rx_q,
				      u16 cleaned_count)
{
	struct virtchnl2_singleq_rx_buf_desc *singleq_rx_desc = NULL;
	struct iecm_page_info *page_info;
	u16 nta = rx_q->next_to_alloc;
	struct iecm_rx_buf *buf;

	/* do nothing if no valid netdev defined */
	if (!rx_q->vport->netdev || !cleaned_count)
		return false;

	singleq_rx_desc = IECM_SINGLEQ_RX_BUF_DESC(rx_q, nta);
	buf = &rx_q->rx_buf.buf[nta];
	page_info = &buf->page_info[buf->page_indx];

	do {
		if (unlikely(!page_info->page)) {
			if (!iecm_init_rx_buf_hw_alloc(rx_q, buf))
				break;
		}

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		singleq_rx_desc->pkt_addr =
			cpu_to_le64(page_info->dma + page_info->page_offset);
		singleq_rx_desc->hdr_addr = 0;
		singleq_rx_desc++;

		buf++;
		nta++;
		if (unlikely(nta == rx_q->desc_count)) {
			singleq_rx_desc = IECM_SINGLEQ_RX_BUF_DESC(rx_q, 0);
			buf = rx_q->rx_buf.buf;
			nta = 0;
		}

		page_info = &buf->page_info[buf->page_indx];

		cleaned_count--;
	} while (cleaned_count);

	if (rx_q->next_to_alloc != nta) {
		iecm_rx_buf_hw_update(rx_q, nta);
		rx_q->next_to_alloc = nta;
	}

	return !!cleaned_count;
}

/**
 * iecm_rx_reuse_page - Put recycled buffer back onto ring
 * @rxq: Rx descriptor ring to store buffers on
 * @old_buf: donor buffer to have page reused
 */
static void iecm_rx_reuse_page(struct iecm_queue *rxq,
			       struct iecm_rx_buf *old_buf)
{
	u16 ntu = rxq->next_to_use;
	struct iecm_rx_buf *new_buf;

	new_buf = &rxq->rx_buf.buf[ntu];

	/* Transfer page from old buffer to new buffer.  Move each member
	 * individually to avoid possible store forwarding stalls and
	 * unnecessary copy of skb.
	 */
	new_buf->page_info[new_buf->page_indx].dma =
				old_buf->page_info[old_buf->page_indx].dma;
	new_buf->page_info[new_buf->page_indx].page =
				old_buf->page_info[old_buf->page_indx].page;
	new_buf->page_info[new_buf->page_indx].page_offset =
			old_buf->page_info[old_buf->page_indx].page_offset;
	new_buf->page_info[new_buf->page_indx].pagecnt_bias =
			old_buf->page_info[old_buf->page_indx].pagecnt_bias;
}

/**
 * iecm_rx_singleq_recycle_buf - Clean up used buffer and either recycle or free
 * @rxq: Rx descriptor queue to transact packets on
 * @rx_buf: Rx buffer to pull data from
 *
 * This function will clean up the contents of the rx_buf. It will either
 * recycle the buffer or unmap it and free the associated resources.
 *
 * Returns true if the buffer is reused, false if the buffer is freed.
 */
static bool iecm_rx_singleq_recycle_buf(struct iecm_queue *rxq,
					struct iecm_rx_buf *rx_buf)
{
	struct iecm_page_info *page_info =
			&rx_buf->page_info[rx_buf->page_indx];

	bool recycled = false;

	if (iecm_rx_can_reuse_page(rx_buf)) {
		/* hand second half of page back to the queue */
		iecm_rx_reuse_page(rxq, rx_buf);
		recycled = true;
	} else {
		/* we are not reusing the buffer so unmap it */
		dma_unmap_page_attrs(rxq->dev, page_info->dma, PAGE_SIZE,
				     DMA_FROM_DEVICE, IECM_RX_DMA_ATTR);
		__page_frag_cache_drain(page_info->page,
					page_info->pagecnt_bias);
	}

	/* clear contents of buffer_info */
	page_info->page = NULL;
	rx_buf->skb = NULL;

	return recycled;
}

/**
 * iecm_rx_singleq_put_buf - Wrapper function to clean and recycle buffers
 * @rxq: Rx descriptor queue to transact packets on
 * @rx_buf: Rx buffer to pull data from
 *
 * This function will update the next_to_use/next_to_alloc if the current
 * buffer is recycled.
 */
static void iecm_rx_singleq_put_buf(struct iecm_queue *rxq,
				    struct iecm_rx_buf *rx_buf)
{
	u16 ntu = rxq->next_to_use;
	bool recycled = false;

	recycled = iecm_rx_singleq_recycle_buf(rxq, rx_buf);

	/* update, and store next to alloc if the buffer was recycled */
	if (recycled) {
		ntu++;
		rxq->next_to_use = (ntu < rxq->desc_count) ? ntu : 0;
	}
}

/**
 * iecm_rx_singleq_bump_ntc - Bump and wrap q->next_to_clean value
 * @rxq: queue to bump
 */
void iecm_rx_singleq_bump_ntc(struct iecm_queue *rxq)
{
	u16 ntc = rxq->next_to_clean + 1;
	/* fetch, update, and store next to clean */
	if (ntc < rxq->desc_count)
		rxq->next_to_clean = ntc;
	else
		rxq->next_to_clean = 0;
}

/**
 * iecm_rx_singleq_get_buf_page - Fetch Rx buffer page and synchronize data
 * @dev: device struct
 * @rx_buf: Rx buf to fetch page for
 * @size: size of buffer to add to skb
 *
 * This function will pull an Rx buffer page from the ring and synchronize it
 * for use by the CPU.
 */
static struct sk_buff *
iecm_rx_singleq_get_buf_page(struct device *dev, struct iecm_rx_buf *rx_buf,
			     const unsigned int size)
{
	struct iecm_page_info *page_info;

	page_info = &rx_buf->page_info[rx_buf->page_indx];
	prefetch(page_info->page);

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(dev, page_info->dma,
				      page_info->page_offset, size,
				      DMA_FROM_DEVICE);

	/* We have pulled a buffer for use, so decrement pagecnt_bias */
	page_info->pagecnt_bias--;

	return rx_buf->skb;
}

/**
 * iecm_rx_singleq_extract_base_fields - Extract fields from the Rx descriptor
 * @rx_q: Rx descriptor queue
 * @rx_desc: the descriptor to process
 * @fields: storage for extracted values
 *
 * Decode the Rx descriptor and extract relevant information including the
 * size, VLAN tag, and Rx packet type.
 *
 * This function only operates on the VIRTCHNL2_RXDID_1_32B_BASE_M legacy 32byte
 * descriptor writeback format.
 */
static inline void
iecm_rx_singleq_extract_base_fields(struct iecm_queue *rx_q,
				    union virtchnl2_rx_desc *rx_desc,
				    struct iecm_rx_extracted *fields)
{
	u64 qw2_status, qword;

	qword = le64_to_cpu(rx_desc->base_wb.qword1.status_error_ptype_len);
	qw2_status = le16_to_cpu(rx_desc->base_wb.qword2.ext_status);

	fields->size = (qword & VIRTCHNL2_RX_BASE_DESC_QW1_LEN_PBUF_M) >>
					VIRTCHNL2_RX_BASE_DESC_QW1_LEN_PBUF_S;
	fields->rx_ptype = (qword & VIRTCHNL2_RX_BASE_DESC_QW1_PTYPE_M) >>
					VIRTCHNL2_RX_BASE_DESC_QW1_PTYPE_S;

	if (qword & BIT(VIRTCHNL2_RX_BASE_DESC_STATUS_L2TAG1P_S) &&
	    test_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG1, rx_q->flags))
		fields->vlan_tag =
			le16_to_cpu(rx_desc->base_wb.qword0.lo_dword.l2tag1);
	if (qw2_status & BIT(VIRTCHNL2_RX_BASE_DESC_EXT_STATUS_L2TAG2P_S) &&
	    test_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG2, rx_q->flags))
		fields->vlan_tag =
			le16_to_cpu(rx_desc->base_wb.qword2.l2tag2_1);
}

/**
 * iecm_rx_singleq_extract_flex_fields - Extract fields from the Rx descriptor
 * @rx_q: Rx descriptor queue
 * @rx_desc: the descriptor to process
 * @fields: storage for extracted values
 *
 * Decode the Rx descriptor and extract relevant information including the
 * size, VLAN tag, and Rx packet type.
 *
 * This function only operates on the VIRTCHNL2_RXDID_2_FLEX_SQ_NIC flexible
 * descriptor writeback format.
 */
static inline void
iecm_rx_singleq_extract_flex_fields(struct iecm_queue *rx_q,
				    union virtchnl2_rx_desc *rx_desc,
				    struct iecm_rx_extracted *fields)
{
	__le16 status0, status1;

	fields->size = le16_to_cpu(rx_desc->flex_nic_wb.pkt_len) &
					VIRTCHNL2_RX_FLEX_DESC_PKT_LEN_M;
	fields->rx_ptype = le16_to_cpu(rx_desc->flex_nic_wb.ptype_flex_flags0) &
					VIRTCHNL2_RX_FLEX_DESC_PTYPE_M;

	status0 = rx_desc->flex_nic_wb.status_error0;
	if (status0 & cpu_to_le16(BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS0_L2TAG1P_S)) &&
	    test_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG1, rx_q->flags))
		fields->vlan_tag = le16_to_cpu(rx_desc->flex_nic_wb.l2tag1);

	status1 = rx_desc->flex_nic_wb.status_error1;
	if (status1 & cpu_to_le16(BIT(VIRTCHNL2_RX_FLEX_DESC_STATUS1_L2TAG2P_S)) &&
	    test_bit(__IECM_Q_VLAN_TAG_LOC_L2TAG2, rx_q->flags))
		fields->vlan_tag = le16_to_cpu(rx_desc->flex_nic_wb.l2tag2_2nd);
}

/**
 * iecm_rx_singleq_extract_fields - Extract fields from the Rx descriptor
 * @rx_q: Rx descriptor queue
 * @rx_desc: the descriptor to process
 * @fields: storage for extracted values
 *
 */
void
iecm_rx_singleq_extract_fields(struct iecm_queue *rx_q,
			       union virtchnl2_rx_desc *rx_desc,
			       struct iecm_rx_extracted *fields)
{
	if (rx_q->rxdids == VIRTCHNL2_RXDID_1_32B_BASE_M)
		iecm_rx_singleq_extract_base_fields(rx_q, rx_desc, fields);
	else
		iecm_rx_singleq_extract_flex_fields(rx_q, rx_desc, fields);
}

/**
 * iecm_rx_singleq_clean - Reclaim resources after receive completes
 * @rx_q: rx queue to clean
 * @budget: Total limit on number of packets to process
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 */
static int iecm_rx_singleq_clean(struct iecm_queue *rx_q, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_pkts = 0;
	u16 cleaned_count = 0;
	bool failure = false;

	/* Process Rx packets bounded by budget */
	while (likely(total_rx_pkts < (unsigned int)budget)) {
		struct iecm_rx_extracted fields = {};
		union virtchnl2_rx_desc *rx_desc;
		struct sk_buff *skb = NULL;
		struct iecm_rx_buf *rx_buf;

		/* get the Rx desc from Rx queue based on 'next_to_clean' */
		rx_desc = IECM_RX_DESC(rx_q, rx_q->next_to_clean);

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc
		 */
		dma_rmb();

		/* status_error_ptype_len will always be zero for unused
		 * descriptors because it's cleared in cleanup, and overlaps
		 * with hdr_addr which is always zero because packet split
		 * isn't used, if the hardware wrote DD then the length will be
		 * non-zero
		 */
#define IECM_RXD_DD BIT(VIRTCHNL2_RX_BASE_DESC_STATUS_DD_S)
		if (!iecm_rx_singleq_test_staterr(rx_desc,
						  IECM_RXD_DD))
			break;
		iecm_rx_singleq_extract_fields(rx_q, rx_desc, &fields);

		if (!fields.size)
			break;

		rx_buf = &rx_q->rx_buf.buf[rx_q->next_to_clean];
		skb = iecm_rx_singleq_get_buf_page(rx_q->dev, rx_buf,
						   fields.size);

		if (skb)
			iecm_rx_add_frag(rx_buf, skb, fields.size);
		else
			skb = iecm_rx_construct_skb(rx_q, rx_buf, fields.size);

		/* exit if we failed to retrieve a buffer */
		if (!skb) {
			rx_buf->page_info[rx_buf->page_indx].pagecnt_bias++;
			break;
		}

		iecm_rx_singleq_put_buf(rx_q, rx_buf);
		iecm_rx_singleq_bump_ntc(rx_q);

		cleaned_count++;

		/* skip if it is non EOP desc */
		if (iecm_rx_singleq_is_non_eop(rx_q, rx_desc,
					       skb))
			continue;

#define IECM_RXD_ERR_S BIT(VIRTCHNL2_RX_BASE_DESC_QW1_ERROR_S)
		if (unlikely(iecm_rx_singleq_test_staterr(rx_desc,
							  IECM_RXD_ERR_S))) {
			dev_kfree_skb_any(skb);
			skb = NULL;
			continue;
		}

		/* pad skb if needed (to make valid ethernet frame) */
		if (eth_skb_pad(skb)) {
			skb = NULL;
			continue;
		}

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;

		/* protocol */
		iecm_rx_singleq_process_skb_fields(rx_q, skb,
						   rx_desc, fields.rx_ptype);

		/* send completed skb up the stack */
		iecm_rx_skb(rx_q, skb, fields.vlan_tag);

		/* update budget accounting */
		total_rx_pkts++;
	}
	if (cleaned_count)
		failure = iecm_rx_singleq_buf_hw_alloc_all(rx_q, cleaned_count);

	u64_stats_update_begin(&rx_q->stats_sync);
	rx_q->q_stats.rx.packets += total_rx_pkts;
	rx_q->q_stats.rx.bytes += total_rx_bytes;
	u64_stats_update_end(&rx_q->stats_sync);

	/* guarantee a trip back through this routine if there was a failure */
	return failure ? budget : (int)total_rx_pkts;
}

/**
 * iecm_rx_singleq_clean_all - Clean all Rx queues
 * @q_vec: queue vector
 * @budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 * Returns false if clean is not complete else returns true
 */
static bool
iecm_rx_singleq_clean_all(struct iecm_q_vector *q_vec, int budget,
			  int *cleaned)
{
	bool clean_complete = true;
	int budget_per_q, i;

	budget_per_q = max(budget / q_vec->num_rxq, 1);
	for (i = 0; i < q_vec->num_rxq; i++) {
		struct iecm_queue *rxq = q_vec->rx[i];
		int pkts_cleaned_per_q;

		pkts_cleaned_per_q = iecm_rx_singleq_clean(rxq, budget_per_q);

		/* if we clean as many as budgeted, we must not be done */
		if (pkts_cleaned_per_q >= budget_per_q)
			clean_complete = false;
		*cleaned += pkts_cleaned_per_q;
	}

	return clean_complete;
}

/**
 * iecm_vport_singleq_napi_poll - NAPI handler
 * @napi: struct from which you get q_vector
 * @budget: budget provided by stack
 */
int iecm_vport_singleq_napi_poll(struct napi_struct *napi, int budget)
{
	struct iecm_q_vector *q_vector =
				container_of(napi, struct iecm_q_vector, napi);
	bool clean_complete;
	int work_done = 0;

	clean_complete = iecm_tx_singleq_clean_all(q_vector, budget);

	/* Handle case where we are called by netpoll with a budget of 0 */
	if (budget <= 0)
		return budget;

	/* We attempt to distribute budget to each Rx queue fairly, but don't
	 * allow the budget to go below 1 because that would exit polling early.
	 */
	clean_complete |= iecm_rx_singleq_clean_all(q_vector, budget,
						    &work_done);

	/* If work not completed, return budget and polling will return */
	if (!clean_complete)
		return budget;

	/* Exit the polling mode, but don't re-enable interrupts if stack might
	 * poll us due to busy-polling
	 */
	if (likely(napi_complete_done(napi, work_done)))
		iecm_vport_intr_update_itr_ena_irq(q_vector);
	return min_t(int, work_done, budget - 1);
}
