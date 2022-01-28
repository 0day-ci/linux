/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020, Intel Corporation. */

#ifndef _IECM_LAN_TXRX_H_
#define _IECM_LAN_TXRX_H_

enum iecm_rss_hash {
	/* Values 0 - 28 are reserved for future use */
	IECM_HASH_INVALID		= 0,
	IECM_HASH_NONF_UNICAST_IPV4_UDP	= 29,
	IECM_HASH_NONF_MULTICAST_IPV4_UDP,
	IECM_HASH_NONF_IPV4_UDP,
	IECM_HASH_NONF_IPV4_TCP_SYN_NO_ACK,
	IECM_HASH_NONF_IPV4_TCP,
	IECM_HASH_NONF_IPV4_SCTP,
	IECM_HASH_NONF_IPV4_OTHER,
	IECM_HASH_FRAG_IPV4,
	/* Values 37-38 are reserved */
	IECM_HASH_NONF_UNICAST_IPV6_UDP	= 39,
	IECM_HASH_NONF_MULTICAST_IPV6_UDP,
	IECM_HASH_NONF_IPV6_UDP,
	IECM_HASH_NONF_IPV6_TCP_SYN_NO_ACK,
	IECM_HASH_NONF_IPV6_TCP,
	IECM_HASH_NONF_IPV6_SCTP,
	IECM_HASH_NONF_IPV6_OTHER,
	IECM_HASH_FRAG_IPV6,
	IECM_HASH_NONF_RSVD47,
	IECM_HASH_NONF_FCOE_OX,
	IECM_HASH_NONF_FCOE_RX,
	IECM_HASH_NONF_FCOE_OTHER,
	/* Values 51-62 are reserved */
	IECM_HASH_L2_PAYLOAD		= 63,
	IECM_HASH_MAX
};

/* Supported RSS offloads */
#define IECM_DEFAULT_RSS_HASH ( \
	BIT_ULL(IECM_HASH_NONF_IPV4_UDP) | \
	BIT_ULL(IECM_HASH_NONF_IPV4_SCTP) | \
	BIT_ULL(IECM_HASH_NONF_IPV4_TCP) | \
	BIT_ULL(IECM_HASH_NONF_IPV4_OTHER) | \
	BIT_ULL(IECM_HASH_FRAG_IPV4) | \
	BIT_ULL(IECM_HASH_NONF_IPV6_UDP) | \
	BIT_ULL(IECM_HASH_NONF_IPV6_TCP) | \
	BIT_ULL(IECM_HASH_NONF_IPV6_SCTP) | \
	BIT_ULL(IECM_HASH_NONF_IPV6_OTHER) | \
	BIT_ULL(IECM_HASH_FRAG_IPV6) | \
	BIT_ULL(IECM_HASH_L2_PAYLOAD))

	/* TODO: Wrap belwo comment under internal flag
	 * Below 6 pcktypes are not supported by FVL or older products
	 * They are supported by FPK and future products
	 */
#define IECM_DEFAULT_RSS_HASH_EXPANDED (IECM_DEFAULT_RSS_HASH | \
	BIT_ULL(IECM_HASH_NONF_IPV4_TCP_SYN_NO_ACK) | \
	BIT_ULL(IECM_HASH_NONF_UNICAST_IPV4_UDP) | \
	BIT_ULL(IECM_HASH_NONF_MULTICAST_IPV4_UDP) | \
	BIT_ULL(IECM_HASH_NONF_IPV6_TCP_SYN_NO_ACK) | \
	BIT_ULL(IECM_HASH_NONF_UNICAST_IPV6_UDP) | \
	BIT_ULL(IECM_HASH_NONF_MULTICAST_IPV6_UDP))

/* For iecm_splitq_base_tx_compl_desc */
#define IECM_TXD_COMPLQ_GEN_S	15
#define IECM_TXD_COMPLQ_GEN_M		BIT_ULL(IECM_TXD_COMPLQ_GEN_S)
#define IECM_TXD_COMPLQ_COMPL_TYPE_S	11
#define IECM_TXD_COMPLQ_COMPL_TYPE_M	\
	MAKEMASK(0x7UL, IECM_TXD_COMPLQ_COMPL_TYPE_S)
#define IECM_TXD_COMPLQ_QID_S	0
#define IECM_TXD_COMPLQ_QID_M		MAKEMASK(0x3FFUL, IECM_TXD_COMPLQ_QID_S)

