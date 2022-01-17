// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2021, Alibaba Group */
/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

void erdma_qp_llp_close(struct erdma_qp *qp)
{
	struct erdma_qp_attrs qp_attrs;

	down_write(&qp->state_lock);

	switch (qp->attrs.state) {
	case ERDMA_QP_STATE_RTS:
	case ERDMA_QP_STATE_RTR:
	case ERDMA_QP_STATE_IDLE:
	case ERDMA_QP_STATE_TERMINATE:
		qp_attrs.state = ERDMA_QP_STATE_CLOSING;
		erdma_modify_qp_internal(qp, &qp_attrs, ERDMA_QP_ATTR_STATE);
		break;
	case ERDMA_QP_STATE_CLOSING:
		qp->attrs.state = ERDMA_QP_STATE_IDLE;
		break;
	default:
		break;
	}

	if (qp->cep) {
		erdma_cep_put(qp->cep);
		qp->cep = NULL;
	}

	up_write(&qp->state_lock);
}

struct ib_qp *erdma_get_ibqp(struct ib_device *ibdev, int id)
{
	struct erdma_qp *qp = find_qp_by_qpn(to_edev(ibdev), id);

	if (qp)
		return &qp->ibqp;

	return (struct ib_qp *)NULL;
}

static int erdma_modify_qp_state_to_rts(struct erdma_qp *qp,
					struct erdma_qp_attrs *attrs,
					enum erdma_qp_attr_mask mask)
{
	int ret;
	struct erdma_dev *dev = qp->dev;
	struct erdma_cmdq_modify_qp_req req;
	struct tcp_sock *tp;
	struct erdma_cep *cep = qp->cep;
	struct sockaddr_storage local_addr, remote_addr;

	if (!(mask & ERDMA_QP_ATTR_LLP_HANDLE))
		return -EINVAL;

	if (!(mask & ERDMA_QP_ATTR_MPA))
		return -EINVAL;

	ret = getname_local(cep->sock, &local_addr);
	if (ret < 0)
		return ret;

	ret = getname_peer(cep->sock, &remote_addr);
	if (ret < 0)
		return ret;

	qp->attrs.state = ERDMA_QP_STATE_RTS;

	tp = tcp_sk(qp->cep->sock->sk);

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_MODIFY_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MODIFY_QP_STATE_MASK, qp->attrs.state) |
		   FIELD_PREP(ERDMA_CMD_MODIFY_QP_CC_MASK, qp->cc_method) |
		   FIELD_PREP(ERDMA_CMD_MODIFY_QP_QPN_MASK, QP_ID(qp));

	req.cookie = qp->cep->mpa.ext_data.cookie;
	req.dip = to_sockaddr_in(remote_addr).sin_addr.s_addr;
	req.sip = to_sockaddr_in(local_addr).sin_addr.s_addr;
	req.dport = to_sockaddr_in(remote_addr).sin_port;
	req.sport = to_sockaddr_in(local_addr).sin_port;

	req.send_nxt = tp->snd_nxt;
	 /* rsvd tcp seq for mpa-rsp in server. */
	if (qp->qp_type == ERDMA_QP_PASSIVE)
		req.send_nxt += MPA_DEFAULT_HDR_LEN + qp->private_data_len;
	req.recv_nxt = tp->rcv_nxt;

	ret = erdma_post_cmd_wait(dev->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (ret) {
		dev_err(dev->dmadev, "ERROR: err code = %d, cmd of modify qp failed.\n", ret);
		return ret;
	}

	return 0;
}

static int erdma_modify_qp_state_to_stop(struct erdma_qp *qp, struct erdma_qp_attrs *attrs,
					 enum erdma_qp_attr_mask mask)
{
	int ret;
	struct erdma_dev *dev = qp->dev;
	struct erdma_cmdq_modify_qp_req req;

	qp->attrs.state = attrs->state;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_MODIFY_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MODIFY_QP_STATE_MASK, attrs->state) |
		   FIELD_PREP(ERDMA_CMD_MODIFY_QP_QPN_MASK, QP_ID(qp));

	ret = erdma_post_cmd_wait(dev->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (ret) {
		dev_err(dev->dmadev, "ERROR: err code = %d, cmd of modify qp failed.\n", ret);
		return ret;
	}

	return 0;
}

