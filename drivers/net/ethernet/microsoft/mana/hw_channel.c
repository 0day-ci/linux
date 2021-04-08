// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include "gdma.h"
#include "hw_channel.h"

static int hwc_get_msg_index(struct hw_channel_context *hwc, u16 *msg_idx)
{
	struct gdma_resource *r = &hwc->inflight_msg_res;
	unsigned long flags;
	u32 index;

	down(&hwc->sema);

	spin_lock_irqsave(&r->lock, flags);

	index = find_first_zero_bit(hwc->inflight_msg_res.map,
				    hwc->inflight_msg_res.size);

	bitmap_set(hwc->inflight_msg_res.map, index, 1);

	spin_unlock_irqrestore(&r->lock, flags);

	*msg_idx = index;

	return 0;
}

static void hwc_put_msg_index(struct hw_channel_context *hwc, u16 msg_idx)
{
	struct gdma_resource *r = &hwc->inflight_msg_res;
	unsigned long flags;

	spin_lock_irqsave(&r->lock, flags);
	bitmap_clear(hwc->inflight_msg_res.map, msg_idx, 1);
	spin_unlock_irqrestore(&r->lock, flags);

	up(&hwc->sema);
}

static int hwc_verify_resp_msg(const struct hwc_caller_ctx *caller_ctx,
			       u32 resp_msglen,
			       const struct gdma_resp_hdr *resp_msg)
{
	if (resp_msglen < sizeof(*resp_msg))
		return -EPROTO;

	if (resp_msglen > caller_ctx->output_buflen)
		return -EPROTO;

	return 0;
}

static void hwc_handle_resp(struct hw_channel_context *hwc, u32 resp_msglen,
			    const struct gdma_resp_hdr *resp_msg)
{
	struct hwc_caller_ctx *ctx;
	int err = -EPROTO;

	if (!test_bit(resp_msg->response.hwc_msg_id,
		      hwc->inflight_msg_res.map)) {
		dev_err(hwc->dev, "hwc_rx: invalid msg_id = %u\n",
			resp_msg->response.hwc_msg_id);
		return;
	}

	ctx = hwc->caller_ctx + resp_msg->response.hwc_msg_id;
	err = hwc_verify_resp_msg(ctx, resp_msglen, resp_msg);
	if (err)
		goto out;

	ctx->status_code = resp_msg->status;

	memcpy(ctx->output_buf, resp_msg, resp_msglen);

out:
	ctx->error = err;
	complete(&ctx->comp_event);
}

static int hwc_post_rx_wqe(const struct hwc_wq *hwc_rxq,
			   struct hwc_work_request *req)
{
	struct device *dev = hwc_rxq->hwc->dev;
	struct gdma_sge *sge;
	int err;

	sge = &req->sge;
	sge->address = (u64)req->buf_sge_addr;
	sge->mem_key = hwc_rxq->msg_buf->gpa_mkey;
	sge->size = req->buf_len;

	memset(&req->wqe_req, 0, sizeof(struct gdma_wqe_request));
	req->wqe_req.sgl = sge;
	req->wqe_req.num_sge = 1;
	req->wqe_req.client_data_unit = 0;

	err = gdma_post_and_ring(hwc_rxq->gdma_wq, &req->wqe_req, NULL);
	if (err)
		dev_err(dev, "Failed to post WQE on HWC RQ: %d\n", err);

	return err;
}

static void hwc_init_event_handler(void *ctx, struct gdma_queue *q_self,
				   struct gdma_event *event)
{
	struct hw_channel_context *hwc = ctx;
	struct gdma_dev *gd = hwc->gdma_dev;
	union hwc_init_type_data type_data;
	union hwc_init_eq_id_db eq_db;
	struct gdma_context *gc;
	u32 type, val;

