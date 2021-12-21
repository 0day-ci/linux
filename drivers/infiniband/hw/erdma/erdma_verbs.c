// SPDX-License-Identifier: GPL-2.0
/*
 * Authors: Cheng Xu <chengyou@linux.alibaba.com>
 *          Kai Shen <kaishen@linux.alibaba.com>
 * Copyright (c) 2020-2021, Alibaba Group.
 *
 * Authors: Bernard Metzler <bmt@zurich.ibm.com>
 *          Fredy Neeser <nfd@zurich.ibm.com>
 * Copyright (c) 2008-2016, IBM Corporation
 *
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 */


#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <rdma/erdma-abi.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/uverbs_ioctl.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_hw.h"
#include "erdma_verbs.h"

static inline int
create_qp_cmd(struct erdma_dev *dev, struct erdma_qp *qp)
{
	struct erdma_cmdq_create_qp_req req;
	struct erdma_pd *pd = to_epd(qp->ibqp.pd);
	struct erdma_uqp *user_qp;
	int err;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_CREATE_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_QP_SQ_DEPTH_MASK, ilog2(qp->attrs.sq_size)) |
		FIELD_PREP(ERDMA_CMD_CREATE_QP_QPN_MASK, QP_ID(qp));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_QP_RQ_DEPTH_MASK, ilog2(qp->attrs.rq_size)) |
		FIELD_PREP(ERDMA_CMD_CREATE_QP_PD_MASK, pd->pdn);

	if (qp->is_kernel_qp) {
		u32 pg_sz_field = ilog2(SZ_1M) - 12;

		req.sq_cqn_mtt_cfg = FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK, pg_sz_field) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);
		req.rq_cqn_mtt_cfg = FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK, pg_sz_field) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg = FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_OFFSET_MASK, 0) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK, 1) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK, ERDMA_MR_INLINE_MTT);
		req.rq_mtt_cfg = req.sq_mtt_cfg;

		req.rq_buf_addr = qp->kern_qp.rq_buf_dma_addr;
		req.sq_buf_addr = qp->kern_qp.sq_buf_dma_addr;
		req.sq_db_info_dma_addr =
			qp->kern_qp.sq_buf_dma_addr + (SQEBB_SHIFT << qp->attrs.sq_size);
		req.rq_db_info_dma_addr =
			qp->kern_qp.rq_buf_dma_addr + (RQE_SHIFT << qp->attrs.rq_size);
	} else {
		user_qp = &qp->user_qp;
		req.sq_cqn_mtt_cfg = FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->sq_mtt.page_size) - 12);
		req.sq_cqn_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);

		req.rq_cqn_mtt_cfg = FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->rq_mtt.page_size) - 12);
		req.rq_cqn_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg = user_qp->sq_mtt.page_offset;
		req.sq_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK, user_qp->sq_mtt.mtt_nents) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK, user_qp->sq_mtt.mtt_type);

		req.rq_mtt_cfg = user_qp->rq_mtt.page_offset;
		req.rq_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK, user_qp->rq_mtt.mtt_nents) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK, user_qp->rq_mtt.mtt_type);

		if (user_qp->sq_mtt.mtt_nents == 1)
			req.sq_buf_addr = *(u64 *)user_qp->sq_mtt.mtt_buf;
		else
			req.sq_buf_addr = user_qp->sq_mtt.mtt_entry[0];

		if (user_qp->rq_mtt.mtt_nents == 1)
			req.rq_buf_addr = *(u64 *)user_qp->rq_mtt.mtt_buf;
		else
			req.rq_buf_addr = user_qp->rq_mtt.mtt_entry[0];

		req.sq_db_info_dma_addr = user_qp->sq_db_info_dma_addr;
		req.rq_db_info_dma_addr = user_qp->rq_db_info_dma_addr;
	}

	err = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (err) {
		dev_err(&dev->pdev->dev,
			"ERROR: err code = %d, cmd of create qp failed.\n", err);
		return err;
	}

	return 0;
}

static inline int
regmr_cmd(struct erdma_dev *dev, struct erdma_mr *mr)
{
	struct erdma_cmdq_reg_mr_req req;
	struct erdma_pd *pd = to_epd(mr->ibmr.pd);
	u64 *phy_addr;
	int err, i;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_REG_MR);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MR_VALID_MASK, mr->valid) |
		FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, mr->ibmr.lkey & 0xFF) |
		FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, mr->ibmr.lkey >> 8);
	req.cfg1 = FIELD_PREP(ERDMA_CMD_REGMR_PD_MASK, pd->pdn) |
		FIELD_PREP(ERDMA_CMD_REGMR_TYPE_MASK, mr->type) |
		FIELD_PREP(ERDMA_CMD_REGMR_RIGHT_MASK, mr->access) |
		FIELD_PREP(ERDMA_CMD_REGMR_ACC_MODE_MASK, 0);
	req.cfg2 = FIELD_PREP(ERDMA_CMD_REGMR_PAGESIZE_MASK, ilog2(mr->mem.page_size)) |
		FIELD_PREP(ERDMA_CMD_REGMR_MTT_TYPE_MASK, mr->mem.mtt_type) |
		FIELD_PREP(ERDMA_CMD_REGMR_MTT_CNT_MASK, mr->mem.page_cnt);

	if (mr->type == ERDMA_MR_TYPE_DMA)
		goto post_cmd;

	if (mr->type == ERDMA_MR_TYPE_NORMAL) {
		req.start_va = mr->mem.va;
		req.size = mr->mem.len;
	}

	if (mr->type == ERDMA_MR_TYPE_FRMR || mr->mem.mtt_type == ERDMA_MR_INDIRECT_MTT) {
		phy_addr = req.phy_addr;
		*phy_addr = mr->mem.mtt_entry[0];
	} else {
		phy_addr = req.phy_addr;
		for (i = 0; i < mr->mem.mtt_nents; i++)
			*phy_addr++ = mr->mem.mtt_entry[i];
	}

post_cmd:
	err = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (err) {
		dev_err(&dev->pdev->dev,
			"ERROR: err code = %d, cmd of reg mr failed.\n", err);
		return err;
	}

	return err;
}

