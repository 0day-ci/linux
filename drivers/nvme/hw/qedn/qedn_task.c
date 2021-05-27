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