	switch (event->type) {
	case GDMA_EQE_HWC_INIT_EQ_ID_DB:
		eq_db.as_uint32 = event->details[0];
		hwc->cq->gdma_eq->id = eq_db.eq_id;
		gd->doorbell = eq_db.doorbell;
		break;

	case GDMA_EQE_HWC_INIT_DATA:

		type_data.as_uint32 = event->details[0];
		type = type_data.type;
		val = type_data.value;

		switch (type) {
		case HWC_INIT_DATA_CQID:
			hwc->cq->gdma_cq->id = val;
			break;

		case HWC_INIT_DATA_RQID:
			hwc->rxq->gdma_wq->id = val;
			break;

		case HWC_INIT_DATA_SQID:
			hwc->txq->gdma_wq->id = val;
			break;

		case HWC_INIT_DATA_QUEUE_DEPTH:
			hwc->hwc_init_q_depth_max = (u16)val;
			break;

		case HWC_INIT_DATA_MAX_REQUEST:
			hwc->hwc_init_max_req_msg_size = val;
			break;

		case HWC_INIT_DATA_MAX_RESPONSE:
			hwc->hwc_init_max_resp_msg_size = val;
			break;

		case HWC_INIT_DATA_MAX_NUM_CQS:
			gc = hwc_to_gdma_context(gd);
			gc->max_num_cq = val;
			break;

		case HWC_INIT_DATA_PDID:
			hwc->gdma_dev->pdid = val;
			break;

		case HWC_INIT_DATA_GPA_MKEY:
			hwc->rxq->msg_buf->gpa_mkey = val;
			hwc->txq->msg_buf->gpa_mkey = val;
			break;
		}

		break;

	case GDMA_EQE_HWC_INIT_DONE:
		complete(&hwc->hwc_init_eqe_comp);
		break;

	default:
		WARN_ON(1);
		break;
	}
}

static void hwc_rx_event_handler(void *ctx, u32 gdma_rxq_id,
				 const struct hwc_rx_oob *rx_oob)
{
	struct hw_channel_context *hwc = ctx;
	struct hwc_wq *hwc_rxq = hwc->rxq;
	struct hwc_work_request *rx_req;
	struct gdma_resp_hdr *resp;
	struct gdma_wqe *dma_oob;
	struct gdma_queue *rq;
	struct gdma_sge *sge;
	u64 rq_base_addr;
	u64 rx_req_idx;
	u16 msg_id;
	u8 *wqe;

	if (WARN_ON(hwc_rxq->gdma_wq->id != gdma_rxq_id))
		return;

	rq = hwc_rxq->gdma_wq;
	wqe = gdma_get_wqe_ptr(rq, rx_oob->wqe_offset / GDMA_WQE_BU_SIZE);
	dma_oob = (struct gdma_wqe *)wqe;

	sge = (struct gdma_sge *)(wqe + 8 + dma_oob->inline_oob_size_div4 * 4);
	WARN_ON(dma_oob->inline_oob_size_div4 != 2 &&
		dma_oob->inline_oob_size_div4 != 6);

	/* Select the rx WorkRequest for access to virtual address if not in SGE
	 * and for reposting.  The receive reqs index may not match
	 * channel msg_id if sender posted send WQE's out of order. The rx WR
	 * that should be recycled here is the one we're currently using. Its
	 * index can be calculated based on the current address's location in
	 * the memory region.
	 */
	rq_base_addr = hwc_rxq->msg_buf->mem_info.dma_handle;
	rx_req_idx = (sge->address - rq_base_addr) / hwc->max_req_msg_size;

	rx_req = &hwc_rxq->msg_buf->reqs[rx_req_idx];
	resp = (struct gdma_resp_hdr *)rx_req->buf_va;

	if (resp->response.hwc_msg_id >= hwc->num_inflight_msg) {
		dev_err(hwc->dev, "HWC RX: wrong msg_id=%u\n",
			resp->response.hwc_msg_id);
		return;
	}

	hwc_handle_resp(hwc, rx_oob->tx_oob_data_size, resp);

	msg_id = resp->response.hwc_msg_id;
	resp = NULL;

	hwc_post_rx_wqe(hwc_rxq, rx_req);

	hwc_put_msg_index(hwc, msg_id);
}

static void hwc_tx_event_handler(void *ctx, u32 gdma_txq_id,
				 const struct hwc_rx_oob *rx_oob)
{
	struct hw_channel_context *hwc = ctx;
	struct hwc_wq *hwc_txq = hwc->txq;

	WARN_ON(!hwc_txq || hwc_txq->gdma_wq->id != gdma_txq_id);
}

static int hwc_create_gdma_wq(struct hw_channel_context *hwc,
			      enum gdma_queue_type type, u64 queue_size,
			      struct gdma_queue **queue)
{
	struct gdma_queue_spec spec = {};

	if (type != GDMA_SQ && type != GDMA_RQ)
		return -EINVAL;

