// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

 /* Kernel includes */
#include <linux/kernel.h>

/* Driver includes */
#include "qedn.h"

extern const struct qed_nvmetcp_ops *qed_ops;

static bool qedn_sgl_has_small_mid_sge(struct nvmetcp_sge *sgl, u16 sge_count)
{
	u16 sge_num;

	if (sge_count > 8) {
		for (sge_num = 0; sge_num < sge_count; sge_num++) {
			if (le32_to_cpu(sgl[sge_num].sge_len) <
			    QEDN_FW_SLOW_IO_MIN_SGE_LIMIT)
				return true; /* small middle SGE found */
		}
	}

	return false; /* no small middle SGEs */
}

static int qedn_init_sgl(struct qedn_ctx *qedn, struct qedn_task_ctx *qedn_task)
{
	struct storage_sgl_task_params *sgl_task_params;
	enum dma_data_direction dma_dir;
	struct scatterlist *sg;
	struct request *rq;
	u16 num_sges;
	int index;
	int rc;

	sgl_task_params = &qedn_task->sgl_task_params;
	rq = blk_mq_rq_from_pdu(qedn_task->req);
	if (qedn_task->task_size == 0) {
		sgl_task_params->num_sges = 0;

		return 0;
	}

	/* Convert BIO to scatterlist */
	num_sges = blk_rq_map_sg(rq->q, rq, qedn_task->nvme_sg);
	if (qedn_task->req_direction == WRITE)
		dma_dir = DMA_TO_DEVICE;
	else
		dma_dir = DMA_FROM_DEVICE;

	/* DMA map the scatterlist */
	if (dma_map_sg(&qedn->pdev->dev, qedn_task->nvme_sg, num_sges, dma_dir) != num_sges) {
		pr_err("Couldn't map sgl\n");
		rc = -EPERM;

		return rc;
	}

	sgl_task_params->total_buffer_size = qedn_task->task_size;
	sgl_task_params->num_sges = num_sges;

	for_each_sg(qedn_task->nvme_sg, sg, num_sges, index) {
		DMA_REGPAIR_LE(sgl_task_params->sgl[index].sge_addr, sg_dma_address(sg));
		sgl_task_params->sgl[index].sge_len = cpu_to_le32(sg_dma_len(sg));
	}

	/* Relevant for Host Write Only */
	sgl_task_params->small_mid_sge = (qedn_task->req_direction == READ) ?
		false :
		qedn_sgl_has_small_mid_sge(sgl_task_params->sgl,
					   sgl_task_params->num_sges);

	return 0;
}

static void qedn_free_nvme_sg(struct qedn_task_ctx *qedn_task)
{
	kfree(qedn_task->nvme_sg);
	qedn_task->nvme_sg = NULL;
}

static void qedn_free_fw_sgl(struct qedn_task_ctx *qedn_task)
{
	struct qedn_ctx *qedn = qedn_task->qedn;
	dma_addr_t sgl_pa;

	sgl_pa = HILO_DMA_REGPAIR(qedn_task->sgl_task_params.sgl_phys_addr);
	dma_free_coherent(&qedn->pdev->dev,
			  QEDN_MAX_FW_SGL_SIZE,
			  qedn_task->sgl_task_params.sgl,
			  sgl_pa);
	qedn_task->sgl_task_params.sgl = NULL;
}

static void qedn_destroy_single_task(struct qedn_task_ctx *qedn_task)
{
	u16 itid;

	itid = qedn_task->itid;
	list_del(&qedn_task->entry);
	qedn_free_nvme_sg(qedn_task);
	qedn_free_fw_sgl(qedn_task);
	kfree(qedn_task);
	qedn_task = NULL;
}

void qedn_destroy_free_tasks(struct qedn_fp_queue *fp_q,
			     struct qedn_io_resources *io_resrc)
{
	struct qedn_task_ctx *qedn_task, *task_tmp;

	/* Destroy tasks from the free task list */
	list_for_each_entry_safe(qedn_task, task_tmp,
				 &io_resrc->task_free_list, entry) {
		qedn_destroy_single_task(qedn_task);
		io_resrc->num_free_tasks -= 1;
	}
}