/* For base mode TX descriptors */
#define IECM_TXD_CTX_QW1_MSS_S		50
#define IECM_TXD_CTX_QW1_MSS_M		\
	MAKEMASK(0x3FFFULL, IECM_TXD_CTX_QW1_MSS_S)
#define IECM_TXD_CTX_QW1_TSO_LEN_S	30
#define IECM_TXD_CTX_QW1_TSO_LEN_M	\
	MAKEMASK(0x3FFFFULL, IECM_TXD_CTX_QW1_TSO_LEN_S)
#define IECM_TXD_CTX_QW1_CMD_S		4
#define IECM_TXD_CTX_QW1_CMD_M		\
	MAKEMASK(0xFFFUL, IECM_TXD_CTX_QW1_CMD_S)
#define IECM_TXD_CTX_QW1_DTYPE_S	0
#define IECM_TXD_CTX_QW1_DTYPE_M	\
	MAKEMASK(0xFUL, IECM_TXD_CTX_QW1_DTYPE_S)
#define IECM_TXD_QW1_L2TAG1_S		48
#define IECM_TXD_QW1_L2TAG1_M		\
	MAKEMASK(0xFFFFULL, IECM_TXD_QW1_L2TAG1_S)
#define IECM_TXD_QW1_TX_BUF_SZ_S	34
#define IECM_TXD_QW1_TX_BUF_SZ_M	\
	MAKEMASK(0x3FFFULL, IECM_TXD_QW1_TX_BUF_SZ_S)
#define IECM_TXD_QW1_OFFSET_S		16
#define IECM_TXD_QW1_OFFSET_M		\
	MAKEMASK(0x3FFFFULL, IECM_TXD_QW1_OFFSET_S)
#define IECM_TXD_QW1_CMD_S		4
#define IECM_TXD_QW1_CMD_M		MAKEMASK(0xFFFUL, IECM_TXD_QW1_CMD_S)
#define IECM_TXD_QW1_DTYPE_S		0
#define IECM_TXD_QW1_DTYPE_M		MAKEMASK(0xFUL, IECM_TXD_QW1_DTYPE_S)

/* TX Completion Descriptor Completion Types */
#define IECM_TXD_COMPLT_ITR_FLUSH	0
#define IECM_TXD_COMPLT_RULE_MISS	1
#define IECM_TXD_COMPLT_RS		2
#define IECM_TXD_COMPLT_REINJECTED	3
#define IECM_TXD_COMPLT_RE		4
#define IECM_TXD_COMPLT_SW_MARKER	5

enum iecm_tx_desc_dtype_value {
	IECM_TX_DESC_DTYPE_DATA				= 0,
	IECM_TX_DESC_DTYPE_CTX				= 1,
	IECM_TX_DESC_DTYPE_REINJECT_CTX			= 2,
	IECM_TX_DESC_DTYPE_FLEX_DATA			= 3,
	IECM_TX_DESC_DTYPE_FLEX_CTX			= 4,
	IECM_TX_DESC_DTYPE_FLEX_TSO_CTX			= 5,
	IECM_TX_DESC_DTYPE_FLEX_TSYN_L2TAG1		= 6,
	IECM_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2		= 7,
	IECM_TX_DESC_DTYPE_FLEX_TSO_L2TAG2_PARSTAG_CTX	= 8,
	IECM_TX_DESC_DTYPE_FLEX_HOSTSPLIT_SA_TSO_CTX	= 9,
	IECM_TX_DESC_DTYPE_FLEX_HOSTSPLIT_SA_CTX	= 10,
	IECM_TX_DESC_DTYPE_FLEX_L2TAG2_CTX		= 11,
	IECM_TX_DESC_DTYPE_FLEX_FLOW_SCHE		= 12,
	IECM_TX_DESC_DTYPE_FLEX_HOSTSPLIT_TSO_CTX	= 13,
	IECM_TX_DESC_DTYPE_FLEX_HOSTSPLIT_CTX		= 14,
	/* DESC_DONE - HW has completed write-back of descriptor */
	IECM_TX_DESC_DTYPE_DESC_DONE			= 15,
};