	spec.type = type;
	spec.monitor_avl_buf = false;
	spec.queue_size = queue_size;

	return gdma_create_hwc_queue(hwc->gdma_dev, &spec, queue);
}

static int hwc_create_gdma_cq(struct hw_channel_context *hwc, u64 queue_size,
			      void *ctx, gdma_cq_callback *cb,
			      struct gdma_queue *parent_eq,
			      struct gdma_queue **queue)
{
	struct gdma_queue_spec spec = {};

	spec.type = GDMA_CQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = queue_size;
	spec.cq.context = ctx;
	spec.cq.callback = cb;
	spec.cq.parent_eq = parent_eq;

	return gdma_create_hwc_queue(hwc->gdma_dev, &spec, queue);
}

static int hwc_create_gdma_eq(struct hw_channel_context *hwc, u64 queue_size,
			      void *ctx, gdma_eq_callback *cb,
			      struct gdma_queue **queue)
{
	struct gdma_queue_spec spec = {};

	spec.type = GDMA_EQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = queue_size;
	spec.eq.context = ctx;
	spec.eq.callback = cb;
	spec.eq.log2_throttle_limit = DEFAULT_LOG2_THROTTLING_FOR_ERROR_EQ;

	return gdma_create_hwc_queue(hwc->gdma_dev, &spec, queue);
}

static void hwc_comp_event(void *ctx, struct gdma_queue *q_self)
{
	struct hwc_rx_oob comp_data = {};
	struct gdma_comp *completions;
	struct hwc_cq *hwc_cq = ctx;
	u32 comp_read, i;

	WARN_ON(hwc_cq->gdma_cq != q_self);

	completions = hwc_cq->comp_buf;
	comp_read = gdma_poll_cq(q_self, completions, hwc_cq->queue_depth);
	WARN_ON(comp_read <= 0 || comp_read > hwc_cq->queue_depth);

	for (i = 0; i < comp_read; ++i) {
		comp_data = *(struct hwc_rx_oob *)completions[i].cqe_data;

		if (completions[i].is_sq)
			hwc_cq->tx_event_handler(hwc_cq->tx_event_ctx,
						completions[i].wq_num,
						&comp_data);
		else
			hwc_cq->rx_event_handler(hwc_cq->rx_event_ctx,
						completions[i].wq_num,
						&comp_data);
	}

	gdma_arm_cq(q_self);
}

static void hwc_destroy_cq(struct gdma_context *gc, struct hwc_cq *hwc_cq)
{
	if (!hwc_cq)
		return;

	kfree(hwc_cq->comp_buf);

	if (hwc_cq->gdma_cq)
		gdma_destroy_queue(gc, hwc_cq->gdma_cq);

	if (hwc_cq->gdma_eq)
		gdma_destroy_queue(gc, hwc_cq->gdma_eq);

	kfree(hwc_cq);
}

static int hwc_create_cq(struct hw_channel_context *hwc, u16 q_depth,
			 gdma_eq_callback *callback, void *ctx,
			 hwc_rx_event_handler_t *rx_ev_hdlr, void *rx_ev_ctx,
			 hwc_tx_event_handler_t *tx_ev_hdlr, void *tx_ev_ctx,
			 struct hwc_cq **hwc_cq_p)
{
	struct gdma_queue *eq, *cq;
	struct gdma_comp *comp_buf;
	struct hwc_cq *hwc_cq;
	u32 eq_size, cq_size;
	int err;

	eq_size = roundup_pow_of_two(GDMA_EQE_SIZE * q_depth);
	WARN_ON(eq_size != 16 * 2 * HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH);
	if (eq_size < MINIMUM_SUPPORTED_PAGE_SIZE)
		eq_size = MINIMUM_SUPPORTED_PAGE_SIZE;

	cq_size = roundup_pow_of_two(GDMA_CQE_SIZE * q_depth);
	WARN_ON(cq_size != 64 * 2 * HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH);
	if (cq_size < MINIMUM_SUPPORTED_PAGE_SIZE)
		cq_size = MINIMUM_SUPPORTED_PAGE_SIZE;

	hwc_cq = kzalloc(sizeof(*hwc_cq), GFP_KERNEL);
	if (!hwc_cq)
		return -ENOMEM;

	err = hwc_create_gdma_eq(hwc, eq_size, ctx, callback, &eq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC EQ for RQ: %d\n", err);
		goto out;
	}
	hwc_cq->gdma_eq = eq;

