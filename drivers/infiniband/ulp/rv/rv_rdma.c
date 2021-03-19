// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#include "rv.h"
#include "trace.h"

/*
 * select next sconn to post and claim WQE by inc outstand_send_write
 * if all sconn SQs are full, next is left back where it started
 */
static struct rv_sconn *rv_conn_next_sconn_to_post(struct rv_conn *conn)
{
	unsigned long flags;
	struct rv_sconn *sconn;
	u8 i;
	u32 qp_depth = conn->jdev->qp_depth;

	spin_lock_irqsave(&conn->next_lock, flags);
	for (i = 0; i < conn->num_conn; i++) {
		sconn = &conn->sconn_arr[conn->next];
		conn->next = (conn->next + 1) % conn->num_conn;
		if (atomic_read(&sconn->stats.outstand_send_write) < qp_depth) {
			atomic_inc(&sconn->stats.outstand_send_write);
			goto unlock;
		}
	}
	sconn = NULL;
unlock:
	spin_unlock_irqrestore(&conn->next_lock, flags);
	return sconn;
}

static int rv_drv_post_write_immed(struct rv_pend_write *pend_wr)
{
	struct ib_rdma_wr wr;
	const struct ib_send_wr *bad_wr;
	struct ib_sge list;
	struct rv_mr_cached *mrc = pend_wr->mrc;

	/* we xlat the user space loc_addr to an iova appropriate for the MR */
	list.addr = mrc->mr.ib_mr->iova + (pend_wr->loc_addr - mrc->addr);
	list.length = pend_wr->length;
	list.lkey = mrc->mr.ib_mr->lkey;

	wr.wr.next = NULL;
	wr.wr.wr_cqe = &pend_wr->cqe;
	wr.wr.sg_list = &list;
	wr.wr.num_sge = 1;
	wr.wr.opcode = IB_WR_RDMA_WRITE_WITH_IMM;
	wr.wr.send_flags = IB_SEND_SIGNALED;
	wr.wr.ex.imm_data = cpu_to_be32(pend_wr->immed);
	wr.remote_addr = pend_wr->rem_addr;
	wr.rkey = pend_wr->rkey;
	return ib_post_send(pend_wr->sconn->qp, &wr.wr, &bad_wr);
}

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

static void rv_user_ring_post_event(struct rv_user_ring *ring,
				    struct rv_event *ev)
{
	unsigned long flags;
	struct rv_ring_header *hdr = ring->hdr;
	int next;

	trace_rv_user_ring_post_event(ring->rv_inx, ring->num_entries,
				      ring->hdr->head, ring->hdr->tail);
	trace_rv_event_post(ev->event_type, ev->wc.status, ev->wc.imm_data,
			    ev->wc.wr_id, ev->wc.conn_handle,
			    ev->wc.byte_len);
	spin_lock_irqsave(&ring->lock, flags);
	next = hdr->tail + 1;
	if (next == ring->num_entries)
		next = 0;
	if (next == hdr->head)  {
		hdr->overflow_cnt++;
		rv_err(ring->rv_inx, "event ring full: head %u tail %u\n",
		       hdr->head, hdr->tail);
		goto unlock;
	}

	smp_rmb(); /* ensure we read tail before writing event */
	hdr->entries[hdr->tail] = *ev;
	smp_wmb(); /* ensure ev written before advance tail */

	hdr->tail = next;
	if (ev->wc.status) {
		ring->stats.cqe_fail[ev->event_type]++;
	} else {
		ring->stats.cqe[ev->event_type]++;
		ring->stats.bytes[ev->event_type] += ev->wc.byte_len;
	}
unlock:
	spin_unlock_irqrestore(&ring->lock, flags);
}

static void rv_post_user_event_by_index(struct rv_job_dev *jdev, u16 index,
					struct rv_event *ev)
{
	unsigned long flags;
	struct rv_user *rv;

	spin_lock_irqsave(&jdev->user_array_lock, flags);
	if (index >= jdev->max_users)
		goto unlock;
	rv = jdev->user_array[index];
	if (rv && rv->cqr)
		rv_user_ring_post_event(rv->cqr, ev);
unlock:
	spin_unlock_irqrestore(&jdev->user_array_lock, flags);
}

