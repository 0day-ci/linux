/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019 Intel Corporation */

#ifndef _IECM_TXRX_H_
#define _IECM_TXRX_H_

#include "virtchnl_lan_desc.h"
#include <linux/indirect_call_wrapper.h>

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

#define IECM_RX_BI_BUFID_S		0
#define IECM_RX_BI_BUFID_M		MAKEMASK(0x7FFF, IECM_RX_BI_BUFID_S)
#define IECM_RX_BI_GEN_S		15
#define IECM_RX_BI_GEN_M		BIT(IECM_RX_BI_GEN_S)

#define IECM_SINGLEQ_RX_BUF_DESC(R, i)	\
	(&(((struct virtchnl2_singleq_rx_buf_desc *)((R)->desc_ring))[i]))
#define IECM_SPLITQ_RX_BUF_DESC(R, i)	\
	(&(((struct virtchnl2_splitq_rx_buf_desc *)((R)->desc_ring))[i]))
#define IECM_SPLITQ_RX_BI_DESC(R, i)	\
	(&(((u16 *)((R)->ring))[i]))

#define IECM_BASE_TX_DESC(R, i)	\
	(&(((struct iecm_base_tx_desc *)((R)->desc_ring))[i]))
#define IECM_BASE_TX_CTX_DESC(R, i) \
	(&(((struct iecm_base_tx_ctx_desc *)((R)->desc_ring))[i]))
#define IECM_SPLITQ_TX_COMPLQ_DESC(R, i)	\
	(&(((struct iecm_splitq_tx_compl_desc *)((R)->desc_ring))[i]))

#define IECM_FLEX_TX_DESC(R, i)	\
	(&(((union iecm_tx_flex_desc *)((R)->desc_ring))[i]))
#define IECM_FLEX_TX_CTX_DESC(R, i)	\
	(&(((union iecm_flex_tx_ctx_desc *)((R)->desc_ring))[i]))

#define IECM_DESC_UNUSED(R)	\
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->desc_count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define IECM_TX_BUF_UNUSED(R)	((R)->buf_stack.top)

#define IECM_TXD_LAST_DESC_CMD (IECM_TX_DESC_CMD_EOP | IECM_TX_DESC_CMD_RS)

#define MAKEMASK(m, s)	((m) << (s))

struct iecm_tx_buf {
	struct hlist_node hlist;
	void *next_to_watch;
	union {
		struct sk_buff *skb;
		struct xdp_frame *xdpf;
	};
	unsigned int bytecount;
	unsigned short gso_segs;
#define IECM_TX_FLAGS_TSO			BIT(0)
#define IECM_TX_FLAGS_VLAN_TAG			BIT(1)
#define IECM_TX_FLAGS_HW_VLAN			BIT(2)
#define IECM_TX_FLAGS_HW_OUTER_SINGLE_VLAN	BIT(3)
#define IECM_TX_FLAGS_VLAN_SHIFT		16
#define IECM_TX_FLAGS_VLAN_MASK			0xFFFF0000
	u32 tx_flags;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u16 compl_tag;		/* Unique identifier for buffer; used to
				 * compare with completion tag returned
				 * in buffer completion event
				 */
};

struct iecm_buf_lifo {
	u16 top;
	u16 size;
	struct iecm_tx_buf **bufs;
};

/* Checksum offload bits decoded from the receive descriptor. */
struct iecm_rx_csum_decoded {
	u8 l3l4p : 1;
	u8 ipe : 1;
	u8 eipe : 1;
	u8 eudpe : 1;
	u8 ipv6exadd : 1;
	u8 l4e : 1;
	u8 pprs : 1;
	u8 nat : 1;
	u8 rsc : 1;
	u8 raw_csum_inv : 1;
	u16 raw_csum;
};

struct iecm_rx_extracted {
	unsigned int size;
	u16 vlan_tag;
	u16 rx_ptype;
};

#define IECM_TX_COMPLQ_CLEAN_BUDGET	256
#define IECM_TX_MIN_LEN			17
#define IECM_TX_DESCS_FOR_SKB_DATA_PTR	1
#define IECM_TX_MAX_BUF			8
#define IECM_TX_DESCS_PER_CACHE_LINE	4
#define IECM_TX_DESCS_FOR_CTX		1
/* TX descriptors needed, worst case */
#define IECM_TX_DESC_NEEDED (MAX_SKB_FRAGS + IECM_TX_DESCS_FOR_CTX + \
			     IECM_TX_DESCS_PER_CACHE_LINE + \
			     IECM_TX_DESCS_FOR_SKB_DATA_PTR)