	err = hwc_create_gdma_cq(hwc, cq_size, hwc_cq, hwc_comp_event, eq, &cq);
	if (err) {
		dev_err(hwc->dev, "Failed to create HWC CQ for RQ: %d\n", err);
		goto out;
	}
	hwc_cq->gdma_cq = cq;

	comp_buf = kcalloc(q_depth, sizeof(struct gdma_comp), GFP_KERNEL);
	if (!comp_buf) {
		err = -ENOMEM;
		goto out;
	}

	hwc_cq->hwc = hwc;
	hwc_cq->comp_buf = comp_buf;
	hwc_cq->queue_depth = q_depth;
	hwc_cq->rx_event_handler = rx_ev_hdlr;
	hwc_cq->rx_event_ctx = rx_ev_ctx;
	hwc_cq->tx_event_handler = tx_ev_hdlr;
	hwc_cq->tx_event_ctx = tx_ev_ctx;

	*hwc_cq_p = hwc_cq;
	return 0;

out:
	hwc_destroy_cq(hwc_to_gdma_context(hwc->gdma_dev), hwc_cq);
	return err;
}

static int hwc_alloc_dma_buf(struct hw_channel_context *hwc, u16 q_depth,
			     u32 max_msg_size, struct hwc_dma_buf **dma_buf_p)
{
	struct gdma_context *gc = hwc_to_gdma_context(hwc->gdma_dev);
	struct hwc_work_request *hwc_wr;
	struct hwc_dma_buf *dma_buf;
	struct gdma_mem_info *gmi;
	void *virt_addr;
	u32 buf_size;
	u8 *base_pa;
	int err;
	u16 i;

	dma_buf = kzalloc(sizeof(*dma_buf) +
			  q_depth * sizeof(struct hwc_work_request),
			  GFP_KERNEL);
	if (!dma_buf)
		return -ENOMEM;

	dma_buf->num_reqs = q_depth;

	buf_size = ALIGN(q_depth * max_msg_size, PAGE_SIZE);

	gmi = &dma_buf->mem_info;
	err = gdma_alloc_memory(gc, buf_size, gmi);
	if (err) {
		dev_err(hwc->dev, "Failed to allocate DMA buffer: %d\n", err);
		goto out;
	}

	virt_addr = dma_buf->mem_info.virt_addr;
	base_pa = (u8 *)dma_buf->mem_info.dma_handle;

	for (i = 0; i < q_depth; i++) {
		hwc_wr = &dma_buf->reqs[i];

		hwc_wr->buf_va = virt_addr + i * max_msg_size;
		hwc_wr->buf_sge_addr = base_pa + i * max_msg_size;

		hwc_wr->buf_len = max_msg_size;
	}

	*dma_buf_p = dma_buf;
	return 0;
out:
	kfree(dma_buf);
	return err;
}

static void hwc_dealloc_dma_buf(struct hw_channel_context *hwc,
				struct hwc_dma_buf *dma_buf)
{
	if (!dma_buf)
		return;

	gdma_free_memory(&dma_buf->mem_info);

	kfree(dma_buf);
}

static void hwc_destroy_wq(struct hw_channel_context *hwc,
			   struct hwc_wq *hwc_wq)
{
	if (!hwc_wq)
		return;

	hwc_dealloc_dma_buf(hwc, hwc_wq->msg_buf);

	if (hwc_wq->gdma_wq)
		gdma_destroy_queue(hwc_to_gdma_context(hwc->gdma_dev),
				   hwc_wq->gdma_wq);

	kfree(hwc_wq);
}

static int hwc_create_wq(struct hw_channel_context *hwc,
			 enum gdma_queue_type q_type, u16 q_depth,
			 u32 max_msg_size, struct hwc_cq *hwc_cq,
			 struct hwc_wq **hwc_wq_p)
{
	struct gdma_queue *queue;
	struct hwc_wq *hwc_wq;
	u32 queue_size;
	int err;

	WARN_ON(q_type != GDMA_SQ && q_type != GDMA_RQ);

	if (q_type == GDMA_RQ)
		queue_size = roundup_pow_of_two(GDMA_MAX_RQE_SIZE * q_depth);
	else
		queue_size = roundup_pow_of_two(GDMA_MAX_SQE_SIZE * q_depth);

