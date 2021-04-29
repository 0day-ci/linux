// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

 /* Kernel includes */
#include <linux/kernel.h>

/* Driver includes */
#include "qedn.h"

inline int qedn_validate_cccid_in_range(struct qedn_conn_ctx *conn_ctx, u16 cccid)
{
	int rc = 0;

	if (unlikely(cccid >= conn_ctx->sq_depth)) {
		pr_err("cccid 0x%x out of range ( > sq depth)\n", cccid);
		rc = -EINVAL;
	}

	return rc;
}

static bool qedn_process_req(struct qedn_conn_ctx *qedn_conn)
{
	return true;
}

/* The WQ handler can be call from 3 flows:
 *	1. queue_rq.
 *	2. async.
 *	3. self requeued
 * Try to send requests from the pending list. If a request proccess has failed,
 * re-register to the workqueue.
 * If there are no additional pending requests - exit the handler.
 */
void qedn_nvme_req_fp_wq_handler(struct work_struct *work)
{
	struct qedn_conn_ctx *qedn_conn;
	bool more = false;

	qedn_conn = container_of(work, struct qedn_conn_ctx, nvme_req_fp_wq_entry);
	do {
		if (mutex_trylock(&qedn_conn->nvme_req_mutex)) {
			more = qedn_process_req(qedn_conn);
			qedn_conn->req = NULL;
			mutex_unlock(&qedn_conn->nvme_req_mutex);
		}
	} while (more);

	if (!list_empty(&qedn_conn->host_pend_req_list))
		queue_work_on(qedn_conn->cpu, qedn_conn->nvme_req_fp_wq,
			      &qedn_conn->nvme_req_fp_wq_entry);
}

void qedn_queue_request(struct qedn_conn_ctx *qedn_conn, struct nvme_tcp_ofld_req *req)
{
	bool empty, res = false;

	spin_lock(&qedn_conn->nvme_req_lock);
	empty = list_empty(&qedn_conn->host_pend_req_list) && !qedn_conn->req;
	list_add_tail(&req->queue_entry, &qedn_conn->host_pend_req_list);
	spin_unlock(&qedn_conn->nvme_req_lock);

	/* attempt workqueue bypass */
	if (qedn_conn->cpu == smp_processor_id() && empty &&
	    mutex_trylock(&qedn_conn->nvme_req_mutex)) {
		res = qedn_process_req(qedn_conn);
		qedn_conn->req = NULL;
		mutex_unlock(&qedn_conn->nvme_req_mutex);
		if (res || list_empty(&qedn_conn->host_pend_req_list))
			return;
	} else if (req->last) {
		queue_work_on(qedn_conn->cpu, qedn_conn->nvme_req_fp_wq,
			      &qedn_conn->nvme_req_fp_wq_entry);
	}
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