int erdma_modify_qp_internal(struct erdma_qp *qp, struct erdma_qp_attrs *attrs,
			     enum erdma_qp_attr_mask mask)
{
	int drop_conn = 0, ret = 0;

	if (!mask)
		return 0;

	if (!(mask & ERDMA_QP_ATTR_STATE))
		return 0;

	switch (qp->attrs.state) {
	case ERDMA_QP_STATE_IDLE:
	case ERDMA_QP_STATE_RTR:
		if (attrs->state == ERDMA_QP_STATE_RTS) {
			ret = erdma_modify_qp_state_to_rts(qp, attrs, mask);
		} else if (attrs->state == ERDMA_QP_STATE_ERROR) {
			qp->attrs.state = ERDMA_QP_STATE_ERROR;
			if (qp->cep) {
				erdma_cep_put(qp->cep);
				qp->cep = NULL;
			}
			ret = erdma_modify_qp_state_to_stop(qp, attrs, mask);
		}
		break;
	case ERDMA_QP_STATE_RTS:
		if (attrs->state == ERDMA_QP_STATE_CLOSING) {
			ret = erdma_modify_qp_state_to_stop(qp, attrs, mask);
			drop_conn = 1;
		} else if (attrs->state == ERDMA_QP_STATE_TERMINATE) {
			qp->attrs.state = ERDMA_QP_STATE_TERMINATE;
			ret = erdma_modify_qp_state_to_stop(qp, attrs, mask);
			drop_conn = 1;
		} else if (attrs->state == ERDMA_QP_STATE_ERROR) {
			ret = erdma_modify_qp_state_to_stop(qp, attrs, mask);
			qp->attrs.state = ERDMA_QP_STATE_ERROR;
			drop_conn = 1;
		}
		break;
	case ERDMA_QP_STATE_TERMINATE:
		if (attrs->state == ERDMA_QP_STATE_ERROR)
			qp->attrs.state = ERDMA_QP_STATE_ERROR;
		break;
	case ERDMA_QP_STATE_CLOSING:
		if (attrs->state == ERDMA_QP_STATE_IDLE)
			qp->attrs.state = ERDMA_QP_STATE_IDLE;
		else if (attrs->state == ERDMA_QP_STATE_ERROR) {
			ret = erdma_modify_qp_state_to_stop(qp, attrs, mask);
			qp->attrs.state = ERDMA_QP_STATE_ERROR;
		} else if (attrs->state != ERDMA_QP_STATE_CLOSING) {
			return -ECONNABORTED;
		}
		break;
	default:
		break;
	}

	if (drop_conn)
		erdma_qp_cm_drop(qp);

	return ret;
}

static void erdma_qp_safe_free(struct kref *ref)
{
	struct erdma_qp *qp = container_of(ref, struct erdma_qp, ref);

	complete(&qp->safe_free);
}

void erdma_qp_put(struct erdma_qp *qp)
{
	WARN_ON(kref_read(&qp->ref) < 1);
	kref_put(&qp->ref, erdma_qp_safe_free);
}

void erdma_qp_get(struct erdma_qp *qp)
{
	kref_get(&qp->ref);
}

static int fill_inline_data(struct erdma_qp *qp, const struct ib_send_wr *send_wr,
			    u16 wqebb_idx, u32 sgl_offset, u32 *length_field)
{
	int i = 0;
	char *data;
	u32 remain_size, copy_size, data_off, bytes = 0;

	wqebb_idx += (sgl_offset >> SQEBB_SHIFT);
	sgl_offset &= (SQEBB_SIZE - 1);
	data = get_sq_entry(qp, wqebb_idx);

	while (i < send_wr->num_sge) {
		bytes += send_wr->sg_list[i].length;
		if (bytes > (int)ERDMA_MAX_INLINE)
			return -EINVAL;

		remain_size = send_wr->sg_list[i].length;
		data_off = 0;

		while (1) {
			copy_size = min(remain_size, SQEBB_SIZE - sgl_offset);

			memcpy(data + sgl_offset,
			       (void *)(uintptr_t)send_wr->sg_list[i].addr + data_off,
			       copy_size);
			remain_size -= copy_size;
			data_off += copy_size;
			sgl_offset += copy_size;
			wqebb_idx += (sgl_offset >> SQEBB_SHIFT);
			sgl_offset &= (SQEBB_SIZE - 1);

			data = get_sq_entry(qp, wqebb_idx);
			if (!remain_size)
				break;
		};

		i++;
	}
	*length_field = bytes;

	return bytes;
}