	if (queue_size < MINIMUM_SUPPORTED_PAGE_SIZE)
		queue_size = MINIMUM_SUPPORTED_PAGE_SIZE;

	hwc_wq = kzalloc(sizeof(*hwc_wq), GFP_KERNEL);
	if (!hwc_wq)
		return -ENOMEM;

	err = hwc_create_gdma_wq(hwc, q_type, queue_size, &queue);
	if (err)
		goto out;

	err = hwc_alloc_dma_buf(hwc, q_depth, max_msg_size, &hwc_wq->msg_buf);
	if (err)
		goto out;

	hwc_wq->hwc = hwc;
	hwc_wq->gdma_wq = queue;
	hwc_wq->queue_depth = q_depth;
	hwc_wq->hwc_cq = hwc_cq;

	*hwc_wq_p = hwc_wq;
	return 0;

out:
	if (err)
		hwc_destroy_wq(hwc, hwc_wq);
	return err;
}

static int hwc_post_tx_wqe(const struct hwc_wq *hwc_txq,
			   struct hwc_work_request *req,
			   u32 dest_virt_rq_id, u32 dest_virt_rcq_id,
			   bool dest_pf)
{
	struct device *dev = hwc_txq->hwc->dev;
	struct hwc_tx_oob *tx_oob;
	struct gdma_sge *sge;
	int err;

	if (req->msg_size == 0 || req->msg_size > req->buf_len) {
		dev_err(dev, "wrong msg_size: %u, buf_len: %u\n",
			req->msg_size, req->buf_len);
		return -EINVAL;
	}

	tx_oob = &req->tx_oob;

	tx_oob->vrq_id = dest_virt_rq_id;
	tx_oob->dest_vfid = 0;
	tx_oob->vrcq_id = dest_virt_rcq_id;
	tx_oob->vscq_id = hwc_txq->hwc_cq->gdma_cq->id;
	tx_oob->loopback = false;
	tx_oob->lso_override = false;
	tx_oob->dest_pf = dest_pf;
	tx_oob->vsq_id = hwc_txq->gdma_wq->id;

	sge = &req->sge;
	sge->address = (u64)req->buf_sge_addr;
	sge->mem_key = hwc_txq->msg_buf->gpa_mkey;
	sge->size = req->msg_size;

	memset(&req->wqe_req, 0, sizeof(struct gdma_wqe_request));
	req->wqe_req.sgl = sge;
	req->wqe_req.num_sge = 1;
	req->wqe_req.inline_oob_size = sizeof(struct hwc_tx_oob);
	req->wqe_req.inline_oob_data = tx_oob;
	req->wqe_req.client_data_unit = 0;

	err = gdma_post_and_ring(hwc_txq->gdma_wq, &req->wqe_req, NULL);
	if (err)
		dev_err(dev, "Failed to post WQE on HWC RQ: %d\n", err);

	return err;
}

static int hwc_init_inflight_msg(struct hw_channel_context *hwc, u16 num_msg)
{
	int err;

	sema_init(&hwc->sema, num_msg);

	WARN_ON(num_msg != HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH);

	err = gdma_alloc_res_map(num_msg, &hwc->inflight_msg_res);
	if (err)
		dev_err(hwc->dev, "Failed to init inflight_msg_res: %d\n", err);

	return err;
}