static int qedn_alloc_nvme_sg(struct qedn_task_ctx *qedn_task)
{
	int rc;

	qedn_task->nvme_sg = kcalloc(QEDN_MAX_SGES_PER_TASK,
				     sizeof(*qedn_task->nvme_sg), GFP_KERNEL);
	if (!qedn_task->nvme_sg) {
		rc = -ENOMEM;

		return rc;
	}

	return 0;
}

static int qedn_alloc_fw_sgl(struct qedn_task_ctx *qedn_task)
{
	struct qedn_ctx *qedn = qedn_task->qedn_conn->qedn;
	dma_addr_t fw_sgl_phys;

	qedn_task->sgl_task_params.sgl =
		dma_alloc_coherent(&qedn->pdev->dev, QEDN_MAX_FW_SGL_SIZE,
				   &fw_sgl_phys, GFP_KERNEL);
	if (!qedn_task->sgl_task_params.sgl) {
		pr_err("Couldn't allocate FW sgl\n");

		return -ENOMEM;
	}

	DMA_REGPAIR_LE(qedn_task->sgl_task_params.sgl_phys_addr, fw_sgl_phys);

	return 0;
}

static inline void *qedn_get_fw_task(struct qed_nvmetcp_tid *info, u16 itid)
{
	return (void *)(info->blocks[itid / info->num_tids_per_block] +
			(itid % info->num_tids_per_block) * info->size);
}

static struct qedn_task_ctx *qedn_alloc_task(struct qedn_conn_ctx *conn_ctx, u16 itid)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct qedn_task_ctx *qedn_task;
	void *fw_task_ctx;
	int rc = 0;

	qedn_task = kzalloc(sizeof(*qedn_task), GFP_KERNEL);
	if (!qedn_task)
		return NULL;

	spin_lock_init(&qedn_task->lock);
	fw_task_ctx = qedn_get_fw_task(&qedn->tasks, itid);
	if (!fw_task_ctx) {
		pr_err("iTID: 0x%x; Failed getting fw_task_ctx memory\n", itid);
		goto release_task;
	}

	/* No need to memset fw_task_ctx - its done in the HSI func */
	qedn_task->qedn_conn = conn_ctx;
	qedn_task->qedn = qedn;
	qedn_task->fw_task_ctx = fw_task_ctx;
	qedn_task->valid = 0;
	qedn_task->flags = 0;
	qedn_task->itid = itid;
	rc = qedn_alloc_fw_sgl(qedn_task);
	if (rc) {
		pr_err("iTID: 0x%x; Failed allocating FW sgl\n", itid);
		goto release_task;
	}

	rc = qedn_alloc_nvme_sg(qedn_task);
	if (rc) {
		pr_err("iTID: 0x%x; Failed allocating FW sgl\n", itid);
		goto release_fw_sgl;
	}

	return qedn_task;

release_fw_sgl:
	qedn_free_fw_sgl(qedn_task);
release_task:
	kfree(qedn_task);

	return NULL;
}

int qedn_alloc_tasks(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct qedn_task_ctx *qedn_task = NULL;
	struct qedn_io_resources *io_resrc;
	u16 itid, start_itid, offset;
	struct qedn_fp_queue *fp_q;
	int i, rc;

	fp_q = conn_ctx->fp_q;

	offset = fp_q->sb_id;
	io_resrc = &fp_q->host_resrc;

	start_itid = qedn->num_tasks_per_pool * offset;
	for (i = 0; i < qedn->num_tasks_per_pool; ++i) {
		itid = start_itid + i;
		qedn_task = qedn_alloc_task(conn_ctx, itid);
		if (!qedn_task) {
			pr_err("Failed allocating task\n");
			rc = -ENOMEM;
			goto release_tasks;
		}

		qedn_task->fp_q = fp_q;
		io_resrc->num_free_tasks += 1;
		list_add_tail(&qedn_task->entry, &io_resrc->task_free_list);
	}

	io_resrc->num_alloc_tasks = io_resrc->num_free_tasks;

	return 0;

release_tasks:
	qedn_destroy_free_tasks(fp_q, io_resrc);

	return rc;
}