/*
 * We have a rv_conn reference for the pend_wr
 * pass all failures to PSM to deal with.  We can't attempt
 * to retry the write in rv since it might have succeeded on remote
 * end (eg. ack lost) and remote end may be using buffer for something
 * else already
 */
static void rv_rdma_write_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct rv_pend_write *pend_wr = container_of(wc->wr_cqe,
						struct rv_pend_write, cqe);
	struct rv_sconn *sconn = pend_wr->sconn;
	struct rv_event ev = { 0 };

	atomic_dec(&sconn->stats.outstand_send_write);
	trace_rv_wc_write_done(pend_wr->wr_id, wc->status, wc->opcode,
			       wc->byte_len, be32_to_cpu(wc->ex.imm_data));
	trace_rv_pend_write_done(pend_wr->user_index, pend_wr->sconn, pend_wr,
				 pend_wr->loc_addr, pend_wr->rkey,
				 pend_wr->rem_addr, pend_wr->length,
				 pend_wr->immed, pend_wr->wr_id);

	if (unlikely(wc->status != IB_WC_SUCCESS))
		rv_report_cqe_error(cq, wc, pend_wr->sconn, "RDMA Write");
	else if (wc->qp != sconn->qp)
		rv_report_cqe_error(cq, wc, pend_wr->sconn, "Stale RDMA Write");

	ev.event_type = RV_WC_RDMA_WRITE;
	ev.wc.status = wc->status;
	ev.wc.wr_id = pend_wr->wr_id;
	ev.wc.conn_handle = (u64)pend_wr->sconn->parent;
	ev.wc.byte_len = pend_wr->length;
	trace_rv_event_write_done(ev.event_type, ev.wc.status, ev.wc.imm_data,
				  ev.wc.wr_id, ev.wc.conn_handle,
				  ev.wc.byte_len);

	rv_mr_cache_put(&pend_wr->umrs->cache, pend_wr->mrc);

	rv_post_user_event_by_index(pend_wr->sconn->parent->jdev,
				    pend_wr->user_index, &ev);

	if (wc->status)
		atomic64_inc(&sconn->stats.send_write_cqe_fail);
	else
		atomic64_inc(&sconn->stats.send_write_cqe);

	/* our rv_conn ref prevents user_mrs_put from triggering job cleanup */
	rv_user_mrs_put(pend_wr->umrs);

	/* rv_conn_put can put rv_job_dev and trigger whole job cleanup */
	rv_conn_put(sconn->parent);

	kfree(pend_wr);
}

/*
 * we do not need a queue inside rv of unposted writes.  If this fails
 * PSM will try to repost later.
 * We use loc_addr/length/access to lookup MR in cache and then verify RDMA is
 * consistent with loc_addr and length
 */
