// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include <rdma/ib_verbs.h>

#include "erdma_hw.h"
#include "erdma_verbs.h"

static int erdma_cq_notempty(struct erdma_cq *cq)
{
	struct erdma_cqe *cqe;
	unsigned long flags;
	u32 hdr;

	spin_lock_irqsave(&cq->kern_cq.lock, flags);

	cqe = &cq->kern_cq.qbuf[cq->kern_cq.ci & (cq->depth - 1)];
	hdr = be32_to_cpu(READ_ONCE(cqe->hdr));
	if (FIELD_GET(ERDMA_CQE_HDR_OWNER_MASK, hdr) != cq->kern_cq.owner) {
		spin_unlock_irqrestore(&cq->kern_cq.lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&cq->kern_cq.lock, flags);
	return 1;
}

static void notify_cq(struct erdma_cq *cq, u8 solcitied)
{
	u64 db_data = FIELD_PREP(ERDMA_CQDB_EQN_MASK, cq->assoc_eqn) |
		      FIELD_PREP(ERDMA_CQDB_CQN_MASK, cq->cqn) |
		      FIELD_PREP(ERDMA_CQDB_ARM_MASK, 1) |
		      FIELD_PREP(ERDMA_CQDB_SOL_MASK, solcitied) |
		      FIELD_PREP(ERDMA_CQDB_CMDSN_MASK, cq->kern_cq.cmdsn) |
		      FIELD_PREP(ERDMA_CQDB_CI_MASK, cq->kern_cq.ci);

	*(u64 *)cq->kern_cq.db_info = db_data;
	writeq(db_data, cq->kern_cq.db);
}

int erdma_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	int ret = 0;

	notify_cq(cq, (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED);

	if (flags & IB_CQ_REPORT_MISSED_EVENTS)
		ret = erdma_cq_notempty(cq);

	return ret;
}

static const struct {
	enum erdma_opcode erdma;
	enum ib_wc_opcode base;
} map_cqe_opcode[ERDMA_NUM_OPCODES] = {
	{ ERDMA_OP_WRITE, IB_WC_RDMA_WRITE },
	{ ERDMA_OP_READ, IB_WC_RDMA_READ },
	{ ERDMA_OP_SEND, IB_WC_SEND },
	{ ERDMA_OP_SEND_WITH_IMM, IB_WC_SEND },
	{ ERDMA_OP_RECEIVE, IB_WC_RECV },
	{ ERDMA_OP_RECV_IMM, IB_WC_RECV_RDMA_WITH_IMM },
	{ ERDMA_OP_RECV_INV, IB_WC_LOCAL_INV }, /* confirm afterwards */
	{ ERDMA_OP_REQ_ERR, IB_WC_RECV }, /* remove afterwards */
	{ ERDNA_OP_READ_RESPONSE, IB_WC_RECV }, /* can not appear */
	{ ERDMA_OP_WRITE_WITH_IMM, IB_WC_RDMA_WRITE },
	{ ERDMA_OP_RECV_ERR, IB_WC_RECV_RDMA_WITH_IMM }, /* can not appear */
	{ ERDMA_OP_INVALIDATE, IB_WC_LOCAL_INV },
	{ ERDMA_OP_RSP_SEND_IMM, IB_WC_RECV },
	{ ERDMA_OP_SEND_WITH_INV, IB_WC_SEND },
	{ ERDMA_OP_REG_MR, IB_WC_REG_MR },
	{ ERDMA_OP_LOCAL_INV, IB_WC_LOCAL_INV },
	{ ERDMA_OP_READ_WITH_INV, IB_WC_RDMA_READ },
};

static const struct {
	enum erdma_wc_status erdma;
	enum ib_wc_status base;
	enum erdma_vendor_err vendor;
} map_cqe_status[ERDMA_NUM_WC_STATUS] = {
	{ ERDMA_WC_SUCCESS, IB_WC_SUCCESS, ERDMA_WC_VENDOR_NO_ERR },
	{ ERDMA_WC_GENERAL_ERR, IB_WC_GENERAL_ERR, ERDMA_WC_VENDOR_NO_ERR },
	{ ERDMA_WC_RECV_WQE_FORMAT_ERR, IB_WC_GENERAL_ERR, ERDMA_WC_VENDOR_INVALID_RQE },
	{ ERDMA_WC_RECV_STAG_INVALID_ERR, IB_WC_REM_ACCESS_ERR,
		ERDMA_WC_VENDOR_RQE_INVALID_STAG },
	{ ERDMA_WC_RECV_ADDR_VIOLATION_ERR, IB_WC_REM_ACCESS_ERR,
		ERDMA_WC_VENDOR_RQE_ADDR_VIOLATION },
	{ ERDMA_WC_RECV_RIGHT_VIOLATION_ERR, IB_WC_REM_ACCESS_ERR,
		ERDMA_WC_VENDOR_RQE_ACCESS_RIGHT_ERR },
	{ ERDMA_WC_RECV_PDID_ERR, IB_WC_REM_ACCESS_ERR, ERDMA_WC_VENDOR_RQE_INVALID_PD },
	{ ERDMA_WC_RECV_WARRPING_ERR, IB_WC_REM_ACCESS_ERR, ERDMA_WC_VENDOR_RQE_WRAP_ERR },
	{ ERDMA_WC_SEND_WQE_FORMAT_ERR, IB_WC_LOC_QP_OP_ERR, ERDMA_WC_VENDOR_INVALID_SQE },
	{ ERDMA_WC_SEND_WQE_ORD_EXCEED, IB_WC_GENERAL_ERR, ERDMA_WC_VENDOR_ZERO_ORD },
	{ ERDMA_WC_SEND_STAG_INVALID_ERR, IB_WC_LOC_ACCESS_ERR,
		ERDMA_WC_VENDOR_SQE_INVALID_STAG },
	{ ERDMA_WC_SEND_ADDR_VIOLATION_ERR, IB_WC_LOC_ACCESS_ERR,
			ERDMA_WC_VENDOR_SQE_ADDR_VIOLATION },
	{ ERDMA_WC_SEND_RIGHT_VIOLATION_ERR, IB_WC_LOC_ACCESS_ERR,
		ERDMA_WC_VENDOR_SQE_ACCESS_ERR },
	{ ERDMA_WC_SEND_PDID_ERR, IB_WC_LOC_ACCESS_ERR, ERDMA_WC_VENDOR_SQE_INVALID_PD },
	{ ERDMA_WC_SEND_WARRPING_ERR, IB_WC_LOC_ACCESS_ERR, ERDMA_WC_VENDOR_SQE_WARP_ERR },
	{ ERDMA_WC_FLUSH_ERR, IB_WC_WR_FLUSH_ERR, ERDMA_WC_VENDOR_NO_ERR },
	{ ERDMA_WC_RETRY_EXC_ERR, IB_WC_RETRY_EXC_ERR, ERDMA_WC_VENDOR_NO_ERR },
};

static int erdma_poll_one_cqe(struct erdma_cq *cq, struct erdma_cqe *cqe, struct ib_wc *wc)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);
	struct erdma_qp *qp;
	struct erdma_kqp *kern_qp;
	u64 *wqe_hdr;
	u64 *id_table;
	u32 qpn = be32_to_cpu(cqe->qpn);
	u16 wqe_idx = be32_to_cpu(cqe->qe_idx);
	u32 hdr = be32_to_cpu(cqe->hdr);
	u16 depth;
	u8 opcode, syndrome, qtype;

	qp = find_qp_by_qpn(dev, qpn);
	kern_qp = &qp->kern_qp;

	qtype = FIELD_GET(ERDMA_CQE_HDR_QTYPE_MASK, hdr);
	syndrome = FIELD_GET(ERDMA_CQE_HDR_SYNDROME_MASK, hdr);
	opcode = FIELD_GET(ERDMA_CQE_HDR_OPCODE_MASK, hdr);

	if (qtype == ERDMA_CQE_QTYPE_SQ) {
		id_table = kern_qp->swr_tbl;
		depth = qp->attrs.sq_size;
		wqe_hdr = (u64 *)get_sq_entry(qp, wqe_idx);
		kern_qp->sq_ci = wqe_idx + FIELD_GET(ERDMA_SQE_HDR_WQEBB_CNT_MASK, *wqe_hdr) + 1;
	} else {
		id_table = kern_qp->rwr_tbl;
		depth = qp->attrs.rq_size;
	}
	wc->wr_id = id_table[wqe_idx & (depth - 1)];
	wc->byte_len = be32_to_cpu(cqe->size);

	wc->wc_flags = 0;

	wc->opcode = map_cqe_opcode[opcode].base;
	if (wc->opcode == IB_WC_RECV_RDMA_WITH_IMM) {
		wc->ex.imm_data = be32_to_cpu(cqe->imm_data);
		wc->wc_flags |= IB_WC_WITH_IMM;
	}

	if (syndrome >= ERDMA_NUM_WC_STATUS)
		syndrome = ERDMA_WC_GENERAL_ERR;

	wc->status = map_cqe_status[syndrome].base;
	wc->vendor_err = map_cqe_status[syndrome].vendor;
	wc->qp = &qp->ibqp;

	return 0;
}

int erdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_cqe *cqe;
	unsigned long flags;
	u32 owner;
	u32 ci;
	int i, ret;
	int new = 0;
	u32 hdr;

	spin_lock_irqsave(&cq->kern_cq.lock, flags);

	owner = cq->kern_cq.owner;
	ci = cq->kern_cq.ci;

	for (i = 0; i < num_entries; i++) {
		cqe = &cq->kern_cq.qbuf[ci & (cq->depth - 1)];

		hdr = be32_to_cpu(READ_ONCE(cqe->hdr));
		if (FIELD_GET(ERDMA_CQE_HDR_OWNER_MASK, hdr) != owner)
			break;

		/* cqbuf should be ready when we poll*/
		dma_rmb();
		ret = erdma_poll_one_cqe(cq, cqe, wc);
		ci++;
		if ((ci & (cq->depth - 1)) == 0)
			owner = !owner;
		if (ret)
			continue;
		wc++;
		new++;
	}
	cq->kern_cq.owner = owner;
	cq->kern_cq.ci = ci;

	spin_unlock_irqrestore(&cq->kern_cq.lock, flags);
	return new;
}