void qedn_common_clear_fw_sgl(struct storage_sgl_task_params *sgl_task_params)
{
	u16 sge_cnt = sgl_task_params->num_sges;

	memset(&sgl_task_params->sgl[(sge_cnt - 1)], 0,
	       sizeof(struct nvmetcp_sge));
	sgl_task_params->total_buffer_size = 0;
	sgl_task_params->small_mid_sge = false;
	sgl_task_params->num_sges = 0;
}

inline void qedn_host_reset_cccid_itid_entry(struct qedn_conn_ctx *conn_ctx, u16 cccid, bool async)
{
	conn_ctx->host_cccid_itid[cccid].itid = cpu_to_le16(QEDN_INVALID_ITID);
	if (unlikely(async))
		clear_bit(cccid - NVME_AQ_DEPTH,
			  conn_ctx->async_cccid_idx_map);
}

static int qedn_get_free_idx(struct qedn_conn_ctx *conn_ctx, unsigned int size)
{
	int idx;

	spin_lock(&conn_ctx->async_cccid_bitmap_lock);
	idx = find_first_zero_bit(conn_ctx->async_cccid_idx_map, size);
	if (unlikely(idx >= size)) {
		idx = -1;
		spin_unlock(&conn_ctx->async_cccid_bitmap_lock);
		goto err_idx;
	}
	set_bit(idx, conn_ctx->async_cccid_idx_map);
	spin_unlock(&conn_ctx->async_cccid_bitmap_lock);

err_idx:

	return idx;
}

int qedn_get_free_async_cccid(struct qedn_conn_ctx *conn_ctx)
{
	int async_cccid;

	async_cccid =
		qedn_get_free_idx(conn_ctx, QEDN_MAX_OUTSTAND_ASYNC);
	if (unlikely(async_cccid == QEDN_INVALID_CCCID))
		pr_err("No available CCCID for Async.\n");
	else
		async_cccid += NVME_AQ_DEPTH;

	return async_cccid;
}

inline void qedn_host_set_cccid_itid_entry(struct qedn_conn_ctx *conn_ctx, u16 cccid, u16 itid)
{
	conn_ctx->host_cccid_itid[cccid].itid = cpu_to_le16(itid);
}

inline int qedn_validate_cccid_in_range(struct qedn_conn_ctx *conn_ctx, u16 cccid)
{
	int rc = 0;

	if (unlikely(cccid >= conn_ctx->sq_depth)) {
		pr_err("cccid 0x%x out of range ( > sq depth)\n", cccid);
		rc = -EINVAL;
	}

	return rc;
}

static void qedn_clear_sgl(struct qedn_ctx *qedn,
			   struct qedn_task_ctx *qedn_task)
{
	struct storage_sgl_task_params *sgl_task_params;
	enum dma_data_direction dma_dir;
	u32 sge_cnt;

	sgl_task_params = &qedn_task->sgl_task_params;
	sge_cnt = sgl_task_params->num_sges;

	/* Nothing to do if no SGEs were used */
	if (!qedn_task->task_size || !sge_cnt)
		return;

	dma_dir = (qedn_task->req_direction == WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	dma_unmap_sg(&qedn->pdev->dev, qedn_task->nvme_sg, sge_cnt, dma_dir);
	memset(&qedn_task->nvme_sg[(sge_cnt - 1)], 0, sizeof(struct scatterlist));
	qedn_common_clear_fw_sgl(sgl_task_params);
	qedn_task->task_size = 0;
}

static void qedn_clear_task(struct qedn_conn_ctx *conn_ctx,
			    struct qedn_task_ctx *qedn_task)
{
	/* Task lock isn't needed since it is no longer in use */
	qedn_clear_sgl(conn_ctx->qedn, qedn_task);
	qedn_task->valid = 0;
	qedn_task->flags = 0;