static int fill_sgl(struct erdma_qp *qp, const struct ib_send_wr *send_wr,
		    u16 wqebb_idx, u32 sgl_offset, u32 *length_field)
{
	int i = 0;
	u32 bytes = 0;
	char *sgl;

	if (send_wr->num_sge > qp->dev->attrs.max_send_sge)
		return -EINVAL;

	if (sgl_offset & 0xf) {
		ibdev_warn(&qp->dev->ibdev, "sgl in wqe not 16 Byte aligned.");
		return -EINVAL;
	}

	while (i < send_wr->num_sge) {
		wqebb_idx += (sgl_offset >> SQEBB_SHIFT);
		sgl_offset &= (SQEBB_SIZE - 1);
		sgl = get_sq_entry(qp, wqebb_idx);

		bytes += send_wr->sg_list[i].length;
		memcpy(sgl + sgl_offset, &send_wr->sg_list[i], sizeof(struct ib_sge));

		sgl_offset += sizeof(struct ib_sge);
		i++;
	}

	*length_field = bytes;
	return 0;
}

static int erdma_push_one_sqe(struct erdma_qp *qp, u16 *pi,
			      const struct ib_send_wr *send_wr)
{
	struct erdma_write_sqe *write_sqe;
	struct erdma_send_sqe *send_sqe;
	struct erdma_readreq_sqe *read_sqe;
	struct erdma_reg_mr_sqe *regmr_sge;
	struct erdma_mr *mr;
	struct ib_rdma_wr *rdma_wr;
	struct ib_sge *sge;
	u32 wqe_size, wqebb_cnt;
	int ret;
	u32 flags = send_wr->send_flags;
	u32 idx = *pi & (qp->attrs.sq_size - 1);
	u64 *entry = (u64 *)get_sq_entry(qp, idx);
	u64 wqe_hdr;
	u32 *length_field = NULL;
	u64 sgl_offset = 0;
	enum ib_wr_opcode op = send_wr->opcode;
	u64 *inline_data;

	*entry = 0;
	qp->kern_qp.swr_tbl[idx] = send_wr->wr_id;

	wqe_hdr = FIELD_PREP(ERDMA_SQE_HDR_CE_MASK,
			     ((flags & IB_SEND_SIGNALED) || qp->kern_qp.sig_all) ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_SE_MASK, flags & IB_SEND_SOLICITED ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_FENCE_MASK, flags & IB_SEND_FENCE ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_INLINE_MASK, flags & IB_SEND_INLINE ? 1 : 0);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_QPN_MASK, QP_ID(qp));

	switch (op) {
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, op == IB_WR_RDMA_WRITE ?
			ERDMA_OP_WRITE : ERDMA_OP_WRITE_WITH_IMM);
		rdma_wr = container_of(send_wr, struct ib_rdma_wr, wr);
		write_sqe = (struct erdma_write_sqe *)entry;

		write_sqe->imm_data = send_wr->ex.imm_data;
		write_sqe->sink_stag = rdma_wr->rkey;
		write_sqe->sink_to_high = (rdma_wr->remote_addr >> 32) & 0xFFFFFFFF;
		write_sqe->sink_to_low = rdma_wr->remote_addr & 0xFFFFFFFF;

		length_field = &write_sqe->length;
		wqe_size = sizeof(struct erdma_write_sqe);
		sgl_offset = wqe_size;
		break;
	case IB_WR_RDMA_READ:
	case IB_WR_RDMA_READ_WITH_INV:
		if (unlikely(send_wr->num_sge != 1))
			return -EINVAL;
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK,
			op == IB_WR_RDMA_READ ? ERDMA_OP_READ : ERDMA_OP_READ_WITH_INV);
		rdma_wr = container_of(send_wr, struct ib_rdma_wr, wr);
		read_sqe = (struct erdma_readreq_sqe *)entry;
		if (op == IB_WR_RDMA_READ_WITH_INV)
			read_sqe->invalid_stag = send_wr->ex.invalidate_rkey;

		read_sqe->length = send_wr->sg_list[0].length;
		read_sqe->sink_stag = send_wr->sg_list[0].lkey;
		read_sqe->sink_to_low = *(__u32 *)(&send_wr->sg_list[0].addr);
		read_sqe->sink_to_high = *((__u32 *)(&send_wr->sg_list[0].addr) + 1);

		sge = (struct ib_sge *)get_sq_entry(qp, idx + 1);
		sge->addr = rdma_wr->remote_addr;
		sge->lkey = rdma_wr->rkey;
		sge->length = send_wr->sg_list[0].length;
		wqe_size = sizeof(struct erdma_readreq_sqe) + send_wr->num_sge * 16;

		goto out;
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_SEND_WITH_INV:
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK,
			op == IB_WR_SEND ? ERDMA_OP_SEND : (op == IB_WR_SEND_WITH_IMM ?
			ERDMA_OP_SEND_WITH_IMM : ERDMA_OP_SEND_WITH_INV));
		send_sqe = (struct erdma_send_sqe *)entry;
		if (op == IB_WR_SEND_WITH_INV)
			send_sqe->imm_data = send_wr->ex.invalidate_rkey;
		else
			send_sqe->imm_data = send_wr->ex.imm_data;

		length_field = &send_sqe->length;
		wqe_size = sizeof(struct erdma_send_sqe);
		sgl_offset = wqe_size;

		break;
	case IB_WR_REG_MR:
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, ERDMA_OP_REG_MR);
		regmr_sge = (struct erdma_reg_mr_sqe *)entry;
		mr = to_emr(reg_wr(send_wr)->mr);

		mr->access = ERDMA_MR_ACC_LR |
			(reg_wr(send_wr)->access & IB_ACCESS_REMOTE_READ  ? ERDMA_MR_ACC_RR : 0) |
			(reg_wr(send_wr)->access & IB_ACCESS_LOCAL_WRITE  ? ERDMA_MR_ACC_LW : 0) |
			(reg_wr(send_wr)->access & IB_ACCESS_REMOTE_WRITE ? ERDMA_MR_ACC_RW : 0);
		regmr_sge->addr = mr->ibmr.iova;
		regmr_sge->length = mr->ibmr.length;
		regmr_sge->stag = mr->ibmr.lkey;
		regmr_sge->attrs |= FIELD_PREP(ERDMA_SQE_MR_ACCESS_MODE_MASK, 0);
		regmr_sge->attrs |= FIELD_PREP(ERDMA_SQE_MR_ACCESS_RIGHT_MASK, mr->access);
		regmr_sge->attrs |= FIELD_PREP(ERDMA_SQE_MR_MTT_COUNT_MASK, mr->mem.mtt_nents);

		if (mr->mem.mtt_nents < 4) {
			regmr_sge->attrs |= FIELD_PREP(ERDMA_SQE_MR_MTT_TYPE_MASK, 0);
			inline_data = (u64 *)get_sq_entry(qp, idx + 1);
			memcpy(inline_data, mr->mem.mtt_buf, mr->mem.mtt_nents * 8);
			wqe_size = sizeof(struct erdma_reg_mr_sqe) + mr->mem.mtt_nents * 8;
		} else {
			regmr_sge->attrs |= FIELD_PREP(ERDMA_SQE_MR_MTT_TYPE_MASK, 1);
			wqe_size = sizeof(struct erdma_reg_mr_sqe);
		}

		goto out;
	case IB_WR_LOCAL_INV:
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_OPCODE_MASK, ERDMA_OP_LOCAL_INV);
		regmr_sge = (struct erdma_reg_mr_sqe *)entry;
		regmr_sge->stag = send_wr->ex.invalidate_rkey;
		wqe_size = sizeof(struct erdma_reg_mr_sqe);
		goto out;
	default:
		return -EOPNOTSUPP;
	}

	if (flags & IB_SEND_INLINE) {
		ret = fill_inline_data(qp, send_wr, idx, sgl_offset, length_field);
		if (ret < 0)
			return -EINVAL;
		wqe_size += ret;
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_SGL_LEN_MASK, ret);
	} else {
		ret = fill_sgl(qp, send_wr, idx, sgl_offset, length_field);
		if (ret)
			return -EINVAL;
		wqe_size += send_wr->num_sge * sizeof(struct ib_sge);
		wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_SGL_LEN_MASK, send_wr->num_sge);
	}

