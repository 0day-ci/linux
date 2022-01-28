/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020, Intel Corporation. */

#ifndef _IECM_CONTROLQ_H_
#define _IECM_CONTROLQ_H_

#include <linux/slab.h>

#include "iecm_controlq_api.h"

/* Maximum buffer lengths for all control queue types */
#define IECM_CTLQ_MAX_RING_SIZE 1024
#define IECM_CTLQ_MAX_BUF_LEN	4096

#define IECM_CTLQ_DESC(R, i) \
	(&(((struct iecm_ctlq_desc *)((R)->desc_ring.va))[i]))

#define IECM_CTLQ_DESC_UNUSED(R) \
	((u16)((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->ring_size) + \
	      (R)->next_to_clean - (R)->next_to_use - 1))

/* Control Queue default settings */
#define IECM_CTRL_SQ_CMD_TIMEOUT	250  /* msecs */

struct iecm_ctlq_desc {
	__le16	flags;
	__le16	opcode;
	__le16	datalen;	/* 0 for direct commands */
	union {
		__le16 ret_val;
		__le16 pfid_vfid;
#define IECM_CTLQ_DESC_VF_ID_S	0
#define IECM_CTLQ_DESC_VF_ID_M	(0x7FF << IECM_CTLQ_DESC_VF_ID_S)
#define IECM_CTLQ_DESC_PF_ID_S	11
#define IECM_CTLQ_DESC_PF_ID_M	(0x1F << IECM_CTLQ_DESC_PF_ID_S)
	};
	__le32 cookie_high;
	__le32 cookie_low;
	union {
		struct {
			__le32 param0;
			__le32 param1;
			__le32 param2;
			__le32 param3;
		} direct;
		struct {
			__le32 param0;
			__le32 param1;
			__le32 addr_high;
			__le32 addr_low;
		} indirect;
		u8 raw[16];
	} params;
};

/* Flags sub-structure
 * |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |10 |11 |12 |13 |14 |15 |
 * |DD |CMP|ERR|  * RSV *  |FTYPE  | *RSV* |RD |VFC|BUF|  * RSV *  |
 */
/* command flags and offsets */
#define IECM_CTLQ_FLAG_DD_S	0
#define IECM_CTLQ_FLAG_CMP_S	1
#define IECM_CTLQ_FLAG_ERR_S	2
#define IECM_CTLQ_FLAG_FTYPE_S	6
#define IECM_CTLQ_FLAG_RD_S	10
#define IECM_CTLQ_FLAG_VFC_S	11
#define IECM_CTLQ_FLAG_BUF_S	12

#define IECM_CTLQ_FLAG_DD	BIT(IECM_CTLQ_FLAG_DD_S)	/* 0x1	  */
#define IECM_CTLQ_FLAG_CMP	BIT(IECM_CTLQ_FLAG_CMP_S)	/* 0x2	  */
#define IECM_CTLQ_FLAG_ERR	BIT(IECM_CTLQ_FLAG_ERR_S)	/* 0x4	  */
#define IECM_CTLQ_FLAG_FTYPE_VM	BIT(IECM_CTLQ_FLAG_FTYPE_S)	/* 0x40	  */
#define IECM_CTLQ_FLAG_FTYPE_PF	BIT(IECM_CTLQ_FLAG_FTYPE_S + 1)	/* 0x80   */
#define IECM_CTLQ_FLAG_RD	BIT(IECM_CTLQ_FLAG_RD_S)	/* 0x400  */
#define IECM_CTLQ_FLAG_VFC	BIT(IECM_CTLQ_FLAG_VFC_S)	/* 0x800  */
#define IECM_CTLQ_FLAG_BUF	BIT(IECM_CTLQ_FLAG_BUF_S)	/* 0x1000 */

struct iecm_mbxq_desc {
	u8 pad[8];		/* CTLQ flags/opcode/len/retval fields */
	u32 chnl_opcode;	/* avoid confusion with desc->opcode */
	u32 chnl_retval;	/* ditto for desc->retval */
	u32 pf_vf_id;		/* used by CP when sending to PF */
};

/* Define the APF hardware struct to replace other control structs as needed
 * Align to ctlq_hw_info
 */
struct iecm_hw {
	u8 __iomem *hw_addr;
	u64 hw_addr_len;
	void *back;

	/* control queue - send and receive */
	struct iecm_ctlq_info *asq;
	struct iecm_ctlq_info *arq;

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;
	bool adapter_stopped;

	struct list_head cq_list_head;
};

int iecm_ctlq_alloc_ring_res(struct iecm_hw *hw,
			     struct iecm_ctlq_info *cq);

void iecm_ctlq_dealloc_ring_res(struct iecm_hw *hw, struct iecm_ctlq_info *cq);

/* prototype for functions used for dynamic memory allocation */
void *iecm_alloc_dma_mem(struct iecm_hw *hw, struct iecm_dma_mem *mem,
			 u64 size);
void iecm_free_dma_mem(struct iecm_hw *hw, struct iecm_dma_mem *mem);
#endif /* _IECM_CONTROLQ_H_ */