	atomic_dec(&conn_ctx->num_active_tasks);
}

void qedn_return_active_tasks(struct qedn_conn_ctx *conn_ctx)
{
	struct qedn_fp_queue *fp_q = conn_ctx->fp_q;
	struct qedn_task_ctx *qedn_task, *task_tmp;
	struct qedn_io_resources *io_resrc;
	int num_returned_tasks = 0;
	int num_active_tasks;

	io_resrc = &fp_q->host_resrc;

	/* Return tasks that aren't "Used by FW" to the pool */
	list_for_each_entry_safe(qedn_task, task_tmp,
				 &conn_ctx->active_task_list, entry) {
		/* If we got this far, cleanup was already done
		 * in which case we want to return the task to the pool and
		 * release it. So we make sure the cleanup indication is down
		 */
		clear_bit(QEDN_TASK_WAIT_FOR_CLEANUP, &qedn_task->flags);

		/* Special handling in case of ICREQ task */
		if (unlikely(conn_ctx->state ==	CONN_STATE_WAIT_FOR_IC_COMP &&
			     test_bit(QEDN_TASK_IS_ICREQ, &(qedn_task)->flags)))
			qedn_common_clear_fw_sgl(&qedn_task->sgl_task_params);

		qedn_clear_task(conn_ctx, qedn_task);
		num_returned_tasks++;
	}

	if (num_returned_tasks) {
		spin_lock(&io_resrc->resources_lock);
		/* Return tasks to FP_Q pool in one shot */

		list_splice_tail_init(&conn_ctx->active_task_list,
				      &io_resrc->task_free_list);
		io_resrc->num_free_tasks += num_returned_tasks;
		spin_unlock(&io_resrc->resources_lock);
	}

	num_active_tasks = atomic_read(&conn_ctx->num_active_tasks);
	if (num_active_tasks)
		pr_err("num_active_tasks is %u after cleanup.\n", num_active_tasks);
}

void qedn_return_task_to_pool(struct qedn_conn_ctx *conn_ctx,
			      struct qedn_task_ctx *qedn_task)
{
	struct qedn_fp_queue *fp_q = conn_ctx->fp_q;
	struct qedn_io_resources *io_resrc;
	unsigned long lock_flags;
	bool async;

	io_resrc = &fp_q->host_resrc;

	spin_lock_irqsave(&qedn_task->lock, lock_flags);
	async = test_bit(QEDN_TASK_ASYNC, &(qedn_task)->flags);
	qedn_task->valid = 0;
	qedn_task->flags = 0;
	qedn_clear_sgl(conn_ctx->qedn, qedn_task);
	spin_unlock_irqrestore(&qedn_task->lock, lock_flags);

	spin_lock(&conn_ctx->task_list_lock);
	list_del(&qedn_task->entry);
	qedn_host_reset_cccid_itid_entry(conn_ctx, qedn_task->cccid, async);
	spin_unlock(&conn_ctx->task_list_lock);

	atomic_dec(&conn_ctx->num_active_tasks);
	atomic_dec(&conn_ctx->num_active_fw_tasks);

	spin_lock(&io_resrc->resources_lock);
	list_add_tail(&qedn_task->entry, &io_resrc->task_free_list);
	io_resrc->num_free_tasks += 1;
	spin_unlock(&io_resrc->resources_lock);
}

struct qedn_task_ctx *
qedn_get_free_task_from_pool(struct qedn_conn_ctx *conn_ctx, u16 cccid)
{
	struct qedn_task_ctx *qedn_task = NULL;
	struct qedn_io_resources *io_resrc;
	struct qedn_fp_queue *fp_q;

	fp_q = conn_ctx->fp_q;
	io_resrc = &fp_q->host_resrc;

	spin_lock(&io_resrc->resources_lock);
	qedn_task = list_first_entry_or_null(&io_resrc->task_free_list,
					     struct qedn_task_ctx, entry);
	if (unlikely(!qedn_task)) {
		spin_unlock(&io_resrc->resources_lock);

		return NULL;
	}
	list_del(&qedn_task->entry);
	io_resrc->num_free_tasks -= 1;
	spin_unlock(&io_resrc->resources_lock);

	spin_lock(&conn_ctx->task_list_lock);
	list_add_tail(&qedn_task->entry, &conn_ctx->active_task_list);
	qedn_host_set_cccid_itid_entry(conn_ctx, cccid, qedn_task->itid);
	spin_unlock(&conn_ctx->task_list_lock);

	atomic_inc(&conn_ctx->num_active_tasks);
	qedn_task->cccid = cccid;
	qedn_task->qedn_conn = conn_ctx;
	qedn_task->valid = 1;

	return qedn_task;
}

void qedn_send_async_event_cmd(struct qedn_task_ctx *qedn_task,
			       struct qedn_conn_ctx *conn_ctx)
{
	struct nvme_tcp_ofld_req *async_req = qedn_task->req;
	struct nvme_command *nvme_cmd = &async_req->nvme_cmd;
	struct storage_sgl_task_params *sgl_task_params;
	struct nvmetcp_task_params task_params;
	struct nvme_tcp_cmd_pdu cmd_hdr;
	struct nvmetcp_wqe *chain_sqe;
	struct nvmetcp_wqe local_sqe;

