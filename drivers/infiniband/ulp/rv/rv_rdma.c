// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#include "rv.h"
#include "trace.h"

/*
 * This is called in Soft IRQs for CQE handling.
 * We just report errors here, let the QP Async Event deal with
 * how the sconn will react to the QP moving to QPS_ERR
 */
void rv_report_cqe_error(struct ib_cq *cq, struct ib_wc *wc,
			 struct rv_sconn *sconn, const char *opname)
{
	if (wc->status != IB_WC_WR_FLUSH_ERR)
		rv_conn_err(sconn,
			    "failed %s qp %u status %s (%d) for CQE %p\n",
			    opname, wc->qp ? wc->qp->qp_num : 0,
			    ib_wc_status_msg(wc->status), wc->status,
			    wc->wr_cqe);
}

static int rv_drv_post_recv(struct rv_sconn *sconn)
{
	struct ib_recv_wr wr;
	const struct ib_recv_wr *bad_wr;

	trace_rv_sconn_recv_post(sconn, sconn->index, sconn->qp->qp_num,
				 sconn->parent, sconn->flags,
				 (u32)sconn->state, 0);

	wr.next = NULL;
	wr.wr_cqe = &sconn->cqe;
	wr.sg_list = NULL;
	wr.num_sge = 0; /* only expect inbound RDMA Write w/immed */
	return ib_post_recv(sconn->qp, &wr, &bad_wr);
}

int rv_drv_prepost_recv(struct rv_sconn *sconn)
{
	int i;
	int ret;
	u32 qp_depth = sconn->parent->jdev->qp_depth;

	trace_rv_msg_prepost_recv(sconn, sconn->index, "prepost recv",
				  (u64)qp_depth, (u64)sconn);
	for (i = 0; i < qp_depth; i++) {
		ret = rv_drv_post_recv(sconn);
		if (ret)
			return ret;
	}
	return 0;
}

/* drain_lock makes sure no recv WQEs get reposted after a drain WQE */
void rv_recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rv_sconn *sconn = container_of(wc->wr_cqe,
					      struct rv_sconn, cqe);
	unsigned long flags;

	trace_rv_wc_recv_done((u64)sconn, wc->status, wc->opcode, wc->byte_len,
			      be32_to_cpu(wc->ex.imm_data));
	if (!sconn->parent)
		return;
	if (rv_conn_get_check(sconn->parent))
		return;
	trace_rv_sconn_recv_done(sconn, sconn->index,
				 wc->qp->qp_num, sconn->parent, sconn->flags,
				 (u32)(sconn->state),
				 be32_to_cpu(wc->ex.imm_data));
	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			rv_report_cqe_error(cq, wc, sconn, "Recv bad status");
			atomic64_inc(&sconn->stats.recv_cqe_fail);
		}
		goto put;
	}
	if (wc->qp != sconn->qp)
		goto put;

	if (unlikely(wc->opcode == IB_WC_RECV)) {
		atomic64_inc(&sconn->stats.recv_hb_cqe);
		goto repost;
	}

	/* use relaxed, no big deal if stats updated out of order */
	atomic64_inc(&sconn->stats.recv_write_cqe);
	atomic64_add_return_relaxed(wc->byte_len,
				    &sconn->stats.recv_write_bytes);

	if (unlikely(wc->opcode != IB_WC_RECV_RDMA_WITH_IMM))
		rv_report_cqe_error(cq, wc, sconn, "Recv bad opcode");
repost:
	spin_lock_irqsave(&sconn->drain_lock, flags);
	if (likely(!test_bit(RV_SCONN_DRAINING, &sconn->flags)))
		rv_drv_post_recv(sconn);
	spin_unlock_irqrestore(&sconn->drain_lock, flags);
put:
	rv_conn_put(sconn->parent);
}