static inline int
create_cq_cmd(struct erdma_dev *dev, struct erdma_cq *cq)
{
	int err;
	struct erdma_cmdq_create_cq_req req;
	u32 page_size;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_CREATE_CQ);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_CQN_MASK, cq->cqn) |
		FIELD_PREP(ERDMA_CMD_CREATE_CQ_DEPTH_MASK, ilog2(cq->depth));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_EQN_MASK, cq->assoc_eqn);

	if (cq->is_kernel_cq) {
		page_size = SZ_32M;
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK, ilog2(page_size) - 12);
		req.qbuf_addr_l = lower_32_bits(cq->kern_cq.qbuf_dma_addr);
		req.qbuf_addr_h = upper_32_bits(cq->kern_cq.qbuf_dma_addr);

		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK, 1) |
			FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK, ERDMA_MR_INLINE_MTT);

		req.first_page_offset = 0;
		req.cq_db_info_addr = cq->kern_cq.qbuf_dma_addr + (cq->depth << CQE_SHIFT);
	} else {
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
			ilog2(cq->user_cq.qbuf_mtt.page_size) - 12);
		if (cq->user_cq.qbuf_mtt.mtt_nents == 1) {
			req.qbuf_addr_l = lower_32_bits(*(u64 *)cq->user_cq.qbuf_mtt.mtt_buf);
			req.qbuf_addr_h = upper_32_bits(*(u64 *)cq->user_cq.qbuf_mtt.mtt_buf);
		} else {
			req.qbuf_addr_l = lower_32_bits(cq->user_cq.qbuf_mtt.mtt_entry[0]);
			req.qbuf_addr_h = upper_32_bits(cq->user_cq.qbuf_mtt.mtt_entry[0]);
		}
		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK,
			cq->user_cq.qbuf_mtt.mtt_nents);
		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK,
			cq->user_cq.qbuf_mtt.mtt_type);

		req.first_page_offset = cq->user_cq.qbuf_mtt.page_offset;
		req.cq_db_info_addr = cq->user_cq.db_info_dma_addr;
	}

	err = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (err) {
		dev_err(&dev->pdev->dev,
			"ERROR: err code = %d, cmd of create cq failed.\n", err);
		return err;
	}

	return 0;
}


static struct rdma_user_mmap_entry *
erdma_user_mmap_entry_insert(struct erdma_ucontext *uctx, void *address, u32 size,
			     u8 mmap_flag, u64 *mmap_offset)
{
	struct erdma_user_mmap_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	int ret;

	if (!entry)
		return NULL;

	entry->address = (u64)address;
	entry->mmap_flag = mmap_flag;

	size = PAGE_ALIGN(size);

	ret = rdma_user_mmap_entry_insert(&uctx->ibucontext,
		&entry->rdma_entry, size);
	if (ret) {
		kfree(entry);
		return NULL;
	}

	*mmap_offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

int erdma_query_device(struct ib_device *ibdev, struct ib_device_attr *attr,
		       struct ib_udata *unused)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(attr, 0, sizeof(*attr));

	attr->max_mr_size = dev->attrs.max_mr_size;
	attr->vendor_id = dev->attrs.vendor_id;
	attr->vendor_part_id = 0;
	attr->max_qp = dev->attrs.max_qp;
	attr->max_qp_wr = dev->attrs.max_send_wr > dev->attrs.max_recv_wr
		? dev->attrs.max_recv_wr : dev->attrs.max_send_wr;

	attr->max_qp_rd_atom = dev->attrs.max_ord;
	attr->max_qp_init_rd_atom = dev->attrs.max_ird;
	attr->max_res_rd_atom = dev->attrs.max_qp * dev->attrs.max_ird;
	attr->device_cap_flags = dev->attrs.cap_flags;
	ibdev->local_dma_lkey = dev->attrs.local_dma_key;
	attr->max_send_sge = dev->attrs.max_send_sge;
	attr->max_recv_sge = dev->attrs.max_recv_sge;
	attr->max_sge_rd = dev->attrs.max_sge_rd;
	attr->max_cq = dev->attrs.max_cq;
	attr->max_cqe = dev->attrs.max_cqe;
	attr->max_mr = dev->attrs.max_mr;
	attr->max_pd = dev->attrs.max_pd;
	attr->max_mw = dev->attrs.max_mw;
	attr->max_srq = dev->attrs.max_srq;
	attr->max_srq_wr = dev->attrs.max_srq_wr;
	attr->max_srq_sge = dev->attrs.max_srq_sge;
	attr->max_fast_reg_page_list_len = ERDMA_MAX_FRMR_PA;

	memcpy(&attr->sys_image_guid, dev->netdev->dev_addr, 6);

	return 0;
}

int erdma_query_pkey(struct ib_device *ibdev, u32 port, u16 idx, u16 *pkey)
{
	*pkey = 0xffff;
	return 0;
}

int erdma_query_gid(struct ib_device *ibdev, u32 port, int idx,
		    union ib_gid *gid)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(gid, 0, sizeof(*gid));
	memcpy(&gid->raw[0], dev->netdev->dev_addr, 6);

	return 0;
}

int erdma_query_port(struct ib_device *ibdev, u32 port,
		     struct ib_port_attr *attr)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(attr, 0, sizeof(*attr));

	attr->state = dev->state;
	attr->max_mtu = IB_MTU_1024;
	attr->active_mtu = attr->max_mtu;
	attr->gid_tbl_len = 1;
	attr->port_cap_flags = IB_PORT_CM_SUP;
	attr->port_cap_flags |= IB_PORT_DEVICE_MGMT_SUP;
	attr->max_msg_sz = -1;
	attr->pkey_tbl_len = 1;
	attr->active_width = 2;
	attr->active_speed = 2;
	attr->phys_state = dev->state == IB_PORT_ACTIVE ? 5 : 3;

	return 0;
}

int erdma_get_port_immutable(struct ib_device *ibdev, u32 port,
			     struct ib_port_immutable *port_immutable)
{
	struct ib_port_attr attr;
	int ret = erdma_query_port(ibdev, port, &attr);