	set_bit(QEDN_TASK_ASYNC, &qedn_task->flags);
	nvme_cmd->common.command_id = qedn_task->cccid;
	qedn_task->task_size = 0;

	/* Initialize sgl params */
	sgl_task_params = &qedn_task->sgl_task_params;
	sgl_task_params->total_buffer_size = 0;
	sgl_task_params->num_sges = 0;
	sgl_task_params->small_mid_sge = false;

	task_params.opq.lo = cpu_to_le32(((u64)(qedn_task)) & 0xffffffff);
	task_params.opq.hi = cpu_to_le32(((u64)(qedn_task)) >> 32);

	/* Initialize task params */
	task_params.context = qedn_task->fw_task_ctx;
	task_params.sqe = &local_sqe;
	task_params.tx_io_size = 0;
	task_params.rx_io_size = 0;
	task_params.conn_icid = (u16)conn_ctx->conn_handle;
	task_params.itid = qedn_task->itid;
	task_params.cq_rss_number = conn_ctx->default_cq;
	task_params.send_write_incapsule = 0;

	/* Internal impl. - async is treated like zero len read */
	cmd_hdr.hdr.type = nvme_tcp_cmd;
	cmd_hdr.hdr.flags = 0;
	cmd_hdr.hdr.hlen = sizeof(cmd_hdr);
	cmd_hdr.hdr.pdo = 0x0;
	cmd_hdr.hdr.plen = cpu_to_le32(cmd_hdr.hdr.hlen);

	qed_ops->init_read_io(&task_params, &cmd_hdr, nvme_cmd,
			      &qedn_task->sgl_task_params);

	set_bit(QEDN_TASK_USED_BY_FW, &qedn_task->flags);
	atomic_inc(&conn_ctx->num_active_fw_tasks);