/* The size limit for a transmit buffer in a descriptor is (16K - 1).
 * In order to align with the read requests we will align the value to
 * the nearest 4K which represents our maximum read request size.
 */
#define IECM_TX_MAX_READ_REQ_SIZE	4096
#define IECM_TX_MAX_DESC_DATA		(16 * 1024 - 1)
#define IECM_TX_MAX_DESC_DATA_ALIGNED \
	(~(IECM_TX_MAX_READ_REQ_SIZE - 1) & IECM_TX_MAX_DESC_DATA)

#define IECM_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)
#define IECM_RX_DESC(R, i)	\
	(&(((union virtchnl2_rx_desc *)((R)->desc_ring))[i]))

struct iecm_page_info {
	dma_addr_t dma;
	struct page *page;
	unsigned int page_offset;
	u16 pagecnt_bias;
};

struct iecm_rx_hdr_buf_pages {
	u32 nr_pages;
	struct iecm_page_info *pages;
};

struct iecm_rx_buf {
#define IECM_RX_BUF_MAX_PAGES 2
	struct iecm_page_info page_info[IECM_RX_BUF_MAX_PAGES];
	u8 page_indx;
	u16 buf_id;
	u16 buf_size;
	struct sk_buff *skb;
};

/* Packet type non-ip values */
enum iecm_rx_ptype_l2 {
	IECM_RX_PTYPE_L2_RESERVED	= 0,
	IECM_RX_PTYPE_L2_MAC_PAY2	= 1,
	IECM_RX_PTYPE_L2_TIMESYNC_PAY2	= 2,
	IECM_RX_PTYPE_L2_FIP_PAY2	= 3,
	IECM_RX_PTYPE_L2_OUI_PAY2	= 4,
	IECM_RX_PTYPE_L2_MACCNTRL_PAY2	= 5,
	IECM_RX_PTYPE_L2_LLDP_PAY2	= 6,
	IECM_RX_PTYPE_L2_ECP_PAY2	= 7,
	IECM_RX_PTYPE_L2_EVB_PAY2	= 8,
	IECM_RX_PTYPE_L2_QCN_PAY2	= 9,
	IECM_RX_PTYPE_L2_EAPOL_PAY2	= 10,
	IECM_RX_PTYPE_L2_ARP		= 11,
};

enum iecm_rx_ptype_outer_ip {
	IECM_RX_PTYPE_OUTER_L2	= 0,
	IECM_RX_PTYPE_OUTER_IP	= 1,
};

enum iecm_rx_ptype_outer_ip_ver {
	IECM_RX_PTYPE_OUTER_NONE	= 0,
	IECM_RX_PTYPE_OUTER_IPV4	= 1,
	IECM_RX_PTYPE_OUTER_IPV6	= 2,
};

enum iecm_rx_ptype_outer_fragmented {
	IECM_RX_PTYPE_NOT_FRAG	= 0,
	IECM_RX_PTYPE_FRAG	= 1,
};

enum iecm_rx_ptype_tunnel_type {
	IECM_RX_PTYPE_TUNNEL_NONE		= 0,
	IECM_RX_PTYPE_TUNNEL_IP_IP		= 1,
	IECM_RX_PTYPE_TUNNEL_IP_GRENAT		= 2,
	IECM_RX_PTYPE_TUNNEL_IP_GRENAT_MAC	= 3,
	IECM_RX_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN	= 4,
};

enum iecm_rx_ptype_tunnel_end_prot {
	IECM_RX_PTYPE_TUNNEL_END_NONE	= 0,
	IECM_RX_PTYPE_TUNNEL_END_IPV4	= 1,
	IECM_RX_PTYPE_TUNNEL_END_IPV6	= 2,
};

enum iecm_rx_ptype_inner_prot {
	IECM_RX_PTYPE_INNER_PROT_NONE		= 0,
	IECM_RX_PTYPE_INNER_PROT_UDP		= 1,
	IECM_RX_PTYPE_INNER_PROT_TCP		= 2,
	IECM_RX_PTYPE_INNER_PROT_SCTP		= 3,
	IECM_RX_PTYPE_INNER_PROT_ICMP		= 4,
	IECM_RX_PTYPE_INNER_PROT_TIMESYNC	= 5,
};