	if (ret)
		return ret;

	port_immutable->pkey_tbl_len = attr.pkey_tbl_len;
	port_immutable->gid_tbl_len = attr.gid_tbl_len;
	port_immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;

	return 0;
}

int erdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);
	int pdn;

	pdn = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_PD]);
	if (pdn < 0)
		return pdn;

	pd->pdn = pdn;

	atomic_inc(&dev->num_pd);

	return 0;
}

int erdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_PD], pd->pdn);
	atomic_dec(&dev->num_pd);

	return 0;
}

static inline int
erdma_qp_validate_cap(struct erdma_dev *dev, struct ib_qp_init_attr *attrs)
{
	if ((attrs->cap.max_send_wr > dev->attrs.max_send_wr) ||
	    (attrs->cap.max_recv_wr > dev->attrs.max_recv_wr) ||
	    (attrs->cap.max_send_sge > dev->attrs.max_send_sge) ||
	    (attrs->cap.max_recv_sge > dev->attrs.max_recv_sge) ||
	    (attrs->cap.max_inline_data > ERDMA_MAX_INLINE) ||
	    !attrs->cap.max_send_wr ||
	    !attrs->cap.max_recv_wr) {
		return -EINVAL;
	}

	return 0;
}

static inline int
erdma_qp_validate_attr(struct erdma_dev *dev, struct ib_qp_init_attr *attrs)
{
	if (attrs->qp_type != IB_QPT_RC) {
		ibdev_err_ratelimited(&dev->ibdev, "only support RC mode.");
		return -EOPNOTSUPP;
	}

	if (attrs->srq) {
		ibdev_err_ratelimited(&dev->ibdev, "not support SRQ now.");
		return -EOPNOTSUPP;
	}

	if (!attrs->send_cq || !attrs->recv_cq) {
		ibdev_err_ratelimited(&dev->ibdev, "SCQ or RCQ is null.");
		return -EOPNOTSUPP;
	}

	return 0;
}

static void free_kernel_qp(struct erdma_qp *qp)
{
	struct erdma_dev *dev = qp->dev;

	vfree(qp->kern_qp.swr_tbl);
	vfree(qp->kern_qp.rwr_tbl);

	if (qp->kern_qp.sq_buf) {
		dma_free_coherent(&dev->pdev->dev,
			(qp->attrs.sq_size << SQEBB_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			qp->kern_qp.sq_buf, qp->kern_qp.sq_buf_dma_addr);
	}

	if (qp->kern_qp.rq_buf) {
		dma_free_coherent(&dev->pdev->dev,
			(qp->attrs.rq_size << RQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			qp->kern_qp.rq_buf, qp->kern_qp.rq_buf_dma_addr);
	}
}

static int
init_kernel_qp(struct erdma_dev *dev, struct erdma_qp *qp, struct ib_qp_init_attr *attrs)
{
	int ret = -ENOMEM;

	if (attrs->sq_sig_type == IB_SIGNAL_ALL_WR)
		qp->kern_qp.sig_all = 1;

	qp->is_kernel_qp = 1;
	qp->kern_qp.sq_pi = 0;
	qp->kern_qp.sq_ci = 0;
	qp->kern_qp.rq_pi = 0;
	qp->kern_qp.rq_ci = 0;
	qp->kern_qp.hw_sq_db = dev->func_bar + ERDMA_BAR_SQDB_SPACE_OFFSET +
		(ERDMA_SDB_SHARED_PAGE_INDEX << PAGE_SHIFT);
	qp->kern_qp.hw_rq_db = dev->func_bar + ERDMA_BAR_RQDB_SPACE_OFFSET;

	qp->kern_qp.swr_tbl = vmalloc(qp->attrs.sq_size * sizeof(u64));
	qp->kern_qp.rwr_tbl = vmalloc(qp->attrs.rq_size * sizeof(u64));

	qp->kern_qp.sq_buf = dma_alloc_coherent(&dev->pdev->dev,
		(qp->attrs.sq_size << SQEBB_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
		&qp->kern_qp.sq_buf_dma_addr, GFP_KERNEL);
	if (!qp->kern_qp.sq_buf)
		goto err_out;

	qp->kern_qp.rq_buf = dma_alloc_coherent(&dev->pdev->dev,
		(qp->attrs.rq_size << RQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
		&qp->kern_qp.rq_buf_dma_addr, GFP_KERNEL);
	if (!qp->kern_qp.rq_buf)
		goto err_out;

	qp->kern_qp.sq_db_info = qp->kern_qp.sq_buf + (qp->attrs.sq_size << SQEBB_SHIFT);
	qp->kern_qp.rq_db_info = qp->kern_qp.rq_buf + (qp->attrs.rq_size << RQE_SHIFT);

	return 0;

err_out:
	free_kernel_qp(qp);
	return ret;
}

static inline int
get_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem, u64 start,
		u64 len, int access, u64 virt, unsigned long req_page_size, u8 force_indirect_mtt)
{
	struct ib_block_iter biter;
	uint64_t *phy_addr = NULL;
	int ret = 0;

	mem->umem = ib_umem_get(&dev->ibdev, start, len, access);
	if (IS_ERR(mem->umem)) {
		ret = PTR_ERR(mem->umem);
		mem->umem = NULL;
		return ret;
	}

	mem->page_size = ib_umem_find_best_pgsz(mem->umem, req_page_size, virt);
	mem->page_offset = start & (mem->page_size - 1);
	mem->mtt_nents = ib_umem_num_dma_blocks(mem->umem, mem->page_size);
	mem->page_cnt = mem->mtt_nents;

	if (mem->page_cnt > ERDMA_MAX_INLINE_MTT_ENTRIES || force_indirect_mtt) {
		mem->mtt_type = ERDMA_MR_INDIRECT_MTT;
		mem->mtt_buf = alloc_pages_exact(MTT_SIZE(mem->page_cnt), GFP_KERNEL);
		if (!mem->mtt_buf) {
			ret = -ENOMEM;
			goto error_ret;
		}
		phy_addr = mem->mtt_buf;
	} else {
		mem->mtt_type = ERDMA_MR_INLINE_MTT;
		phy_addr = mem->mtt_entry;
	}

	rdma_umem_for_each_dma_block(mem->umem, &biter, mem->page_size) {
		*phy_addr = rdma_block_iter_dma_address(&biter);
		phy_addr++;
	}

	if (mem->mtt_type == ERDMA_MR_INDIRECT_MTT) {
		mem->mtt_entry[0] = dma_map_single(&dev->pdev->dev, mem->mtt_buf,
			MTT_SIZE(mem->page_cnt), DMA_TO_DEVICE);
		if (dma_mapping_error(&dev->pdev->dev, mem->mtt_entry[0])) {
			ibdev_err(&dev->ibdev, "failed to map DMA address.\n");
			free_pages_exact(mem->mtt_buf, MTT_SIZE(mem->page_cnt));
			mem->mtt_buf = NULL;
			ret = -ENOMEM;
			goto error_ret;
		}
	}

	return 0;

error_ret:
	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}

	return ret;
}

static void
put_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem)
{
	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}

	if (mem->mtt_buf) {
		dma_unmap_single(&dev->pdev->dev, mem->mtt_entry[0],
				MTT_SIZE(mem->page_cnt), DMA_TO_DEVICE);
		free_pages_exact(mem->mtt_buf, MTT_SIZE(mem->page_cnt));
	}
}