static int hwc_test_channel(struct hw_channel_context *hwc, u16 q_depth,
			    u32 max_req_msg_size, u32 max_resp_msg_size)
{
	struct gdma_context *gc = hwc_to_gdma_context(hwc->gdma_dev);
	struct hwc_wq *hwc_rxq = hwc->rxq;
	struct hwc_work_request *req;
	struct hwc_caller_ctx *ctx;
	int err;
	int i;

	/* Post all WQEs on the RQ */
	for (i = 0; i < q_depth; i++) {
		req = &hwc_rxq->msg_buf->reqs[i];
		err = hwc_post_rx_wqe(hwc_rxq, req);
		if (err)
			return err;
	}

	ctx = kzalloc(q_depth * sizeof(struct hwc_caller_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	for (i = 0; i < q_depth; ++i)
		init_completion(&ctx[i].comp_event);

	hwc->caller_ctx = ctx;

	err = gdma_test_eq(gc, hwc->cq->gdma_eq);
	return err;
}

void hwc_destroy_channel(struct gdma_context *gc)
{
	struct hw_channel_context *hwc = gc->hwc.driver_data;
	struct hwc_caller_ctx *ctx;

	shm_channel_teardown_hwc(&gc->shm_channel, false);

	ctx = hwc->caller_ctx;
	kfree(ctx);
	hwc->caller_ctx = NULL;

	hwc_destroy_wq(hwc, hwc->txq);
	hwc->txq = NULL;

	hwc_destroy_wq(hwc, hwc->rxq);
	hwc->rxq = NULL;

	hwc_destroy_cq(hwc_to_gdma_context(hwc->gdma_dev), hwc->cq);
	hwc->cq = NULL;

	gdma_free_res_map(&hwc->inflight_msg_res);

	hwc->num_inflight_msg = 0;

	if (hwc->gdma_dev->pdid != INVALID_PDID) {
		hwc->gdma_dev->doorbell = INVALID_DOORBELL;
		hwc->gdma_dev->pdid = INVALID_PDID;
	}

	kfree(hwc);
	gc->hwc.driver_data = NULL;
}

static int hwc_establish_channel(struct gdma_context *gc, u16 *q_depth,
				 u32 *max_req_msg_size, u32 *max_resp_msg_size)
{
	struct hw_channel_context *hwc = gc->hwc.driver_data;
	struct gdma_queue *rq = hwc->rxq->gdma_wq;
	struct gdma_queue *sq = hwc->txq->gdma_wq;
	struct gdma_queue *eq = hwc->cq->gdma_eq;
	struct gdma_queue *cq = hwc->cq->gdma_cq;
	int err;

	init_completion(&hwc->hwc_init_eqe_comp);

	err = shm_channel_setup_hwc(&gc->shm_channel, false,
				    eq->mem_info.dma_handle,
				    cq->mem_info.dma_handle,
				    rq->mem_info.dma_handle,
				    sq->mem_info.dma_handle,
				    eq->eq.msix_index);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&hwc->hwc_init_eqe_comp, 60 * HZ))
		return -ETIMEDOUT;

	*q_depth = hwc->hwc_init_q_depth_max;
	*max_req_msg_size = hwc->hwc_init_max_req_msg_size;
	*max_resp_msg_size = hwc->hwc_init_max_resp_msg_size;

	WARN_ON(*q_depth < HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH);
	WARN_ON(*max_req_msg_size != HW_CHANNEL_MAX_REQUEST_SIZE);
	WARN_ON(*max_resp_msg_size != HW_CHANNEL_MAX_RESPONSE_SIZE);

	WARN_ON(gc->max_num_cq == 0);
	if (WARN_ON(cq->id >= gc->max_num_cq))
		return -EPROTO;

	gc->cq_table = vzalloc(gc->max_num_cq * sizeof(struct gdma_queue *));
	if (!gc->cq_table)
		return -ENOMEM;

	gc->cq_table[cq->id] = cq;

	return 0;
}

static int hwc_init_queues(struct hw_channel_context *hwc, u16 q_depth,
			   u32 max_req_msg_size, u32 max_resp_msg_size)
{
	struct hwc_wq *hwc_rxq = NULL;
	struct hwc_wq *hwc_txq = NULL;
	struct hwc_cq *hwc_cq = NULL;
	int err;

	err = hwc_init_inflight_msg(hwc, q_depth);
	if (err)
		return err;

	/* CQ is shared by SQ and RQ, so CQ's queue depth is the sum of SQ
	 * queue depth and RQ queue depth.
	 */
	err = hwc_create_cq(hwc, q_depth * 2, hwc_init_event_handler, hwc,
			    hwc_rx_event_handler, hwc, hwc_tx_event_handler,
			    hwc, &hwc_cq);
	if (err) {
		WARN(1, "Failed to create HWC CQ: %d\n", err);
		goto out;
	}
	hwc->cq = hwc_cq;

	err = hwc_create_wq(hwc, GDMA_RQ, q_depth, max_req_msg_size,
			    hwc_cq, &hwc_rxq);
	if (err) {
		WARN(1, "Failed to create HWC RQ: %d\n", err);
		goto out;
	}
	hwc->rxq = hwc_rxq;

	err = hwc_create_wq(hwc, GDMA_SQ, q_depth, max_resp_msg_size,
			    hwc_cq, &hwc_txq);
	if (err) {
		WARN(1, "Failed to create HWC SQ: %d\n", err);
		goto out;
	}
	hwc->txq = hwc_txq;

	hwc->num_inflight_msg = q_depth;
	hwc->max_req_msg_size = max_req_msg_size;

	return 0;
out:
	if (hwc_txq)
		hwc_destroy_wq(hwc, hwc_txq);

	if (hwc_rxq)
		hwc_destroy_wq(hwc, hwc_rxq);

	if (hwc_cq)
		hwc_destroy_cq(hwc_to_gdma_context(hwc->gdma_dev),
			       hwc_cq);

	gdma_free_res_map(&hwc->inflight_msg_res);
	return err;
}

