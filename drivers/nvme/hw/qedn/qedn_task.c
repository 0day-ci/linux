// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

 /* Kernel includes */
#include <linux/kernel.h>

/* Driver includes */
#include "qedn.h"

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

inline void qedn_host_reset_cccid_itid_entry(struct qedn_conn_ctx *conn_ctx,
					     u16 cccid)
{
	conn_ctx->host_cccid_itid[cccid].itid = cpu_to_le16(QEDN_INVALID_ITID);
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

	io_resrc = &fp_q->host_resrc;

	spin_lock_irqsave(&qedn_task->lock, lock_flags);
	qedn_task->valid = 0;
	qedn_task->flags = 0;
	qedn_clear_sgl(conn_ctx->qedn, qedn_task);
	spin_unlock_irqrestore(&qedn_task->lock, lock_flags);

	spin_lock(&conn_ctx->task_list_lock);
	list_del(&qedn_task->entry);
	qedn_host_reset_cccid_itid_entry(conn_ctx, qedn_task->cccid);
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

int qedn_queue_request(struct qedn_conn_ctx *qedn_conn, struct nvme_tcp_ofld_req *req)
{
	/* Process the request */

	return 0;
}

struct qedn_task_ctx *qedn_cqe_get_active_task(struct nvmetcp_fw_cqe *cqe)
{
	struct regpair *p = &cqe->task_opaque;

	return (struct qedn_task_ctx *)((((u64)(le32_to_cpu(p->hi)) << 32)
					+ le32_to_cpu(p->lo)));
}

void qedn_io_work_cq(struct qedn_ctx *qedn, struct nvmetcp_fw_cqe *cqe)
{
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
		/* Placeholder - verify the connection was established */

		switch (cqe->task_type) {
		case NVMETCP_TASK_TYPE_HOST_WRITE:
		case NVMETCP_TASK_TYPE_HOST_READ:

			/* Placeholder - IO flow */

			break;

		case NVMETCP_TASK_TYPE_HOST_READ_NO_CQE:

			/* Placeholder - IO flow */

			break;

		case NVMETCP_TASK_TYPE_INIT_CONN_REQUEST:

			/* Placeholder - ICReq flow */

			break;
		default:
			pr_info("Could not identify task type\n");
		}
	} else {
		/* Placeholder - Recovery flows */
	}
}