static int
erdma_map_user_dbrecords(struct erdma_ucontext *ctx, u64 dbrecords_va,
	struct erdma_user_dbrecords_page **dbr_page, dma_addr_t *dma_addr)
{
	struct erdma_user_dbrecords_page *page = NULL;
	int rv = 0;

	mutex_lock(&ctx->dbrecords_page_mutex);

	list_for_each_entry(page, &ctx->dbrecords_page_list, list)
		if (page->va == (dbrecords_va & PAGE_MASK))
			goto found;

	page = kmalloc(sizeof(*page), GFP_KERNEL);
	if (!page) {
		rv = -ENOMEM;
		goto out;
	}

	page->va = (dbrecords_va & PAGE_MASK);
	page->refcnt = 0;

	page->umem = ib_umem_get(ctx->ibucontext.device, dbrecords_va & PAGE_MASK, PAGE_SIZE, 0);
	if (IS_ERR(page->umem)) {
		rv = PTR_ERR(page->umem);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &ctx->dbrecords_page_list);

found:
	*dma_addr = sg_dma_address(page->umem->sgt_append.sgt.sgl) + (dbrecords_va & ~PAGE_MASK);
	*dbr_page = page;
	page->refcnt++;

out:
	mutex_unlock(&ctx->dbrecords_page_mutex);
	return rv;
}

static void
erdma_unmap_user_dbrecords(struct erdma_ucontext *ctx, struct erdma_user_dbrecords_page **dbr_page)
{
	if (!ctx || !(*dbr_page))
		return;

	mutex_lock(&ctx->dbrecords_page_mutex);
	if (--(*dbr_page)->refcnt == 0) {
		list_del(&(*dbr_page)->list);
		ib_umem_release((*dbr_page)->umem);
		kfree(*dbr_page);
	}

	*dbr_page = NULL;
	mutex_unlock(&ctx->dbrecords_page_mutex);
}

static int
init_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx, u64 va, u32 len, u64 db_info_va)
{
	int ret;
	dma_addr_t db_info_dma_addr;
	u32 rq_offset;

	qp->is_kernel_qp = false;
	if (len < (PAGE_ALIGN(qp->attrs.sq_size * SQEBB_SIZE) + qp->attrs.rq_size * RQE_SIZE)) {
		ibdev_err(&qp->dev->ibdev, "queue len error qbuf(%u) sq(%u) rq(%u).\n", len,
			qp->attrs.sq_size, qp->attrs.rq_size);
		return -EINVAL;
	}

	ret = get_mtt_entries(qp->dev, &qp->user_qp.sq_mtt, va,
		qp->attrs.sq_size << SQEBB_SHIFT, 0, va, (SZ_1M - SZ_4K), 1);
	if (ret)
		goto err_out;

	rq_offset = PAGE_ALIGN(qp->attrs.sq_size << SQEBB_SHIFT);
	qp->user_qp.rq_offset = rq_offset;

	ret = get_mtt_entries(qp->dev, &qp->user_qp.rq_mtt, va + rq_offset,
		qp->attrs.rq_size << RQE_SHIFT, 0, va + rq_offset, (SZ_1M - SZ_4K), 1);
	if (ret)
		goto err_out;

	ret = erdma_map_user_dbrecords(uctx, db_info_va,
		&qp->user_qp.user_dbr_page, &db_info_dma_addr);
	if (ret)
		goto err_out;

	qp->user_qp.sq_db_info_dma_addr = db_info_dma_addr;
	qp->user_qp.rq_db_info_dma_addr = db_info_dma_addr + 8;

	return 0;

err_out:
	return ret;
}

static void
free_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx)
{
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mtt);
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mtt);
	erdma_unmap_user_dbrecords(uctx, &qp->user_qp.user_dbr_page);
}