enum iecm_rx_ptype_payload_layer {
	IECM_RX_PTYPE_PAYLOAD_LAYER_NONE	= 0,
	IECM_RX_PTYPE_PAYLOAD_LAYER_PAY2	= 1,
	IECM_RX_PTYPE_PAYLOAD_LAYER_PAY3	= 2,
	IECM_RX_PTYPE_PAYLOAD_LAYER_PAY4	= 3,
};

struct iecm_rx_ptype_decoded {
	u32 ptype:10;
	u32 known:1;
	u32 outer_ip:1;
	u32 outer_ip_ver:2;
	u32 outer_frag:1;
	u32 tunnel_type:3;
	u32 tunnel_end_prot:2;
	u32 tunnel_end_frag:1;
	u32 inner_prot:4;
	u32 payload_layer:3;
};

enum iecm_rx_hsplit {
	IECM_RX_NO_HDR_SPLIT = 0,
	IECM_RX_HDR_SPLIT = 1,
};

/* The iecm_ptype_lkup table is used to convert from the 10-bit ptype in the
 * hardware to a bit-field that can be used by SW to more easily determine the
 * packet type.
 *
 * Macros are used to shorten the table lines and make this table human
 * readable.
 *
 * We store the PTYPE in the top byte of the bit field - this is just so that
 * we can check that the table doesn't have a row missing, as the index into
 * the table should be the PTYPE.
 *
 * Typical work flow:
 *
 * IF NOT iecm_ptype_lkup[ptype].known
 * THEN
 *      Packet is unknown
 * ELSE IF iecm_ptype_lkup[ptype].outer_ip == IECM_RX_PTYPE_OUTER_IP
 *      Use the rest of the fields to look at the tunnels, inner protocols, etc
 * ELSE
 *      Use the enum iecm_rx_ptype_l2 to decode the packet type
 * ENDIF
 */
/* macro to make the table lines short */
#define IECM_PTT(PTYPE, OUTER_IP, OUTER_IP_VER, OUTER_FRAG, T, TE, TEF, I, PL)\
	{	PTYPE, \
		1, \
		IECM_RX_PTYPE_OUTER_##OUTER_IP, \
		IECM_RX_PTYPE_OUTER_##OUTER_IP_VER, \
		IECM_RX_PTYPE_##OUTER_FRAG, \
		IECM_RX_PTYPE_TUNNEL_##T, \
		IECM_RX_PTYPE_TUNNEL_END_##TE, \
		IECM_RX_PTYPE_##TEF, \
		IECM_RX_PTYPE_INNER_PROT_##I, \
		IECM_RX_PTYPE_PAYLOAD_LAYER_##PL }

#define IECM_PTT_UNUSED_ENTRY(PTYPE) { PTYPE, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/* shorter macros makes the table fit but are terse */
#define IECM_RX_PTYPE_NOF		IECM_RX_PTYPE_NOT_FRAG
#define IECM_RX_PTYPE_FRG		IECM_RX_PTYPE_FRAG
#define IECM_RX_PTYPE_INNER_PROT_TS	IECM_RX_PTYPE_INNER_PROT_TIMESYNC
#define IECM_RX_SUPP_PTYPE		18
#define IECM_RX_MAX_PTYPE		1024
#define IECM_RX_MAX_BASE_PTYPE		256

#define IECM_INT_NAME_STR_LEN	(IFNAMSIZ + 16)

/* Lookup table mapping the HW PTYPE to the bit field for decoding */
extern const struct iecm_rx_ptype_decoded iecm_ptype_lookup[IECM_RX_MAX_PTYPE];

enum iecm_queue_flags_t {
	__IECM_Q_GEN_CHK,
	__IECM_RFLQ_GEN_CHK,
	__IECM_Q_FLOW_SCH_EN,
	__IECM_Q_ETF_EN,
	__IECM_Q_SW_MARKER,
	__IECM_Q_VLAN_TAG_LOC_L2TAG1,
	__IECM_Q_VLAN_TAG_LOC_L2TAG2,
	__IECM_Q_FLAGS_NBITS,
};

struct iecm_vec_regs {
	u32 dyn_ctl_reg;
	u32 itrn_reg;
};

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

struct iecm_rx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 rsc_pkts;
};

struct iecm_tx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 lso_pkts;
};

union iecm_queue_stats {
	struct iecm_rx_queue_stats rx;
	struct iecm_tx_queue_stats tx;
};