	spin_lock(&conn_ctx->ep.doorbell_lock);
	chain_sqe = qed_chain_produce(&conn_ctx->ep.fw_sq_chain);
	memcpy(chain_sqe, &local_sqe, sizeof(local_sqe));
	qedn_ring_doorbell(conn_ctx);
	spin_unlock(&conn_ctx->ep.doorbell_lock);
}

int qedn_send_read_cmd(struct qedn_task_ctx *qedn_task, struct qedn_conn_ctx *conn_ctx)
{
	struct nvme_command *nvme_cmd = &qedn_task->req->nvme_cmd;
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct nvmetcp_task_params task_params;
	struct nvme_tcp_cmd_pdu cmd_hdr;
	struct nvmetcp_wqe *chain_sqe;
	struct nvmetcp_wqe local_sqe;
	int rc;

	rc = qedn_init_sgl(qedn, qedn_task);
	if (rc)
		return rc;

	task_params.opq.lo = cpu_to_le32(((u64)(qedn_task)) & 0xffffffff);
	task_params.opq.hi = cpu_to_le32(((u64)(qedn_task)) >> 32);

	/* Initialize task params */
	task_params.context = qedn_task->fw_task_ctx;
	task_params.sqe = &local_sqe;
	task_params.tx_io_size = 0;
	task_params.rx_io_size = qedn_task->task_size;
	task_params.conn_icid = (u16)conn_ctx->conn_handle;
	task_params.itid = qedn_task->itid;
	task_params.cq_rss_number = conn_ctx->default_cq;
	task_params.send_write_incapsule = 0;

	cmd_hdr.hdr.type = nvme_tcp_cmd;
	cmd_hdr.hdr.flags = 0;
	cmd_hdr.hdr.hlen = sizeof(cmd_hdr);
	cmd_hdr.hdr.pdo = 0x0;
	cmd_hdr.hdr.plen = cpu_to_le32(cmd_hdr.hdr.hlen);

	qed_ops->init_read_io(&task_params, &cmd_hdr, nvme_cmd,
			      &qedn_task->sgl_task_params);

	set_bit(QEDN_TASK_USED_BY_FW, &qedn_task->flags);
	atomic_inc(&conn_ctx->num_active_fw_tasks);

	spin_lock(&conn_ctx->ep.doorbell_lock);
	chain_sqe = qed_chain_produce(&conn_ctx->ep.fw_sq_chain);
	memcpy(chain_sqe, &local_sqe, sizeof(local_sqe));
	qedn_ring_doorbell(conn_ctx);
	spin_unlock(&conn_ctx->ep.doorbell_lock);

	return 0;
}

int qedn_send_write_cmd(struct qedn_task_ctx *qedn_task, struct qedn_conn_ctx *conn_ctx)
{
	struct nvme_command *nvme_cmd = &qedn_task->req->nvme_cmd;
	struct nvmetcp_task_params task_params;
	struct qedn_ctx *qedn = conn_ctx->qedn;
	struct nvme_tcp_cmd_pdu cmd_hdr;
	u32 pdu_len = sizeof(cmd_hdr);
	struct nvmetcp_wqe *chain_sqe;
	struct nvmetcp_wqe local_sqe;
	u8 send_write_incapsule;
	int rc;

	if (qedn_task->task_size <= nvme_tcp_ofld_inline_data_size(conn_ctx->queue) &&
	    qedn_task->task_size) {
		send_write_incapsule = 1;
		pdu_len += qedn_task->task_size;

		/* Add digest length once supported */
		cmd_hdr.hdr.pdo = sizeof(cmd_hdr);
	} else {
		send_write_incapsule = 0;

		cmd_hdr.hdr.pdo = 0x0;
	}

	rc = qedn_init_sgl(qedn, qedn_task);
	if (rc)
		return rc;

	task_params.host_cccid = cpu_to_le16(qedn_task->cccid);
	task_params.opq.lo = cpu_to_le32(((u64)(qedn_task)) & 0xffffffff);
	task_params.opq.hi = cpu_to_le32(((u64)(qedn_task)) >> 32);

	/* Initialize task params */
	task_params.context = qedn_task->fw_task_ctx;
	task_params.sqe = &local_sqe;
	task_params.tx_io_size = qedn_task->task_size;
	task_params.rx_io_size = 0;
	task_params.conn_icid = (u16)conn_ctx->conn_handle;
	task_params.itid = qedn_task->itid;
	task_params.cq_rss_number = conn_ctx->default_cq;
	task_params.send_write_incapsule = send_write_incapsule;

	cmd_hdr.hdr.type = nvme_tcp_cmd;
	cmd_hdr.hdr.flags = 0;
	cmd_hdr.hdr.hlen = sizeof(cmd_hdr);
	cmd_hdr.hdr.plen = cpu_to_le32(pdu_len);

	qed_ops->init_write_io(&task_params, &cmd_hdr, nvme_cmd,
			       &qedn_task->sgl_task_params);

	set_bit(QEDN_TASK_USED_BY_FW, &qedn_task->flags);
	atomic_inc(&conn_ctx->num_active_fw_tasks);

	spin_lock(&conn_ctx->ep.doorbell_lock);
	chain_sqe = qed_chain_produce(&conn_ctx->ep.fw_sq_chain);
	memcpy(chain_sqe, &local_sqe, sizeof(local_sqe));
	qedn_ring_doorbell(conn_ctx);
	spin_unlock(&conn_ctx->ep.doorbell_lock);

	return 0;
}

static void qedn_return_error_req(struct nvme_tcp_ofld_req *req)
{
	__le16 status = cpu_to_le16(NVME_SC_HOST_PATH_ERROR << 1);
	union nvme_result res = {};

	if (!req)
		return;

	/* Call request done to compelete the request */
	if (req->done)
		req->done(req, &res, status);
	else
		pr_err("request done not set !!!\n");
}

int qedn_queue_request(struct qedn_conn_ctx *qedn_conn, struct nvme_tcp_ofld_req *req)
{
	struct qedn_task_ctx *qedn_task;
	struct request *rq;
	int rc = 0;
	u16 cccid;

	rq = blk_mq_rq_from_pdu(req);

	if (unlikely(req->async)) {
		cccid = qedn_get_free_async_cccid(qedn_conn);
		if (cccid == QEDN_INVALID_CCCID) {
			qedn_return_error_req(req);

			return BLK_STS_NOTSUPP;
		}
	} else {
		cccid = rq->tag;
	}

	qedn_task = qedn_get_free_task_from_pool(qedn_conn, cccid);
	if (unlikely(!qedn_task)) {
		pr_err("Not able to allocate task context resource\n");

		return BLK_STS_NOTSUPP;
	}

	req->private_data = qedn_task;
	qedn_task->req = req;

	if (unlikely(req->async)) {
		qedn_send_async_event_cmd(qedn_task, qedn_conn);

		return BLK_STS_TRANSPORT;
	}

	/* Check if there are physical segments in request to determine the task size.
	 * The logic of nvme_tcp_set_sg_null() will be implemented as part of
	 * qedn_set_sg_host_data().
	 */
	qedn_task->task_size = blk_rq_nr_phys_segments(rq) ? blk_rq_payload_bytes(rq) : 0;
	qedn_task->req_direction = rq_data_dir(rq);
	if (qedn_task->req_direction == WRITE)
		rc = qedn_send_write_cmd(qedn_task, qedn_conn);
	else
		rc = qedn_send_read_cmd(qedn_task, qedn_conn);

	if (unlikely(rc)) {
		pr_err("Read/Write command failure\n");

		return BLK_STS_TRANSPORT;
	}

	spin_lock(&qedn_conn->ep.doorbell_lock);
	qedn_ring_doorbell(qedn_conn);
	spin_unlock(&qedn_conn->ep.doorbell_lock);

	return BLK_STS_OK;
}

struct qedn_task_ctx *qedn_cqe_get_active_task(struct nvmetcp_fw_cqe *cqe)
{
	struct regpair *p = &cqe->task_opaque;