int erdma_create_qp(struct ib_qp *ibqp,
		    struct ib_qp_init_attr *attrs,
		    struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *uctx =
		rdma_udata_to_drv_context(udata, struct erdma_ucontext, ibucontext);
	struct erdma_ureq_create_qp ureq;
	struct erdma_uresp_create_qp uresp;
	int ret;

	ret = erdma_qp_validate_cap(dev, attrs);
	if (ret)
		goto err_out;

	ret = erdma_qp_validate_attr(dev, attrs);
	if (ret)
		goto err_out;

	qp->scq = to_ecq(attrs->send_cq);
	qp->rcq = to_ecq(attrs->recv_cq);
	qp->dev = dev;

	init_rwsem(&qp->state_lock);
	kref_init(&qp->ref);
	init_completion(&qp->safe_free);

	ret = xa_alloc_cyclic(&dev->qp_xa, &qp->ibqp.qp_num, qp,
		XA_LIMIT(1, dev->attrs.max_qp - 1), &dev->next_alloc_qpn, GFP_KERNEL);
	if (ret < 0) {
		ret = -ENOMEM;
		goto err_out;
	}

	qp->attrs.sq_size = roundup_pow_of_two(attrs->cap.max_send_wr * ERDMA_MAX_WQEBB_PER_SQE);
	qp->attrs.rq_size = roundup_pow_of_two(attrs->cap.max_recv_wr);

	if (uctx) {
		ret = ib_copy_from_udata(&ureq, udata, min(sizeof(ureq), udata->inlen));
		if (ret)
			goto err_out_xa;

		init_user_qp(qp, uctx, ureq.qbuf_va, ureq.qbuf_len, ureq.db_record_va);

		memset(&uresp, 0, sizeof(uresp));

		uresp.num_sqe = qp->attrs.sq_size;
		uresp.num_rqe = qp->attrs.rq_size;
		uresp.qp_id = QP_ID(qp);
		uresp.rq_offset = qp->user_qp.rq_offset;

		ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (ret)
			goto err_out_xa;
	} else {
		init_kernel_qp(dev, qp, attrs);
	}

	qp->attrs.max_send_sge = attrs->cap.max_send_sge;
	qp->attrs.max_recv_sge = attrs->cap.max_recv_sge;
	qp->attrs.state = ERDMA_QP_STATE_IDLE;

	ret = create_qp_cmd(dev, qp);
	if (ret)
		goto err_out_cmd;

	spin_lock_init(&qp->lock);
	atomic_inc(&dev->num_qp);

	return 0;

err_out_cmd:
	if (qp->is_kernel_qp)
		free_kernel_qp(qp);
	else
		free_user_qp(qp, uctx);
err_out_xa:
	xa_erase(&dev->qp_xa, QP_ID(qp));
err_out:
	return ret;
}

static inline int erdma_create_stag(struct erdma_dev *dev, u32 *stag)
{
	int stag_idx;
	u32 key = 0;

	stag_idx = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX]);
	if (stag_idx < 0)
		return stag_idx;

	*stag = (stag_idx << 8) | (key & 0xFF);

	return 0;
}

struct ib_mr *erdma_get_dma_mr(struct ib_pd *ibpd, int mr_access_flags)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibpd->device);
	int ret;
	u32 stag;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_DMA;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->access = ERDMA_MR_ACC_LR |
		(mr_access_flags & IB_ACCESS_REMOTE_READ ? ERDMA_MR_ACC_RR : 0) |
		(mr_access_flags & IB_ACCESS_LOCAL_WRITE ? ERDMA_MR_ACC_LW : 0) |
		(mr_access_flags & IB_ACCESS_REMOTE_WRITE ? ERDMA_MR_ACC_RW : 0);
	ret = regmr_cmd(dev, mr);
	if (ret) {
		ret = -EIO;
		goto out_remove_stag;
	}

	atomic_inc(&dev->num_mr);
	return &mr->ibmr;

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

struct ib_mr *
erdma_ib_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type, u32 max_num_sg)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibpd->device);
	int ret;
	u32 stag;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EOPNOTSUPP);

	if (max_num_sg > ERDMA_MR_MAX_MTT_CNT) {
		ibdev_err(&dev->ibdev, "max_num_sg too large:%u", max_num_sg);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_FRMR;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	/* update it in FRMR. */
	mr->access = ERDMA_MR_ACC_LR | ERDMA_MR_ACC_LW | ERDMA_MR_ACC_RR | ERDMA_MR_ACC_RW;

	mr->mem.page_size = PAGE_SIZE; /* update it later. */
	mr->mem.page_cnt = max_num_sg;
	mr->mem.mtt_type = ERDMA_MR_INDIRECT_MTT;
	mr->mem.mtt_buf = alloc_pages_exact(MTT_SIZE(mr->mem.page_cnt), GFP_KERNEL);
	if (!mr->mem.mtt_buf) {
		ret = -ENOMEM;
		goto out_remove_stag;
	}

	mr->mem.mtt_entry[0] = dma_map_single(&dev->pdev->dev, mr->mem.mtt_buf,
		MTT_SIZE(mr->mem.page_cnt), DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->pdev->dev, mr->mem.mtt_entry[0])) {
		ret = -ENOMEM;
		goto out_free_mtt;
	}

	ret = regmr_cmd(dev, mr);
	if (ret) {
		ret = -EIO;
		goto out_dma_unmap;
	}

	atomic_inc(&dev->num_mr);
	return &mr->ibmr;

out_dma_unmap:
	dma_unmap_single(&dev->pdev->dev, mr->mem.mtt_entry[0],
		MTT_SIZE(mr->mem.page_cnt), DMA_TO_DEVICE);
out_free_mtt:
	free_pages_exact(mr->mem.mtt_buf, MTT_SIZE(mr->mem.page_cnt));

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	return ERR_PTR(ret);
}


static int erdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct erdma_mr *mr = to_emr(ibmr);

	if (mr->mem.mtt_nents >= mr->mem.page_cnt)
		return -1;

	*((u64 *)mr->mem.mtt_buf + mr->mem.mtt_nents) = addr;
	mr->mem.mtt_nents++;

	return 0;
}


int erdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset)
{
	struct erdma_mr *mr = to_emr(ibmr);
	int num;

	mr->mem.mtt_nents = 0;

	num = ib_sg_to_pages(&mr->ibmr, sg, sg_nents, sg_offset, erdma_set_page);

	return num;
}


struct ib_mr *erdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				u64 virt, int access, struct ib_udata *udata)
{
	struct erdma_mr *mr = NULL;
	struct erdma_dev *dev = to_edev(ibpd->device);
	u32 stag;
	int ret;