#define IECM_ITR_DYNAMIC	1
#define IECM_ITR_MAX		0x1FE0
#define IECM_ITR_20K		0x0032
#define IECM_ITR_GRAN_S		1	/* Assume ITR granularity is 2us */
#define IECM_ITR_MASK		0x1FFE	/* ITR register value alignment mask */
#define ITR_REG_ALIGN(setting)	((setting) & IECM_ITR_MASK)
#define IECM_ITR_IS_DYNAMIC(itr_mode) (itr_mode)
#define IECM_ITR_TX_DEF		IECM_ITR_20K
#define IECM_ITR_RX_DEF		IECM_ITR_20K

/* queue associated with a vport */
struct iecm_queue {
	struct device *dev;		/* Used for DMA mapping */
	struct iecm_vport *vport;	/* Backreference to associated vport */
	union {
		struct iecm_txq_group *txq_grp;
		struct iecm_rxq_group *rxq_grp;
	};
	/* bufq: Used as group id, either 0 or 1, on clean Buf Q uses this
	 *       index to determine which group of refill queues to clean.
	 *       Bufqs are use in splitq only.
	 * txq: Index to map between Tx Q group and hot path Tx ptrs stored in
	 *      vport.  Used in both single Q/split Q
	 * rxq: Index to total rxq across groups, used for skb reporting
	 */
	u16 idx;
	/* Used for both Q models single and split. In split Q model relevant
	 * only to Tx Q and Rx Q
	 */
	u8 __iomem *tail;
	/* Used in both single and split Q.  In single Q, Tx Q uses tx_buf and
	 * Rx Q uses rx_buf.  In split Q, Tx Q uses tx_buf, Rx Q uses skb, and
	 * Buf Q uses rx_buf.
	 */
	union {
		struct iecm_tx_buf *tx_buf;
		struct {
			struct iecm_rx_buf *buf;
			struct iecm_dma_mem **hdr_buf;
		} rx_buf;
		struct sk_buff *skb;
	};
	u16 q_type;
	/* Queue id(Tx/Tx compl/Rx/Bufq) */
	u32 q_id;
	u16 desc_count;		/* Number of descriptors */

	/* Relevant in both split & single Tx Q & Buf Q*/
	u16 next_to_use;
	/* In split q model only relevant for Tx Compl Q and Rx Q */
	u16 next_to_clean;	/* used in interrupt processing */
	/* Used only for Rx. In split Q model only relevant to Rx Q */
	u16 next_to_alloc;
	/* Generation bit check stored, as HW flips the bit at Queue end */
	DECLARE_BITMAP(flags, __IECM_Q_FLAGS_NBITS);

	union iecm_queue_stats q_stats;
	struct u64_stats_sync stats_sync;

	bool rx_hsplit_en;

	u16 rx_hbuf_size;	/* Header buffer size */
	u16 rx_buf_size;
	u16 rx_max_pkt_size;
	u16 rx_buf_stride;
	u8 rx_buffer_low_watermark;
	u64 rxdids;
	/* Used for both Q models single and split. In split Q model relavant
	 * only to Tx compl Q and Rx compl Q
	 */
	struct iecm_q_vector *q_vector;	/* Backreference to associated vector */
	unsigned int size;		/* length of descriptor ring in bytes */
	dma_addr_t dma;			/* physical address of ring */
	void *desc_ring;		/* Descriptor ring memory */

	struct iecm_buf_lifo buf_stack; /* Stack of empty buffers to store
					 * buffer info for out of order
					 * buffer completions
					 */
	u16 tx_buf_key;			/* 16 bit unique "identifier" (index)
					 * to be used as the completion tag when
					 * queue is using flow based scheduling
					 */
	u16 tx_max_bufs;		/* Max buffers that can be transmitted
					 * with scatter-gather
					 */
	DECLARE_HASHTABLE(sched_buf_hash, 12);

	struct iecm_rx_hdr_buf_pages hbuf_pages;
} ____cacheline_internodealigned_in_smp;

/* Software queues are used in splitq mode to manage buffers between rxq
 * producer and the bufq consumer.  These are required in order to maintain a
 * lockless buffer management system and are strictly software only constructs.
 */
struct iecm_sw_queue {
	u16 next_to_clean ____cacheline_aligned_in_smp;
	u16 next_to_alloc ____cacheline_aligned_in_smp;
	u16 next_to_use ____cacheline_aligned_in_smp;
	DECLARE_BITMAP(flags, __IECM_Q_FLAGS_NBITS)
		____cacheline_aligned_in_smp;
	u16 *ring ____cacheline_aligned_in_smp;
	u16 desc_count;
	u16 buf_size;
	struct device *dev;
} ____cacheline_internodealigned_in_smp;