out:
	wqebb_cnt = SQEBB_COUNT(wqe_size);
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_WQEBB_CNT_MASK, wqebb_cnt - 1);
	*pi += wqebb_cnt;
	wqe_hdr |= FIELD_PREP(ERDMA_SQE_HDR_WQEBB_INDEX_MASK, *pi);

	*entry = wqe_hdr;

	return 0;
}

static void kick_sq_db(struct erdma_qp *qp, u16 pi)
{
	u64 db_data = FIELD_PREP(ERDMA_SQE_HDR_QPN_MASK, QP_ID(qp)) |
		      FIELD_PREP(ERDMA_SQE_HDR_WQEBB_INDEX_MASK, pi);

	*(u64 *)qp->kern_qp.sq_db_info = db_data;
	writeq(db_data, qp->kern_qp.hw_sq_db);
}

int erdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *send_wr,
		    const struct ib_send_wr **bad_send_wr)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	int ret = 0;
	const struct ib_send_wr *wr = send_wr;
	unsigned long flags;
	u16 sq_pi;

	if (!send_wr)
		return -EINVAL;

	spin_lock_irqsave(&qp->lock, flags);
	sq_pi = qp->kern_qp.sq_pi;

	while (wr) {
		if ((u16)(sq_pi - qp->kern_qp.sq_ci) >= qp->attrs.sq_size) {
			ret = -ENOMEM;
			*bad_send_wr = send_wr;
			break;
		}

		ret = erdma_push_one_sqe(qp, &sq_pi, wr);
		if (ret) {
			*bad_send_wr = wr;
			break;
		}
		qp->kern_qp.sq_pi = sq_pi;
		kick_sq_db(qp, sq_pi);

		wr = wr->next;
	}
	spin_unlock_irqrestore(&qp->lock, flags);

	return ret;
}