	if (!len || len > dev->attrs.max_mr_size) {
		ibdev_err(&dev->ibdev, "ERROR: Out of mr size: %llu, max %llu\n",
			len, dev->attrs.max_mr_size);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = get_mtt_entries(dev, &mr->mem, start, len, access, virt, SZ_2G - SZ_4K, 0);
	if (ret)
		goto err_out_free;

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto err_out_put_mtt;

	mr->ibmr.lkey = mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->mem.va = virt;
	mr->mem.len = len;
	mr->access = ERDMA_MR_ACC_LR |
		(access & IB_ACCESS_REMOTE_READ ? ERDMA_MR_ACC_RR : 0) |
		(access & IB_ACCESS_LOCAL_WRITE ? ERDMA_MR_ACC_LW : 0) |
		(access & IB_ACCESS_REMOTE_WRITE ? ERDMA_MR_ACC_RW : 0);
	mr->valid = 1;
	mr->type = ERDMA_MR_TYPE_NORMAL;

	ret = regmr_cmd(dev, mr);
	if (ret) {
		ret = -EIO;
		goto err_out_mr;
	}

	atomic_inc(&dev->num_mr);

	return &mr->ibmr;

err_out_mr:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], mr->ibmr.lkey >> 8);

err_out_put_mtt:
	put_mtt_entries(dev, &mr->mem);

err_out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

int erdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibmr->device);
	struct erdma_cmdq_dereg_mr_req req;
	int ret;

	mr = to_emr(ibmr);

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_DEREG_MR);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, ibmr->lkey >> 8) |
		FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, ibmr->lkey & 0xFF);

	ret = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (ret) {
		dev_err(&dev->pdev->dev,
			"ERROR: err code = %d, cmd of dereg mr failed.\n", ret);
		return ret;
	}

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], ibmr->lkey >> 8);
	atomic_dec(&dev->num_mr);

	put_mtt_entries(dev, &mr->mem);

	kfree(mr);
	return 0;
}

extern int erdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	struct erdma_ucontext *ctx =
		rdma_udata_to_drv_context(udata, struct erdma_ucontext, ibucontext);
	int err;
	struct erdma_cmdq_destroy_cq_req req;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_DESTROY_CQ);
	req.cqn = cq->cqn;

	err = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req,
		sizeof(req), NULL, NULL);
	if (err) {
		dev_err(&dev->pdev->dev,
			"ERROR: err code = %d, cmd of destroy cq failed.\n", err);
		return err;
	}

	if (cq->is_kernel_cq) {
		dma_free_coherent(&dev->pdev->dev,
			(cq->depth << CQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
	} else {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
	}

	xa_erase(&dev->cq_xa, cq->cqn);
	atomic_dec(&dev->num_cq);

	return 0;
}

int erdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *ctx =
		rdma_udata_to_drv_context(udata, struct erdma_ucontext, ibucontext);
	struct erdma_qp_attrs qp_attrs;
	int err;
	struct erdma_cmdq_destroy_qp_req req;

	down_write(&qp->state_lock);
	qp_attrs.state = ERDMA_QP_STATE_ERROR;
	(void)erdma_modify_qp_internal(qp, &qp_attrs, ERDMA_QP_ATTR_STATE);
	up_write(&qp->state_lock);

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_DESTROY_QP);
	req.qpn = QP_ID(qp);

	erdma_qp_put(qp);
	wait_for_completion(&qp->safe_free);

	err = erdma_post_cmd_wait(&dev->cmdq, (u64 *)&req,
		sizeof(req), NULL, NULL);
	if (err) {
		dev_err(&dev->pdev->dev,
			"ERROR: err code = %d, cmd of destroy qp failed.\n", err);
		up_write(&qp->state_lock);
		return err;
	}

	if (qp->is_kernel_qp) {
		vfree(qp->kern_qp.swr_tbl);
		vfree(qp->kern_qp.rwr_tbl);
		dma_free_coherent(&dev->pdev->dev,
			(qp->attrs.rq_size << RQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			qp->kern_qp.rq_buf, qp->kern_qp.rq_buf_dma_addr);
		dma_free_coherent(&dev->pdev->dev,
			(qp->attrs.sq_size << SQEBB_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			qp->kern_qp.sq_buf, qp->kern_qp.sq_buf_dma_addr);
	} else {
		put_mtt_entries(dev, &qp->user_qp.sq_mtt);
		put_mtt_entries(dev, &qp->user_qp.rq_mtt);
		erdma_unmap_user_dbrecords(ctx, &qp->user_qp.user_dbr_page);
	}

	if (qp->cep)
		erdma_cep_put(qp->cep);
	xa_erase(&dev->qp_xa, QP_ID(qp));
	atomic_dec(&dev->num_qp);

	return 0;
}

void erdma_qp_get_ref(struct ib_qp *ibqp)
{
	erdma_qp_get(to_eqp(ibqp));
}

void erdma_qp_put_ref(struct ib_qp *ibqp)
{
	erdma_qp_put(to_eqp(ibqp));
}

int erdma_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct erdma_user_mmap_entry *entry;
	int err = -EINVAL;

	if (vma->vm_start & (PAGE_SIZE - 1)) {
		pr_warn("WARN: map not page aligned\n");
		goto out;
	}

	rdma_entry = rdma_user_mmap_entry_get(ctx, vma);
	if (!rdma_entry) {
		pr_warn("WARN: mmap lookup failed: %lx\n", vma->vm_pgoff);
		goto out;
	}

	entry = to_emmap(rdma_entry);

	switch (entry->mmap_flag) {
	case ERDMA_MMAP_IO_NC:
		/* map doorbell. */
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		err = io_remap_pfn_range(vma, vma->vm_start, PFN_DOWN(entry->address),
			PAGE_SIZE, vma->vm_page_prot);
		break;
	default:
		pr_err("mmap failed, uobj type = %d\n", entry->mmap_flag);
		err = -EINVAL;
		break;
	}

	rdma_user_mmap_entry_put(rdma_entry);
out:
	return err;
}

#define ERDMA_SDB_PAGE     0
#define ERDMA_SDB_ENTRY    1
#define ERDMA_SDB_SHARED   2

