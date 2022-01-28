/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Intel Corporation */

#ifndef _IECM_TXRX_H_
#define _IECM_TXRX_H_

#define IECM_LARGE_MAX_Q			256
#define IECM_MAX_Q				16
/* Mailbox Queue */
#define IECM_MAX_NONQ				1
#define IECM_MAX_TXQ_DESC			4096
#define IECM_MAX_RXQ_DESC			4096
#define IECM_MIN_TXQ_DESC			32
#define IECM_MIN_TXQ_COMPLQ_DESC		64
#define IECM_MIN_RXQ_DESC			32
#define IECM_REQ_DESC_MULTIPLE			32
#define IECM_REQ_SPLITQ_RXQ_DESC_MULTIPLE	64
#define IECM_MIN_TX_DESC_NEEDED (MAX_SKB_FRAGS + 6)
#define IECM_TX_WAKE_THRESH ((s16)IECM_MIN_TX_DESC_NEEDED * 2)

#define IECM_DFLT_SINGLEQ_TX_Q_GROUPS		1
#define IECM_DFLT_SINGLEQ_RX_Q_GROUPS		1
#define IECM_DFLT_SINGLEQ_TXQ_PER_GROUP		4
#define IECM_DFLT_SINGLEQ_RXQ_PER_GROUP		4

#define IECM_COMPLQ_PER_GROUP			1
#define IECM_MAX_BUFQS_PER_RXQ_GRP		2

#define IECM_DFLT_SPLITQ_TX_Q_GROUPS		4
#define IECM_DFLT_SPLITQ_RX_Q_GROUPS		4
#define IECM_DFLT_SPLITQ_TXQ_PER_GROUP		1
#define IECM_DFLT_SPLITQ_RXQ_PER_GROUP		1

/* Default vector sharing */
#define IECM_NONQ_VEC		1
#define IECM_MAX_Q_VEC		4 /* For Tx Completion queue and Rx queue */
#define IECM_MIN_Q_VEC		1
#define IECM_MAX_RDMA_VEC	2 /* To share with RDMA */
#define IECM_MIN_RDMA_VEC	1 /* Minimum vectors to be shared with RDMA */
#define IECM_MIN_VEC		3 /* One for mailbox, one for data queues, one
				   * for RDMA
				   */

#define IECM_DFLT_TX_Q_DESC_COUNT		512
#define IECM_DFLT_TX_COMPLQ_DESC_COUNT		512
#define IECM_DFLT_RX_Q_DESC_COUNT		512
/* IMPORTANT: We absolutely _cannot_ have more buffers in the system than a
 * given RX completion queue has descriptors. This includes _ALL_ buffer
 * queues. E.g.: If you have two buffer queues of 512 descriptors and buffers,
 * you have a total of 1024 buffers so your RX queue _must_ have at least that
 * many descriptors. This macro divides a given number of RX descriptors by
 * number of buffer queues to calculate how many descriptors each buffer queue
 * can have without overrunning the RX queue.
 *
 * If you give hardware more buffers than completion descriptors what will
 * happen is that if hardware gets a chance to post more than ring wrap of
 * descriptors before SW gets an interrupt and overwrites SW head, the gen bit
 * in the descriptor will be wrong. Any overwritten descriptors' buffers will
 * be gone forever and SW has no reasonable way to tell that this has happened.
 * From SW perspective, when we finally get an interrupt, it looks like we're
 * still waiting for descriptor to be done, stalling forever.
 */
#define IECM_RX_BUFQ_DESC_COUNT(RXD, NUM_BUFQ)	((RXD) / (NUM_BUFQ))

#define IECM_RX_BUFQ_WORKING_SET(R)		((R)->desc_count - 1)
#define IECM_RX_BUFQ_NON_WORKING_SET(R)		((R)->desc_count - \
						 IECM_RX_BUFQ_WORKING_SET(R))

#define IECM_RX_HDR_SIZE			256
#define IECM_RX_BUF_2048			2048
#define IECM_RX_BUF_4096			4096
#define IECM_RX_BUF_STRIDE			64
#define IECM_LOW_WATERMARK			64
#define IECM_HDR_BUF_SIZE			256
#define IECM_PACKET_HDR_PAD	\
	(ETH_HLEN + ETH_FCS_LEN + (VLAN_HLEN * 2))
#define IECM_MAX_RXBUFFER			9728
#define IECM_MAX_MTU		\
	(IECM_MAX_RXBUFFER - IECM_PACKET_HDR_PAD)
#define IECM_INT_NAME_STR_LEN	(IFNAMSIZ + 16)

#define IECM_TX_COMPLQ_CLEAN_BUDGET	256

struct iecm_intr_reg {
	u32 dyn_ctl;
	u32 dyn_ctl_intena_m;
	u32 dyn_ctl_clrpba_m;
	u32 dyn_ctl_itridx_s;
	u32 dyn_ctl_itridx_m;
	u32 dyn_ctl_intrvl_s;
	u32 rx_itr;
	u32 tx_itr;
	u32 icr_ena;
	u32 icr_ena_ctlq_m;
};

struct iecm_q_vector {
	struct iecm_vport *vport;
	cpumask_t affinity_mask;
	struct napi_struct napi;
	u16 v_idx;		/* index in the vport->q_vector array */
	struct iecm_intr_reg intr_reg;

	int num_txq;
	struct iecm_queue **tx;
	struct dim tx_dim;	/* data for net_dim algorithm */
	u16 tx_itr_value;
	bool tx_intr_mode;
	u32 tx_itr_idx;

	int num_rxq;
	struct iecm_queue **rx;
	struct dim rx_dim;	/* data for net_dim algorithm */
	u16 rx_itr_value;
	bool rx_intr_mode;
	u32 rx_itr_idx;

	int num_bufq;
	struct iecm_queue **bufq;

	u16 total_events;       /* net_dim(): number of interrupts processed */
	char name[IECM_INT_NAME_STR_LEN];
};

irqreturn_t
iecm_vport_intr_clean_queues(int __always_unused irq, void *data);
#endif /* !_IECM_TXRX_H_ */