enum iecm_tx_ctx_desc_cmd_bits {
	IECM_TX_CTX_DESC_TSO		= 0x01,
	IECM_TX_CTX_DESC_TSYN		= 0x02,
	IECM_TX_CTX_DESC_IL2TAG2	= 0x04,
	IECM_TX_CTX_DESC_RSVD		= 0x08,
	IECM_TX_CTX_DESC_SWTCH_NOTAG	= 0x00,
	IECM_TX_CTX_DESC_SWTCH_UPLINK	= 0x10,
	IECM_TX_CTX_DESC_SWTCH_LOCAL	= 0x20,
	IECM_TX_CTX_DESC_SWTCH_VSI	= 0x30,
	IECM_TX_CTX_DESC_FILT_AU_EN	= 0x40,
	IECM_TX_CTX_DESC_FILT_AU_EVICT	= 0x80,
	IECM_TX_CTX_DESC_RSVD1		= 0xF00
};

enum iecm_tx_desc_len_fields {
	/* Note: These are predefined bit offsets */
	IECM_TX_DESC_LEN_MACLEN_S	= 0, /* 7 BITS */
	IECM_TX_DESC_LEN_IPLEN_S	= 7, /* 7 BITS */
	IECM_TX_DESC_LEN_L4_LEN_S	= 14 /* 4 BITS */
};

enum iecm_tx_base_desc_cmd_bits {
	IECM_TX_DESC_CMD_EOP			= 0x0001,
	IECM_TX_DESC_CMD_RS			= 0x0002,
	 /* only on VFs else RSVD */
	IECM_TX_DESC_CMD_ICRC			= 0x0004,
	IECM_TX_DESC_CMD_IL2TAG1		= 0x0008,
	IECM_TX_DESC_CMD_RSVD1			= 0x0010,
	IECM_TX_DESC_CMD_IIPT_NONIP		= 0x0000, /* 2 BITS */
	IECM_TX_DESC_CMD_IIPT_IPV6		= 0x0020, /* 2 BITS */
	IECM_TX_DESC_CMD_IIPT_IPV4		= 0x0040, /* 2 BITS */
	IECM_TX_DESC_CMD_IIPT_IPV4_CSUM		= 0x0060, /* 2 BITS */
	IECM_TX_DESC_CMD_RSVD2			= 0x0080,
	IECM_TX_DESC_CMD_L4T_EOFT_UNK		= 0x0000, /* 2 BITS */
	IECM_TX_DESC_CMD_L4T_EOFT_TCP		= 0x0100, /* 2 BITS */
	IECM_TX_DESC_CMD_L4T_EOFT_SCTP		= 0x0200, /* 2 BITS */
	IECM_TX_DESC_CMD_L4T_EOFT_UDP		= 0x0300, /* 2 BITS */
	IECM_TX_DESC_CMD_RSVD3			= 0x0400,
	IECM_TX_DESC_CMD_RSVD4			= 0x0800,
};

/* Transmit descriptors  */
/* splitq tx buf, singleq tx buf and singleq compl desc */
struct iecm_base_tx_desc {
	__le64 buf_addr; /* Address of descriptor's data buf */
	__le64 qw1; /* type_cmd_offset_bsz_l2tag1 */
};/* read used with buffer queues*/

struct iecm_splitq_tx_compl_desc {
	/* qid=[10:0] comptype=[13:11] rsvd=[14] gen=[15] */
	__le16 qid_comptype_gen;
	union {
		__le16 q_head; /* Queue head */
		__le16 compl_tag; /* Completion tag */
	} q_head_compl_tag;
	u32 rsvd;

};/* writeback used with completion queues*/

/* Context descriptors */
struct iecm_base_tx_ctx_desc {
	struct {
		__le32 rsvd0;
		__le16 l2tag2;
		__le16 rsvd1;
	} qw0;
	__le64 qw1; /* type_cmd_tlen_mss/rt_hint */
};

/* Common cmd field defines for all desc except Flex Flow Scheduler (0x0C) */
enum iecm_tx_flex_desc_cmd_bits {
	IECM_TX_FLEX_DESC_CMD_EOP			= 0x01,
	IECM_TX_FLEX_DESC_CMD_RS			= 0x02,
	IECM_TX_FLEX_DESC_CMD_RE			= 0x04,
	IECM_TX_FLEX_DESC_CMD_IL2TAG1			= 0x08,
	IECM_TX_FLEX_DESC_CMD_DUMMY			= 0x10,
	IECM_TX_FLEX_DESC_CMD_CS_EN			= 0x20,
	IECM_TX_FLEX_DESC_CMD_FILT_AU_EN		= 0x40,
	IECM_TX_FLEX_DESC_CMD_FILT_AU_EVICT		= 0x80,
};