int doit_post_rdma_write(struct rv_user *rv, unsigned long arg)
{
	struct rv_post_write_params pparams;
	struct rv_conn *conn;
	struct rv_sconn *sconn;
	struct rv_mr_cached *mrc;
	struct rv_pend_write *pend_wr;
	int ret;

	if (copy_from_user(&pparams.in, (void __user *)arg,
			   sizeof(pparams.in)))
		return -EFAULT;

	mutex_lock(&rv->mutex);

	conn = user_conn_find(rv, pparams.in.handle);
	if (!conn) {
		rv_err(rv->inx, "post_write: No connection found\n");
		ret = -EINVAL;
		goto bail_unlock;
	}
	sconn = rv_conn_next_sconn_to_post(conn);
	if (unlikely(!sconn)) {
		ret = -ENOMEM;
		goto bail_unlock;
	}

	mrc = rv_mr_cache_search_get(&rv->umrs->cache, pparams.in.loc_mr_addr,
				     pparams.in.loc_mr_length,
				     pparams.in.loc_mr_access, false);
	if (!mrc) {
		rv_err(rv->inx, "post_write: bad loc_mr\n");
		ret = -EINVAL;
		goto bail_dec;
	}

	if (mrc->addr > (u64)pparams.in.loc_addr ||
	    mrc->addr + mrc->len <
	    (u64)pparams.in.loc_addr + pparams.in.length) {
		rv_err(rv->inx, "post_write: addr inconsistent with loc_mr\n");
		ret = -EINVAL;
		goto bail_put_mr;
	}
	if (!(mrc->access & IBV_ACCESS_KERNEL)) {
		rv_err(rv->inx, "post_write: loc_mr not a kernel MR\n");
		ret = -EINVAL;
		goto bail_put_mr;
	}

	pend_wr = kzalloc(sizeof(*pend_wr), GFP_KERNEL);
	if (!pend_wr) {
		ret = -ENOMEM;
		goto bail_put_mr;
	}
	pend_wr->cqe.done = rv_rdma_write_done;
	pend_wr->user_index = rv->index;

	rv_user_mrs_get(rv->umrs);
	pend_wr->umrs = rv->umrs;

	rv_conn_get(sconn->parent);
	pend_wr->sconn = sconn;

	pend_wr->mrc = mrc;
	pend_wr->loc_addr = (u64)pparams.in.loc_addr;
	pend_wr->rem_addr = pparams.in.rem_addr;
	pend_wr->rkey = pparams.in.rkey;
	pend_wr->length = pparams.in.length;
	pend_wr->immed = pparams.in.immed;
	pend_wr->wr_id = pparams.in.wr_id;

	mutex_lock(&sconn->mutex);
	if (sconn->state != RV_CONNECTED) {
		if (sconn->state == RV_ERROR)
			ret = -EIO;
		else if (test_bit(RV_SCONN_WAS_CONNECTED, &sconn->flags))
			ret = -EAGAIN;
		else
			ret = -EINVAL;
		mutex_unlock(&sconn->mutex);
		goto bail_free_pend;
	}

	trace_rv_pend_write_post(pend_wr->user_index, pend_wr->sconn, pend_wr,
				 pend_wr->loc_addr, pend_wr->rkey,
				 pend_wr->rem_addr, pend_wr->length,
				 pend_wr->immed, pend_wr->wr_id);
	ret = rv_drv_post_write_immed(pend_wr);
	if (ret) {
		sconn->stats.post_write_fail++;
	} else {
		sconn->stats.post_write++;
		sconn->stats.post_write_bytes += pparams.in.length;
	}

	pparams.out.sconn_index = sconn->index;
	pparams.out.conn_count = sconn->stats.conn_recovery + 1;

	mutex_unlock(&sconn->mutex);
	if (ret) {
		rv_err(rv->inx, "post_write: failed: %d\n", ret);
		goto bail_free_pend;
	}

	if (copy_to_user((void __user *)arg, &pparams.out, sizeof(pparams.out)))
		ret = -EFAULT;

	mutex_unlock(&rv->mutex);

	return 0;

bail_free_pend:
	rv_conn_put(pend_wr->sconn->parent);
	rv_user_mrs_put(pend_wr->umrs);
	kfree(pend_wr);

bail_put_mr:
	rv_mr_cache_put(&rv->umrs->cache, mrc);
bail_dec:
	atomic_dec(&sconn->stats.outstand_send_write);
bail_unlock:
	mutex_unlock(&rv->mutex);
	return ret;
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

static void rv_recv_rdma_write(struct rv_sconn *sconn, struct ib_wc *wc)
{
	struct rv_job_dev *jdev = sconn->parent->jdev;
	u32 index = be32_to_cpu(wc->ex.imm_data) >> (32 - jdev->index_bits);
	struct rv_event ev = { 0 };

	ev.event_type = RV_WC_RECV_RDMA_WITH_IMM;
	ev.wc.status = wc->status;
	ev.wc.resv1 = 0;
	ev.wc.imm_data = be32_to_cpu(wc->ex.imm_data);
	ev.wc.wr_id = 0;	/* N/A */
	ev.wc.conn_handle = (u64)sconn->parent;
	ev.wc.byte_len = wc->byte_len;
	ev.wc.resv2 = 0;
	trace_rv_event_recv_write(ev.event_type, ev.wc.status, ev.wc.imm_data,
				  ev.wc.wr_id, ev.wc.conn_handle,
				  ev.wc.byte_len);

	rv_post_user_event_by_index(jdev, index, &ev);
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
	else
		rv_recv_rdma_write(sconn, wc);
repost:
	spin_lock_irqsave(&sconn->drain_lock, flags);
	if (likely(!test_bit(RV_SCONN_DRAINING, &sconn->flags)))
		rv_drv_post_recv(sconn);
	spin_unlock_irqrestore(&sconn->drain_lock, flags);
put:
	rv_conn_put(sconn->parent);
}