	return (struct qedn_task_ctx *)((((u64)(le32_to_cpu(p->hi)) << 32)
					+ le32_to_cpu(p->lo)));
}

static struct nvme_tcp_ofld_req *qedn_decouple_req_task(struct qedn_task_ctx *qedn_task)
{
	struct nvme_tcp_ofld_req *ulp_req = qedn_task->req;

	qedn_task->req = NULL;
	if (ulp_req)
		ulp_req->private_data = NULL;

	return ulp_req;
}

static inline int qedn_comp_valid_task(struct qedn_task_ctx *qedn_task,
				       union nvme_result *result, __le16 status)
{
	struct qedn_conn_ctx *conn_ctx = qedn_task->qedn_conn;
	struct nvme_tcp_ofld_req *req;

	req = qedn_decouple_req_task(qedn_task);
	qedn_return_task_to_pool(conn_ctx, qedn_task);
	if (!req) {
		pr_err("req not found\n");

		return -EINVAL;
	}

	/* Call request done to complete the request */
	if (req->done)
		req->done(req, result, status);
	else
		pr_err("request done not Set !!!\n");

	return 0;
}

int qedn_process_nvme_cqe(struct qedn_task_ctx *qedn_task, struct nvme_completion *cqe)
{
	struct qedn_conn_ctx *conn_ctx = qedn_task->qedn_conn;
	struct nvme_tcp_ofld_req *req;
	int rc = 0;
	bool async;

	async = test_bit(QEDN_TASK_ASYNC, &(qedn_task)->flags);

	/* CQE arrives swapped
	 * Swapping requirement will be removed in future FW versions
	 */
	qedn_swap_bytes((u32 *)cqe, (sizeof(*cqe) / sizeof(u32)));

	if (unlikely(async)) {
		qedn_return_task_to_pool(conn_ctx, qedn_task);
		req = qedn_task->req;
		if (req->done)
			req->done(req, &cqe->result, cqe->status);
		else
			pr_err("request done not set for async request !!!\n");
	} else {
		rc = qedn_comp_valid_task(qedn_task, &cqe->result, cqe->status);
	}

	return rc;
}

int qedn_complete_c2h(struct qedn_task_ctx *qedn_task)
{
	int rc = 0;

	__le16 status = cpu_to_le16(NVME_SC_SUCCESS << 1);
	union nvme_result result = {};

	rc = qedn_comp_valid_task(qedn_task, &result, status);

	return rc;
}

void qedn_io_work_cq(struct qedn_ctx *qedn, struct nvmetcp_fw_cqe *cqe)
{
	int rc = 0;

	struct nvme_completion *nvme_cqe = NULL;
	struct qedn_task_ctx *qedn_task = NULL;
	struct qedn_conn_ctx *conn_ctx = NULL;
	u16 itid;
	u32 cid;

	conn_ctx = qedn_get_conn_hash(qedn, le16_to_cpu(cqe->conn_id));
	if (unlikely(!conn_ctx)) {
		pr_err("CID 0x%x: Failed to fetch conn_ctx from hash\n",
		       le16_to_cpu(cqe->conn_id));

		return;
	}

	cid = conn_ctx->fw_cid;
	itid = le16_to_cpu(cqe->itid);
	qedn_task = qedn_cqe_get_active_task(cqe);
	if (unlikely(!qedn_task))
		return;

	if (likely(cqe->cqe_type == NVMETCP_FW_CQE_TYPE_NORMAL)) {
		if (unlikely(test_bit(QEDN_TASK_WAIT_FOR_CLEANUP, &qedn_task->flags)))
			return;

		switch (cqe->task_type) {
		case NVMETCP_TASK_TYPE_HOST_WRITE:
		case NVMETCP_TASK_TYPE_HOST_READ:

			/* Verify data digest once supported */

			nvme_cqe = (struct nvme_completion *)&cqe->cqe_data.nvme_cqe;
			rc = qedn_process_nvme_cqe(qedn_task, nvme_cqe);
			if (rc) {
				pr_err("Read/Write completion error\n");

				return;
			}
			break;

		case NVMETCP_TASK_TYPE_HOST_READ_NO_CQE:

			/* Verify data digest once supported */

			rc = qedn_complete_c2h(qedn_task);
			if (rc) {
				pr_err("Controller To Host Data Transfer error error\n");

				return;
			}

			break;

		case NVMETCP_TASK_TYPE_INIT_CONN_REQUEST:
			/* Clear ICReq-padding SGE from SGL */
			qedn_common_clear_fw_sgl(&qedn_task->sgl_task_params);
			/* Task is not required for icresp processing */
			qedn_return_task_to_pool(conn_ctx, qedn_task);
			qedn_prep_icresp(conn_ctx, cqe);
			break;
		default:
			pr_info("Could not identify task type\n");
		}
	} else {
		if (cqe->cqe_type == NVMETCP_FW_CQE_TYPE_CLEANUP) {
			clear_bit(QEDN_TASK_WAIT_FOR_CLEANUP, &qedn_task->flags);
			qedn_return_task_to_pool(conn_ctx, qedn_task);
			atomic_dec(&conn_ctx->task_cleanups_cnt);
			wake_up_interruptible(&conn_ctx->cleanup_waitq);

			return;
		}

		 /* The else is NVMETCP_FW_CQE_TYPE_DUMMY - in which don't return the task.
		  * The task will return during NVMETCP_FW_CQE_TYPE_CLEANUP.
		  */
	}
}