struct iecm_flex_tx_desc {
	__le64 buf_addr;	/* Packet buffer address */
	struct {
		__le16 cmd_dtype;
#define IECM_FLEX_TXD_QW1_DTYPE_S		0
#define IECM_FLEX_TXD_QW1_DTYPE_M		\
		MAKEMASK(0x1FUL, IECM_FLEX_TXD_QW1_DTYPE_S)
#define IECM_FLEX_TXD_QW1_CMD_S		5
#define IECM_FLEX_TXD_QW1_CMD_M		MAKEMASK(0x7FFUL, IECM_TXD_QW1_CMD_S)
		union {
			/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_DATA_(0x03) */
			u8 raw[4];

			/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_TSYN_L2TAG1 (0x06) */
			struct {
				__le16 l2tag1;
				u8 flex;
				u8 tsync;
			} tsync;

			/* DTYPE=IECM_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2 (0x07) */
			struct {
				__le16 l2tag1;
				__le16 l2tag2;
			} l2tags;
		} flex;
		__le16 buf_size;
	} qw1;
};

struct iecm_flex_tx_sched_desc {
	__le64 buf_addr;	/* Packet buffer address */

	/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_FLOW_SCHE_16B (0x0C) */
	struct {
		u8 cmd_dtype;
#define IECM_TXD_FLEX_FLOW_DTYPE_M	0x1F
#define IECM_TXD_FLEX_FLOW_CMD_EOP	0x20
#define IECM_TXD_FLEX_FLOW_CMD_CS_EN	0x40
#define IECM_TXD_FLEX_FLOW_CMD_RE	0x80

		u8 rsvd[3];

		__le16 compl_tag;
		__le16 rxr_bufsize;
#define IECM_TXD_FLEX_FLOW_RXR		0x4000
#define IECM_TXD_FLEX_FLOW_BUFSIZE_M	0x3FFF
	} qw1;
};

/* Common cmd fields for all flex context descriptors
 * Note: these defines already account for the 5 bit dtype in the cmd_dtype
 * field
 */
enum iecm_tx_flex_ctx_desc_cmd_bits {
	IECM_TX_FLEX_CTX_DESC_CMD_TSO			= 0x0020,
	IECM_TX_FLEX_CTX_DESC_CMD_TSYN_EN		= 0x0040,
	IECM_TX_FLEX_CTX_DESC_CMD_L2TAG2		= 0x0080,
	IECM_TX_FLEX_CTX_DESC_CMD_SWTCH_UPLNK		= 0x0200, /* 2 bits */
	IECM_TX_FLEX_CTX_DESC_CMD_SWTCH_LOCAL		= 0x0400, /* 2 bits */
	IECM_TX_FLEX_CTX_DESC_CMD_SWTCH_TARGETVSI	= 0x0600, /* 2 bits */
};

/* Standard flex descriptor TSO context quad word */
struct iecm_flex_tx_tso_ctx_qw {
	__le32 flex_tlen;
#define IECM_TXD_FLEX_CTX_TLEN_M	0x1FFFF
#define IECM_TXD_FLEX_TSO_CTX_FLEX_S	24
	__le16 mss_rt;
#define IECM_TXD_FLEX_CTX_MSS_RT_M	0x3FFF
	u8 hdr_len;
	u8 flex;
};

union iecm_flex_tx_ctx_desc {
	/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_CTX (0x04) */
	struct {
		u8 qw0_flex[8];
		struct {
			__le16 cmd_dtype;
			__le16 l2tag1;
			u8 qw1_flex[4];
		} qw1;
	} gen;

	/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_TSO_CTX (0x05) */
	struct {
		struct iecm_flex_tx_tso_ctx_qw qw0;
		struct {
			__le16 cmd_dtype;
			u8 flex[6];
		} qw1;
	} tso;

	/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_TSO_L2TAG2_PARSTAG_CTX (0x08) */
	struct {
		struct iecm_flex_tx_tso_ctx_qw qw0;
		struct {
			__le16 cmd_dtype;
			__le16 l2tag2;
			u8 flex0;
			u8 ptag;
			u8 flex1[2];
		} qw1;
	} tso_l2tag2_ptag;

	/* DTYPE = IECM_TX_DESC_DTYPE_FLEX_L2TAG2_CTX (0x0B) */
	struct {
		u8 qw0_flex[8];
		struct {
			__le16 cmd_dtype;
			__le16 l2tag2;
			u8 flex[4];
		} qw1;
	} l2tag2;