int hwc_create_channel(struct gdma_context *gc)
{
	u32 max_req_msg_size, max_resp_msg_size;
	struct gdma_dev *gd = &gc->hwc;
	struct hw_channel_context *hwc;
	u16 q_depth_max;
	int err;

	hwc = kzalloc(sizeof(*hwc), GFP_KERNEL);
	if (!hwc)
		return -ENOMEM;

	gd->driver_data = hwc;
	hwc->gdma_dev = gd;
	hwc->dev = gc->dev;

	/* HWC's instance number is always 0. */
	gd->dev_id.as_uint32 = 0;
	gd->dev_id.type = GDMA_DEVICE_HWC;

	gd->pdid = INVALID_PDID;
	gd->doorbell = INVALID_DOORBELL;

	err = hwc_init_queues(hwc, HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH,
			      HW_CHANNEL_MAX_REQUEST_SIZE,
			      HW_CHANNEL_MAX_RESPONSE_SIZE);
	if (err) {
		dev_err(hwc->dev, "Failed to initialize HWC: %d\n", err);
		goto out;
	}

	err = hwc_establish_channel(gc, &q_depth_max, &max_req_msg_size,
				    &max_resp_msg_size);
	if (err) {
		dev_err(hwc->dev, "Failed to establish HWC: %d\n", err);
		goto out;
	}

	WARN_ON(q_depth_max < HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH);
	WARN_ON(max_req_msg_size < HW_CHANNEL_MAX_REQUEST_SIZE);
	WARN_ON(max_resp_msg_size > HW_CHANNEL_MAX_RESPONSE_SIZE);

	err = hwc_test_channel(gc->hwc.driver_data,
			       HW_CHANNEL_VF_BOOTSTRAP_QUEUE_DEPTH,
			       max_req_msg_size, max_resp_msg_size);
	if (err) {
		dev_err(hwc->dev, "Failed to establish HWC: %d\n", err);
		goto out;
	}

	return 0;
out:
	kfree(hwc);
	return err;
}

int hwc_send_request(struct hw_channel_context *hwc, u32 req_len,
		     const void *req, u32 resp_len, void *resp)
{
	struct hwc_work_request *tx_wr;
	struct hwc_wq *txq = hwc->txq;
	struct gdma_req_hdr *req_msg;
	struct hwc_caller_ctx *ctx;
	u16 msg_idx;
	int err;

	hwc_get_msg_index(hwc, &msg_idx);

	tx_wr = &txq->msg_buf->reqs[msg_idx];

	if (req_len > tx_wr->buf_len) {
		dev_err(hwc->dev, "HWC: req msg size: %d > %d\n", req_len,
			tx_wr->buf_len);
		return -EINVAL;
	}

	ctx = hwc->caller_ctx + msg_idx;
	ctx->output_buf = resp;
	ctx->output_buflen = resp_len;

	req_msg = (struct gdma_req_hdr *)tx_wr->buf_va;
	if (req)
		memcpy(req_msg, req, req_len);

	req_msg->req.hwc_msg_id = msg_idx;

	tx_wr->msg_size = req_len;

	err = hwc_post_tx_wqe(txq, tx_wr, 0, 0, false);
	if (err) {
		dev_err(hwc->dev, "HWC: Failed to post send WQE: %d\n", err);
		return err;
	}

	if (!wait_for_completion_timeout(&ctx->comp_event, 30 * HZ)) {
		dev_err(hwc->dev, "HWC: Request timed out!\n");
		return -ETIMEDOUT;
	}

	if (ctx->error)
		return ctx->error;

	if (ctx->status_code) {
		dev_err(hwc->dev, "HWC: Failed hw_channel req: 0x%x\n",
			ctx->status_code);
		return -EPROTO;
	}

	return 0;
}