static int erdma_post_recv_one(struct ib_qp *ibqp, const struct ib_recv_wr *recv_wr,
			       const struct ib_recv_wr **bad_recv_wr)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_rqe *rqe;
	unsigned int rq_pi;
	u16 idx;

	rq_pi = qp->kern_qp.rq_pi;
	idx = rq_pi & (qp->attrs.rq_size - 1);
	rqe = (struct erdma_rqe *)qp->kern_qp.rq_buf + idx;

	rqe->qe_idx = rq_pi + 1;
	rqe->qpn = QP_ID(qp);

	if (recv_wr->num_sge == 0) {
		rqe->length = 0;
	} else if (recv_wr->num_sge == 1) {
		rqe->stag = recv_wr->sg_list[0].lkey;
		rqe->to = recv_wr->sg_list[0].addr;
		rqe->length = recv_wr->sg_list[0].length;
	} else {
		return -EINVAL;
	}

	*(u64 *)qp->kern_qp.rq_db_info = *(u64 *)rqe;
	writeq(*(u64 *)rqe, qp->kern_qp.hw_rq_db);

	qp->kern_qp.rwr_tbl[idx] = recv_wr->wr_id;
	qp->kern_qp.rq_pi = rq_pi + 1;

	return 0;
}

int erdma_post_recv(struct ib_qp *qp, const struct ib_recv_wr *recv_wr,
		    const struct ib_recv_wr **bad_recv_wr)
{
	struct erdma_qp *eqp = to_eqp(qp);
	int ret = 0;
	const struct ib_recv_wr *wr = recv_wr;
	unsigned long flags;

	if (!qp || !recv_wr)
		return -EINVAL;

	spin_lock_irqsave(&eqp->lock, flags);
	while (wr) {
		ret = erdma_post_recv_one(qp, wr, bad_recv_wr);
		if (ret) {
			*bad_recv_wr = wr;
			break;
		}
		wr = wr->next;
	}
	spin_unlock_irqrestore(&eqp->lock, flags);
	return ret;
}