	/* DTYPE = IECM_TX_DESC_DTYPE_REINJECT_CTX (0x02) */
	struct {
		struct {
			__le32 sa_domain;
#define IECM_TXD_FLEX_CTX_SA_DOM_M	0xFFFF
#define IECM_TXD_FLEX_CTX_SA_DOM_VAL	0x10000
			__le32 sa_idx;
#define IECM_TXD_FLEX_CTX_SAIDX_M	0x1FFFFF
		} qw0;
		struct {
			__le16 cmd_dtype;
			__le16 txr2comp;
#define IECM_TXD_FLEX_CTX_TXR2COMP	0x1
			__le16 miss_txq_comp_tag;
			__le16 miss_txq_id;
		} qw1;
	} reinjection_pkt;
};

/* Host Split Context Descriptors */
struct iecm_flex_tx_hs_ctx_desc {
	union {
		struct {
			__le32 host_fnum_tlen;
#define IECM_TXD_FLEX_CTX_TLEN_S	0
#define IECM_TXD_FLEX_CTX_TLEN_M	0x1FFFF
#define IECM_TXD_FLEX_CTX_FNUM_S	18
#define IECM_TXD_FLEX_CTX_FNUM_M	0x7FF
#define IECM_TXD_FLEX_CTX_HOST_S	29
#define IECM_TXD_FLEX_CTX_HOST_M	0x7
			__le16 ftype_mss_rt;
#define IECM_TXD_FLEX_CTX_MSS_RT_0	0
#define IECM_TXD_FLEX_CTX_MSS_RT_M	0x3FFF
#define IECM_TXD_FLEX_CTX_FTYPE_S	14
#define IECM_TXD_FLEX_CTX_FTYPE_VF	MAKEMASK(0x0, IECM_TXD_FLEX_CTX_FTYPE_S)
#define IECM_TXD_FLEX_CTX_FTYPE_VDEV	MAKEMASK(0x1, IECM_TXD_FLEX_CTX_FTYPE_S)
#define IECM_TXD_FLEX_CTX_FTYPE_PF	MAKEMASK(0x2, IECM_TXD_FLEX_CTX_FTYPE_S)
			u8 hdr_len;
			u8 ptag;
		} tso;
		struct {
			u8 flex0[2];
			__le16 host_fnum_ftype;
			u8 flex1[3];
			u8 ptag;
		} no_tso;
	} qw0;

	__le64 qw1_cmd_dtype;
#define IECM_TXD_FLEX_CTX_QW1_PASID_S		16
#define IECM_TXD_FLEX_CTX_QW1_PASID_M		0xFFFFF
#define IECM_TXD_FLEX_CTX_QW1_PASID_VALID_S	36
#define IECM_TXD_FLEX_CTX_QW1_PASID_VALID	\
		MAKEMASK(0x1, IECM_TXD_FLEX_CTX_PASID_VALID_S)
#define IECM_TXD_FLEX_CTX_QW1_TPH_S		37
#define IECM_TXD_FLEX_CTX_QW1_TPH \
		MAKEMASK(0x1, IECM_TXD_FLEX_CTX_TPH_S)
#define IECM_TXD_FLEX_CTX_QW1_PFNUM_S		38
#define IECM_TXD_FLEX_CTX_QW1_PFNUM_M		0xF
/* The following are only valid for DTYPE = 0x09 and DTYPE = 0x0A */
#define IECM_TXD_FLEX_CTX_QW1_SAIDX_S		42
#define IECM_TXD_FLEX_CTX_QW1_SAIDX_M		0x1FFFFF
#define IECM_TXD_FLEX_CTX_QW1_SAIDX_VAL_S	63
#define IECM_TXD_FLEX_CTX_QW1_SAIDX_VALID	\
		MAKEMASK(0x1, IECM_TXD_FLEX_CTX_QW1_SAIDX_VAL_S)
/* The following are only valid for DTYPE = 0x0D and DTYPE = 0x0E */
#define IECM_TXD_FLEX_CTX_QW1_FLEX0_S		48
#define IECM_TXD_FLEX_CTX_QW1_FLEX0_M		0xFF
#define IECM_TXD_FLEX_CTX_QW1_FLEX1_S		56
#define IECM_TXD_FLEX_CTX_QW1_FLEX1_M		0xFF
};
#endif /* _IECM_LAN_TXRX_H_ */