/* Splitq only.  iecm_rxq_set associates an rxq with at an array of refillqs.
 * Each rxq needs a refillq to return used buffers back to the respective bufq.
 * Bufqs then clean these refillqs for buffers to give to hardware.
 */
struct iecm_rxq_set {
	struct iecm_queue rxq;
	/* refillqs assoc with bufqX mapped to this rxq */
	struct iecm_sw_queue *refillq0;
	struct iecm_sw_queue *refillq1;
};

/* Splitq only.  iecm_bufq_set associates a bufq to an array of refillqs.
 * In this bufq_set, there will be one refillq for each rxq in this rxq_group.
 * Used buffers received by rxqs will be put on refillqs which bufqs will
 * clean to return new buffers back to hardware.
 *
 * Buffers needed by some number of rxqs associated in this rxq_group are
 * managed by at most two bufqs (depending on performance configuration).
 */
struct iecm_bufq_set {
	struct iecm_queue bufq;
	/* This is always equal to num_rxq_sets in iecm_rxq_group */
	int num_refillqs;
	struct iecm_sw_queue *refillqs;
};

/* In singleq mode, an rxq_group is simply an array of rxqs.  In splitq, a
 * rxq_group contains all the rxqs, bufqs and refillqs needed to
 * manage buffers in splitq mode.
 */
struct iecm_rxq_group {
	struct iecm_vport *vport; /* back pointer */

	union {
		struct {
			int num_rxq;
			/* store queue pointers */
			struct iecm_queue *rxqs[IECM_LARGE_MAX_Q];
		} singleq;
		struct {
			int num_rxq_sets;
			/* store queue pointers */
			struct iecm_rxq_set *rxq_sets[IECM_LARGE_MAX_Q];
			struct iecm_bufq_set *bufq_sets;
		} splitq;
	};
};

/* Between singleq and splitq, a txq_group is largely the same except for the
 * complq.  In splitq a single complq is responsible for handling completions
 * for some number of txqs associated in this txq_group.
 */
struct iecm_txq_group {
	struct iecm_vport *vport; /* back pointer */

	int num_txq;
	/* store queue pointers */
	struct iecm_queue *txqs[IECM_LARGE_MAX_Q];

	/* splitq only */
	struct iecm_queue *complq;
};

struct iecm_adapter;

int iecm_vport_singleq_napi_poll(struct napi_struct *napi, int budget);
void iecm_vport_init_num_qs(struct iecm_vport *vport,
			    struct virtchnl2_create_vport *vport_msg);
void iecm_vport_calc_num_q_desc(struct iecm_vport *vport);
void iecm_vport_calc_total_qs(struct iecm_adapter *adapter,
			      struct virtchnl2_create_vport *vport_msg);
void iecm_vport_calc_num_q_groups(struct iecm_vport *vport);
int iecm_vport_queues_alloc(struct iecm_vport *vport);
void iecm_vport_queues_rel(struct iecm_vport *vport);
void iecm_vport_calc_num_q_vec(struct iecm_vport *vport);
void iecm_vport_intr_rel(struct iecm_vport *vport);
int iecm_vport_intr_alloc(struct iecm_vport *vport);
void iecm_vport_intr_dis_irq_all(struct iecm_vport *vport);
void iecm_vport_intr_clear_dflt_itr(struct iecm_vport *vport);
void iecm_vport_intr_update_itr_ena_irq(struct iecm_q_vector *q_vector);
void iecm_vport_intr_deinit(struct iecm_vport *vport);
int iecm_vport_intr_init(struct iecm_vport *vport);
irqreturn_t
iecm_vport_intr_clean_queues(int __always_unused irq, void *data);
void iecm_vport_intr_ena_irq_all(struct iecm_vport *vport);
int iecm_config_rss(struct iecm_vport *vport);
void iecm_fill_dflt_rss_lut(struct iecm_vport *vport);
int iecm_init_rss(struct iecm_vport *vport);
void iecm_deinit_rss(struct iecm_vport *vport);
bool iecm_init_rx_buf_hw_alloc(struct iecm_queue *rxq, struct iecm_rx_buf *buf);
void iecm_rx_buf_hw_update(struct iecm_queue *rxq, u32 val);
void iecm_tx_buf_rel(struct iecm_queue *tx_q, struct iecm_tx_buf *tx_buf);
bool iecm_rx_singleq_buf_hw_alloc_all(struct iecm_queue *rxq,
				      u16 cleaned_count);
#endif /* !_IECM_TXRX_H_ */
