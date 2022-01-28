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