static void alloc_db_resources(struct erdma_dev *dev, struct erdma_ucontext *ctx)
{
	u32 bitmap_idx;

	if (dev->disable_dwqe)
		goto alloc_normal_db;

	/* Try to alloc independent SDB page. */
	spin_lock(&dev->db_bitmap_lock);
	bitmap_idx = find_first_zero_bit(dev->sdb_page, dev->dwqe_pages);
	if (bitmap_idx != dev->dwqe_pages) {
		set_bit(bitmap_idx, dev->sdb_page);
		spin_unlock(&dev->db_bitmap_lock);

		ctx->sdb_type = ERDMA_SDB_PAGE;
		ctx->sdb_idx = bitmap_idx;
		ctx->sdb_page_idx = bitmap_idx;
		ctx->sdb = dev->func_bar_addr +
			ERDMA_BAR_SQDB_SPACE_OFFSET + (bitmap_idx << PAGE_SHIFT);
		ctx->sdb_page_off = 0;

		return;
	}

	bitmap_idx = find_first_zero_bit(dev->sdb_entry, dev->dwqe_entries);
	if (bitmap_idx != dev->dwqe_entries) {
		set_bit(bitmap_idx, dev->sdb_entry);
		spin_unlock(&dev->db_bitmap_lock);

		ctx->sdb_type = ERDMA_SDB_ENTRY;
		ctx->sdb_idx = bitmap_idx;
		ctx->sdb_page_idx = ERDMA_DWQE_TYPE0_CNT +
			bitmap_idx / ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
		ctx->sdb_page_off = bitmap_idx % ERDMA_DWQE_TYPE1_CNT_PER_PAGE;

		ctx->sdb = dev->func_bar_addr +
			ERDMA_BAR_SQDB_SPACE_OFFSET + (ctx->sdb_page_idx << PAGE_SHIFT);

		return;
	}

	spin_unlock(&dev->db_bitmap_lock);

alloc_normal_db:
	ctx->sdb_type = ERDMA_SDB_SHARED;
	ctx->sdb_idx = 0;
	ctx->sdb_page_idx = ERDMA_SDB_SHARED_PAGE_INDEX;
	ctx->sdb_page_off = 0;

	ctx->sdb = dev->func_bar_addr +
		ERDMA_BAR_SQDB_SPACE_OFFSET + (ctx->sdb_page_idx << PAGE_SHIFT);
}

static void erdma_uctx_user_mmap_entries_remove(struct erdma_ucontext *uctx)
{
	rdma_user_mmap_entry_remove(uctx->sq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->rq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->cq_db_mmap_entry);
}

int erdma_alloc_ucontext(struct ib_ucontext *ibctx,
			 struct ib_udata *udata)
{
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_dev *dev = to_edev(ibctx->device);
	int ret;
	struct erdma_uresp_alloc_ctx uresp = {};

	if (atomic_inc_return(&dev->num_ctx) > ERDMA_MAX_CONTEXT) {
		ret = -ENOMEM;
		goto err_out;
	}

	INIT_LIST_HEAD(&ctx->dbrecords_page_list);
	mutex_init(&ctx->dbrecords_page_mutex);
	ctx->dev = dev;

	alloc_db_resources(dev, ctx);

	ctx->rdb = dev->func_bar_addr + ERDMA_BAR_RQDB_SPACE_OFFSET;
	ctx->cdb = dev->func_bar_addr + ERDMA_BAR_CQDB_SPACE_OFFSET;

	if (udata->outlen < sizeof(uresp)) {
		ret = -EINVAL;
		goto err_out;
	}

	ctx->sq_db_mmap_entry = erdma_user_mmap_entry_insert(ctx, (void *)ctx->sdb,
		PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.sdb);
	if (!ctx->sq_db_mmap_entry) {
		ret = -ENOMEM;
		goto err_out;
	}

	ctx->rq_db_mmap_entry = erdma_user_mmap_entry_insert(ctx, (void *)ctx->rdb,
		PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.rdb);
	if (!ctx->sq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_out;
	}

	ctx->cq_db_mmap_entry = erdma_user_mmap_entry_insert(ctx, (void *)ctx->cdb,
		PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.cdb);
	if (!ctx->cq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_out;
	}

	uresp.dev_id = dev->attrs.vendor_part_id;
	uresp.sdb_type = ctx->sdb_type;
	uresp.sdb_offset = ctx->sdb_page_off;

	ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (ret)
		goto err_out;

	return 0;

err_out:
	erdma_uctx_user_mmap_entries_remove(ctx);
	atomic_dec(&dev->num_ctx);
	return ret;
}


void erdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_dev *dev = ctx->dev;

	spin_lock(&dev->db_bitmap_lock);
	if (ctx->sdb_type == ERDMA_SDB_PAGE)
		clear_bit(ctx->sdb_idx, dev->sdb_page);
	else if (ctx->sdb_type == ERDMA_SDB_ENTRY)
		clear_bit(ctx->sdb_idx, dev->sdb_entry);

	erdma_uctx_user_mmap_entries_remove(ctx);

	spin_unlock(&dev->db_bitmap_lock);

	atomic_dec(&ctx->dev->num_ctx);
}

static int ib_qp_state_to_erdma_qp_state[IB_QPS_ERR+1] = {
	[IB_QPS_RESET]	= ERDMA_QP_STATE_IDLE,
	[IB_QPS_INIT]	= ERDMA_QP_STATE_IDLE,
	[IB_QPS_RTR]	= ERDMA_QP_STATE_RTR,
	[IB_QPS_RTS]	= ERDMA_QP_STATE_RTS,
	[IB_QPS_SQD]	= ERDMA_QP_STATE_CLOSING,
	[IB_QPS_SQE]	= ERDMA_QP_STATE_TERMINATE,
	[IB_QPS_ERR]	= ERDMA_QP_STATE_ERROR
};

int erdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		    int attr_mask, struct ib_udata *udata)
{
	struct erdma_qp_attrs new_attrs;
	enum erdma_qp_attr_mask erdma_attr_mask = 0;
	struct erdma_qp *qp = to_eqp(ibqp);
	int ret = 0;

	if (!attr_mask)
		goto out;

	memset(&new_attrs, 0, sizeof(new_attrs));

	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		erdma_attr_mask |= ERDMA_QP_ATTR_ACCESS_FLAGS;

		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ)
			new_attrs.flags |= ERDMA_READ_ENABLED;
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE)
			new_attrs.flags |= ERDMA_WRITE_ENABLED;
		if (attr->qp_access_flags & IB_ACCESS_MW_BIND)
			new_attrs.flags |= ERDMA_BIND_ENABLED;
	}

	if (attr_mask & IB_QP_STATE) {
		new_attrs.state = ib_qp_state_to_erdma_qp_state[attr->qp_state];

		if (new_attrs.state == ERDMA_QP_STATE_UNDEF)
			return -EINVAL;

		erdma_attr_mask |= ERDMA_QP_ATTR_STATE;
	}

	down_write(&qp->state_lock);

	ret = erdma_modify_qp_internal(qp, &new_attrs, erdma_attr_mask);

	up_write(&qp->state_lock);

out:
	return ret;
}

static inline enum ib_mtu erdma_mtu_net2ib(unsigned short mtu)
{
	if (mtu >= 4096)
		return IB_MTU_4096;
	if (mtu >= 2048)
		return IB_MTU_2048;
	if (mtu >= 1024)
		return IB_MTU_1024;
	if (mtu >= 512)
		return IB_MTU_512;
	if (mtu >= 256)
		return IB_MTU_256;
	return IB_MTU_4096;
}

int erdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct erdma_qp *qp;
	struct erdma_dev *dev;

	if (ibqp && qp_attr && qp_init_attr) {
		qp = to_eqp(ibqp);
		dev = to_edev(ibqp->device);
	} else
		return -EINVAL;

	qp_attr->cap.max_inline_data = ERDMA_MAX_INLINE;
	qp_init_attr->cap.max_inline_data = ERDMA_MAX_INLINE;

	qp_attr->cap.max_send_wr = qp->attrs.sq_size;
	qp_attr->cap.max_recv_wr = qp->attrs.rq_size;
	qp_attr->cap.max_send_sge = qp->attrs.max_send_sge;
	qp_attr->cap.max_recv_sge = qp->attrs.max_recv_sge;

	qp_attr->path_mtu = erdma_mtu_net2ib(dev->netdev->mtu);
	qp_attr->max_rd_atomic = qp->attrs.irq_size;
	qp_attr->max_dest_rd_atomic = qp->attrs.orq_size;

	qp_attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE |
		IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_READ;

	qp_init_attr->cap = qp_attr->cap;

	return 0;
}

int erdma_create_cq(struct ib_cq *ibcq,
		    const struct ib_cq_init_attr *attr,
		    struct ib_udata *udata)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	unsigned int depth = attr->cqe;
	int ret;
	struct erdma_ucontext *ctx =
		rdma_udata_to_drv_context(udata, struct erdma_ucontext, ibucontext);

	if (depth > dev->attrs.max_cqe) {
		dev_warn(&dev->pdev->dev,
			"WARN: exceed cqe(%d) > capbility(%d)\n",
			depth, dev->attrs.max_cqe);
		return -EINVAL;
	}

	depth = roundup_pow_of_two(depth);
	cq->ibcq.cqe = depth;
	cq->depth = depth;
	cq->assoc_eqn = attr->comp_vector + 1;

	ret = xa_alloc_cyclic(&dev->cq_xa, &cq->cqn, cq,
		XA_LIMIT(1, dev->attrs.max_cq - 1), &dev->next_alloc_cqn, GFP_KERNEL);
	if (ret < 0)
		return ret;

	if (udata) {
		struct erdma_ureq_create_cq ureq;
		struct erdma_uresp_create_cq uresp;

		ret = ib_copy_from_udata(&ureq, udata, min(udata->inlen, sizeof(ureq)));
		if (ret)
			goto err_out_xa;
		cq->is_kernel_cq = 0;

		ret = get_mtt_entries(dev, &cq->user_cq.qbuf_mtt, ureq.qbuf_va, ureq.qbuf_len,
			0, ureq.qbuf_va, SZ_64M - SZ_4K, 1);
		if (ret)
			goto err_out_xa;

		ret = erdma_map_user_dbrecords(ctx, ureq.db_record_va, &cq->user_cq.user_dbr_page,
			&cq->user_cq.db_info_dma_addr);
		if (ret) {
			put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
			goto err_out_xa;
		}

		uresp.cq_id = cq->cqn;
		uresp.num_cqe = depth;

		ret = ib_copy_to_udata(udata, &uresp, min(sizeof(uresp), udata->outlen));
		if (ret) {
			erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
			put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
			goto err_out_xa;
		}
	} else {
		cq->is_kernel_cq = 1;
		cq->kern_cq.owner = 1;

		cq->kern_cq.qbuf = dma_alloc_coherent(&dev->pdev->dev,
			(depth << CQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			&cq->kern_cq.qbuf_dma_addr, GFP_KERNEL);
		if (!cq->kern_cq.qbuf) {
			ret = -ENOMEM;
			goto err_out_xa;
		}

		cq->kern_cq.db_info = cq->kern_cq.qbuf + (depth << CQE_SHIFT);
		spin_lock_init(&cq->kern_cq.lock);
		/* use default cqdb. */
		cq->kern_cq.db = dev->func_bar + ERDMA_BAR_CQDB_SPACE_OFFSET;
	}

	ret = create_cq_cmd(dev, cq);
	if (ret)
		goto err_free_res;

	atomic_inc(&dev->num_cq);
	return 0;

err_free_res:
	if (udata) {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
	} else {
		dma_free_coherent(&dev->pdev->dev, (depth << CQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE,
			cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
	}

err_out_xa:
	xa_erase(&dev->cq_xa, cq->cqn);

	return ret;
}

struct net_device *erdma_get_netdev(struct ib_device *device, u32 port_num)
{
	struct erdma_dev *dev = to_edev(device);

	if (dev->netdev)
		dev_hold(dev->netdev);

	return dev->netdev;
}

void erdma_disassociate_ucontext(struct ib_ucontext *ibcontext)
{

}

void erdma_port_event(struct erdma_dev *dev, enum ib_event_type reason)
{
	struct ib_event event;

	event.device = &dev->ibdev;
	event.element.port_num = 1;
	event.event = reason;

	ib_dispatch_event(&event);
}
