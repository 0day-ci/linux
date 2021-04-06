// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>

#include "mana.h"

static u32 gdma_r32(struct gdma_context *g, u64 offset)
{
	return readl(g->bar0_va + offset);
}

static u64 gdma_r64(struct gdma_context *g, u64 offset)
{
	return readq(g->bar0_va + offset);
}

static void gdma_init_registers(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	gc->db_page_size = gdma_r32(gc, GDMA_REG_DB_PAGE_SIZE) & 0xFFFF;

	gc->db_page_base = gc->bar0_va + gdma_r64(gc, GDMA_REG_DB_PAGE_OFFSET);

	gc->shm_base = gc->bar0_va + gdma_r64(gc, GDMA_REG_SHM_OFFSET);
}

static int gdma_query_max_resources(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_general_req req = { 0 };
	struct gdma_query_max_resources_resp resp = { 0 };
	int err;

	gdma_init_req_hdr(&req.hdr, GDMA_QUERY_MAX_RESOURCES,
			  sizeof(req), sizeof(resp));

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("%s, line %d: err=%d, err=0x%x\n", __func__, __LINE__,
		       err, resp.hdr.status);
		return -EPROTO;
	}

	if (gc->num_msix_usable > resp.max_msix)
		gc->num_msix_usable = resp.max_msix;

	if (gc->num_msix_usable <= 1)
		return -ENOSPC;

	/* HWC consumes 1 MSI-X interrupt. */
	gc->max_num_queue = gc->num_msix_usable - 1;

	if (gc->max_num_queue > resp.max_eq)
		gc->max_num_queue = resp.max_eq;

	if (gc->max_num_queue > resp.max_cq)
		gc->max_num_queue = resp.max_cq;

	if (gc->max_num_queue > resp.max_sq)
		gc->max_num_queue = resp.max_sq;

	if (gc->max_num_queue > resp.max_rq)
		gc->max_num_queue = resp.max_rq;

	return 0;
}

static int gdma_detect_devices(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_general_req req = { 0 };
	struct gdma_list_devices_resp resp = { 0 };
	u32 i, max_num_devs;
	struct gdma_dev_id dev;
	u16 dev_type;
	int err;

	gdma_init_req_hdr(&req.hdr, GDMA_LIST_DEVICES, sizeof(req),
			  sizeof(resp));

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("gdma: failed to detect devices: err=%d, 0x%x\n", err,
		       resp.hdr.status);
		return -EPROTO;
	}

	max_num_devs = min_t(u32, MAX_NUM_GDMA_DEVICES, resp.num_of_clients);

	for (i = 0; i < max_num_devs; i++) {
		dev = resp.clients[i];
		dev_type = dev.type;

		/* HWC is already detected in hwc_create_channel(). */
		if (dev_type == GDMA_DEVICE_HWC)
			continue;

		if (dev_type == GDMA_DEVICE_ANA)
			gc->ana.dev_id = dev;
	}

	return gc->ana.dev_id.type == 0 ? -ENODEV : 0;
}

int gdma_send_request(struct gdma_context *gc, u32 req_len, const void *req,
		      u32 resp_len, void *resp)
{
	struct hw_channel_context *hwc = gc->hwc.driver_data;

	return hwc_send_request(hwc, req_len, req, resp_len, resp);
}

int gdma_alloc_memory(struct gdma_context *gc, unsigned int length,
		      struct gdma_mem_info *gmi)
{
	dma_addr_t dma_handle;
	void *buf;

	if (length < PAGE_SIZE || !is_power_of_2(length))
		return -EINVAL;

	gmi->dev = &gc->pci_dev->dev;
	buf = dma_alloc_coherent(gmi->dev, length, &dma_handle,
				 GFP_KERNEL | __GFP_ZERO);
	if (!buf)
		return -ENOMEM;

	gmi->dma_handle = dma_handle;
	gmi->virt_addr = buf;
	gmi->length = length;

	return 0;
}

void gdma_free_memory(struct gdma_mem_info *gmi)
{
	dma_free_coherent(gmi->dev, gmi->length, gmi->virt_addr,
			  gmi->dma_handle);
}

static int gdma_create_hw_eq(struct gdma_context *gc, struct gdma_queue *queue)
{
	struct gdma_create_queue_req req = { 0 };
	struct gdma_create_queue_resp resp = { 0 };
	int err;

	if (queue->type != GDMA_EQ)
		return -EINVAL;

	gdma_init_req_hdr(&req.hdr, GDMA_CREATE_QUEUE,
			  sizeof(req), sizeof(resp));

	req.hdr.dev_id = queue->gdma_dev->dev_id;
	req.type = queue->type;
	req.pdid = queue->gdma_dev->pdid;
	req.doolbell_id = queue->gdma_dev->doorbell;
	req.dma_region = queue->mem_info.dma_region;
	req.queue_size = queue->queue_size;
	req.log2_throttle_limit = queue->eq.log2_throttle_limit;
	req.eq_pci_msix_index = queue->eq.msix_index;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("Failed to create queue: %d, 0x%x\n", err,
		       resp.hdr.status);
		return err ? err : -EPROTO;
	}

	queue->id = resp.queue_index;
	queue->eq.disable_needed = true;
	queue->mem_info.dma_region = GDMA_INVALID_DMA_REGION;
	return 0;
}

static int gdma_disable_queue(struct gdma_queue *queue)
{
	struct gdma_context *gc = gdma_dev_to_context(queue->gdma_dev);
	struct gdma_disable_queue_req req = { 0 };
	struct gdma_general_resp resp = { 0 };
	int err;

	WARN_ON(queue->type != GDMA_EQ);

	gdma_init_req_hdr(&req.hdr, GDMA_DISABLE_QUEUE,
			  sizeof(req), sizeof(resp));

	req.hdr.dev_id = queue->gdma_dev->dev_id;
	req.type = queue->type;
	req.queue_index =  queue->id;
	req.alloc_res_id_on_creation = 1;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("Failed to disable queue: %d, 0x%x\n", err,
		       resp.hdr.status);
		return -EPROTO;
	}

	return 0;
}

#define DOORBELL_OFFSET_SQ	0x0
#define DOORBELL_OFFSET_RQ	0x400
#define DOORBELL_OFFSET_CQ	0x800
#define DOORBELL_OFFSET_EQ	0xFF8

static void gdma_ring_doorbell(struct gdma_context *gc, u32 db_index,
			       enum gdma_queue_type q_type, u32 qid,
			       u32 tail_ptr, u8 num_req)
{
	void __iomem *addr = gc->db_page_base + gc->db_page_size * db_index;
	union gdma_doorbell_entry e = { 0 };

	switch (q_type) {
	case GDMA_EQ:
		e.eq.id = qid;
		e.eq.tail_ptr = tail_ptr;
		e.eq.arm = num_req;

		addr += DOORBELL_OFFSET_EQ;
		break;

	case GDMA_CQ:
		e.cq.id = qid;
		e.cq.tail_ptr = tail_ptr;
		e.cq.arm = num_req;

		addr += DOORBELL_OFFSET_CQ;
		break;

	case GDMA_RQ:
		e.rq.id = qid;
		e.rq.tail_ptr = tail_ptr;
		e.rq.wqe_cnt = num_req;

		addr += DOORBELL_OFFSET_RQ;
		break;

	case GDMA_SQ:
		e.sq.id = qid;
		e.sq.tail_ptr = tail_ptr;

		addr += DOORBELL_OFFSET_SQ;
		break;

	default:
		WARN_ON(1);
		return;
	}

	/* Ensure all writes are done before ring doorbell */
	wmb();

	writeq(e.as_uint64, addr);
}

void gdma_wq_ring_doorbell(struct gdma_context *gc, struct gdma_queue *queue)
{
	gdma_ring_doorbell(gc, queue->gdma_dev->doorbell, queue->type,
			   queue->id, queue->head * GDMA_WQE_BU_SIZE, 1);
}

void gdma_arm_cq(struct gdma_queue *cq)
{
	struct gdma_context *gc = gdma_dev_to_context(cq->gdma_dev);

	u32 num_cqe = cq->queue_size / GDMA_CQE_SIZE;

	u32 head = cq->head % (num_cqe << GDMA_CQE_OWNER_BITS);

	gdma_ring_doorbell(gc, cq->gdma_dev->doorbell, cq->type, cq->id, head,
			   SET_ARM_BIT);
}

static void gdma_process_eqe(struct gdma_queue *eq)
{
	struct gdma_context *gc = gdma_dev_to_context(eq->gdma_dev);
	u32 head = eq->head % (eq->queue_size / GDMA_EQE_SIZE);
	struct gdma_eqe *eq_eqe_ptr = eq->queue_mem_ptr;
	union gdma_eqe_info eqe_info;
	enum gdma_eqe_type type;
	struct gdma_event event;
	struct gdma_queue *cq;
	struct gdma_eqe *eqe;
	u32 cq_id;

	eqe = &eq_eqe_ptr[head];
	eqe_info.as_uint32 = eqe->eqe_info;
	type = eqe_info.type;

	if ((type >= GDMA_EQE_APP_START && type <= GDMA_EQE_APP_END) ||
	    type == GDMA_EQE_SOC_TO_VF_EVENT ||
	    type == GDMA_EQE_HWC_INIT_EQ_ID_DB ||
	    type == GDMA_EQE_HWC_INIT_DATA || type == GDMA_EQE_HWC_INIT_DONE) {
		if (eq->eq.callback) {
			event.type = type;
			memcpy(&event.details, &eqe->details,
			       GDMA_EVENT_DATA_SIZE);

			eq->eq.callback(eq->eq.context, eq, &event);
		}

		return;
	}

	switch (type) {
	case GDMA_EQE_COMPLETION:
		cq_id = eqe->details[0] & 0xFFFFFF;
		if (WARN_ON(cq_id >= gc->max_num_cq))
			break;

		cq = gc->cq_table[cq_id];
		if (WARN_ON(!cq || cq->type != GDMA_CQ || cq->id != cq_id))
			break;

		if (cq->cq.callback)
			cq->cq.callback(cq->cq.context, cq);

		break;

	case GDMA_EQE_TEST_EVENT:
		gc->test_event_eq_id = eq->id;
		complete(&gc->eq_test_event);
		break;

	default:
		break;
	}
}

static void gdma_process_eq_events(void *arg)
{
	struct gdma_queue *eq = arg;
	struct gdma_context *gc;
	struct gdma_eqe *eqe;
	struct gdma_eqe *eq_eqe_ptr = eq->queue_mem_ptr;
	u32 owner_bits, new_bits, old_bits;
	u32 head;
	u32 num_eqe;
	union gdma_eqe_info eqe_info;
	int i;
	int arm_bit;

	num_eqe = eq->queue_size / GDMA_EQE_SIZE;

	/* Process up to 5 EQEs at a time, and update the HW head. */
	for (i = 0; i < 5; i++) {
		eqe = &eq_eqe_ptr[eq->head % num_eqe];
		eqe_info.as_uint32 = eqe->eqe_info;

		new_bits = (eq->head / num_eqe) & GDMA_EQE_OWNER_MASK;
		old_bits = (eq->head / num_eqe - 1) & GDMA_EQE_OWNER_MASK;

		owner_bits = eqe_info.owner_bits;

		if (owner_bits == old_bits)
			break;

		if (owner_bits != new_bits) {
			pr_err("EQ %d: overflow detected\n", eq->id);
			break;
		}

		gdma_process_eqe(eq);

		eq->head++;
	}

	/* Always rearm the EQ for HWC. For ANA, rearm it when NAPI is done. */
	if (gdma_is_hwc(eq->gdma_dev)) {
		arm_bit = SET_ARM_BIT;
	} else if (eq->eq.work_done < eq->eq.budget &&
		   napi_complete_done(&eq->eq.napi, eq->eq.work_done)) {
		arm_bit = SET_ARM_BIT;
	} else {
		arm_bit = 0;
	}

	head = eq->head % (num_eqe << GDMA_EQE_OWNER_BITS);

	gc = gdma_dev_to_context(eq->gdma_dev);

	gdma_ring_doorbell(gc, eq->gdma_dev->doorbell, eq->type, eq->id, head,
			   arm_bit);
}

static int ana_poll(struct napi_struct *napi, int budget)
{
	struct gdma_queue *eq = container_of(napi, struct gdma_queue, eq.napi);

	eq->eq.work_done = 0;
	eq->eq.budget = budget;

	gdma_process_eq_events(eq);

	return min(eq->eq.work_done, budget);
}

static void gdma_schedule_napi(void *arg)
{
	struct gdma_queue *eq = arg;
	struct napi_struct *napi = &eq->eq.napi;

	napi_schedule_irqoff(napi);
}

static int gdma_register_irq(struct gdma_queue *queue)
{
	struct gdma_dev *gd = queue->gdma_dev;
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_resource *r = &gc->msix_resource;
	bool is_ana = gdma_is_ana(gd);
	unsigned int msi_index;
	unsigned long flags;
	struct gdma_irq_context *gic;
	int err;

	spin_lock_irqsave(&r->lock, flags);

	msi_index = find_first_zero_bit(r->map, r->size);
	if (msi_index >= r->size) {
		err = -ENOSPC;
	} else {
		bitmap_set(r->map, msi_index, 1);
		queue->eq.msix_index = msi_index;
		err = 0;
	}

	spin_unlock_irqrestore(&r->lock, flags);

	if (err)
		return err;

	WARN_ON(msi_index >= gc->num_msix_usable);

	gic = &gc->irq_contexts[msi_index];

	if (is_ana) {
		netif_napi_add(gd->driver_data, &queue->eq.napi, ana_poll,
			       NAPI_POLL_WEIGHT);

		napi_enable(&queue->eq.napi);
	}

	WARN_ON(gic->handler || gic->arg);

	gic->arg = queue;
	gic->handler = is_ana ? gdma_schedule_napi : gdma_process_eq_events;

	return 0;
}

static void gdma_deregiser_irq(struct gdma_queue *queue)
{
	struct gdma_dev *gd = queue->gdma_dev;
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_resource *r = &gc->msix_resource;
	unsigned int msix_index = queue->eq.msix_index;
	struct gdma_irq_context *gic;
	unsigned long flags;

	if (WARN_ON(msix_index == INVALID_PCI_MSIX_INDEX ||
		    msix_index > num_online_cpus()))
		return;

	gic = &gc->irq_contexts[msix_index];

	WARN_ON(!gic->handler || !gic->arg);
	gic->handler = NULL;
	gic->arg = NULL;

	spin_lock_irqsave(&r->lock, flags);
	bitmap_clear(r->map, msix_index, 1);
	spin_unlock_irqrestore(&r->lock, flags);

	queue->eq.msix_index = INVALID_PCI_MSIX_INDEX;
}

int gdma_test_eq(struct gdma_context *gc, struct gdma_queue *eq)
{
	struct gdma_generate_test_event_req req = { 0 };
	struct gdma_general_resp resp = { 0 };
	int err;

	mutex_lock(&gc->eq_test_event_mutex);

	init_completion(&gc->eq_test_event);
	gc->test_event_eq_id = INVALID_QUEUE_ID;

	gdma_init_req_hdr(&req.hdr, GDMA_GENERATE_TEST_EQE,
			  sizeof(req), sizeof(resp));

	req.hdr.dev_id = eq->gdma_dev->dev_id;
	req.queue_index = eq->id;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		pr_err("test_eq failed: %d\n", err);
		goto out;
	}

	err = -EPROTO;

	if (resp.hdr.status) {
		pr_err("test_eq failed: 0x%x\n", resp.hdr.status);
		goto out;
	}

	if (!wait_for_completion_timeout(&gc->eq_test_event, 30 * HZ)) {
		pr_err("test_eq timed out on queue %d\n", eq->id);
		goto out;
	}

	if (eq->id != gc->test_event_eq_id) {
		pr_err("test_eq got an event on wrong queue %d (%d)\n",
		       gc->test_event_eq_id, eq->id);
		goto out;
	}

	err = 0;
out:
	mutex_unlock(&gc->eq_test_event_mutex);
	return err;
}

static void gdma_destroy_eq(struct gdma_context *gc, bool flush_evenets,
			    struct gdma_queue *queue)
{
	int err;

	if (flush_evenets) {
		err = gdma_test_eq(gc, queue);
		if (err)
			pr_warn("Failed to flush EQ events: %d\n", err);
	}

	gdma_deregiser_irq(queue);

	if (gdma_is_ana(queue->gdma_dev)) {
		napi_disable(&queue->eq.napi);
		netif_napi_del(&queue->eq.napi);
	}

	if (queue->eq.disable_needed)
		gdma_disable_queue(queue);
}

static int gdma_create_eq(struct gdma_dev *gd,
			  const struct gdma_queue_spec *spec, bool create_hwq,
			  struct gdma_queue *queue)
{
	struct gdma_context *gc = gdma_dev_to_context(gd);
	u32 log2_num_entries;
	int err;

	queue->eq.msix_index = INVALID_PCI_MSIX_INDEX;

	log2_num_entries = ilog2(queue->queue_size / GDMA_EQE_SIZE);

	if (spec->eq.log2_throttle_limit > log2_num_entries) {
		pr_err("EQ throttling limit (%lu) > maximum EQE (%u)\n",
		       spec->eq.log2_throttle_limit, log2_num_entries);
		return -EINVAL;
	}

	err = gdma_register_irq(queue);
	if (err) {
		pr_err("Failed to register irq: %d\n", err);
		return err;
	}

	queue->eq.callback = spec->eq.callback;
	queue->eq.context = spec->eq.context;
	queue->head |= INITIALIZED_OWNER_BIT(log2_num_entries);

	queue->eq.log2_throttle_limit = spec->eq.log2_throttle_limit ?: 1;

	if (create_hwq) {
		err = gdma_create_hw_eq(gc, queue);
		if (err)
			goto out;

		err = gdma_test_eq(gc, queue);
		if (err)
			goto out;
	}

	return 0;
out:
	pr_err("Failed to create EQ: %d\n", err);
	gdma_destroy_eq(gc, false, queue);
	return err;
}

static void gdma_create_cq(const struct gdma_queue_spec *spec,
			   struct gdma_queue *queue)
{
	u32 log2_num_entries = ilog2(spec->queue_size / GDMA_CQE_SIZE);

	queue->head = queue->head | INITIALIZED_OWNER_BIT(log2_num_entries);
	queue->cq.parent = spec->cq.parent_eq;
	queue->cq.context = spec->cq.context;
	queue->cq.callback = spec->cq.callback;
}

static void gdma_destroy_cq(struct gdma_context *gc, struct gdma_queue *queue)
{
	u32 id = queue->id;

	if (id >= gc->max_num_cq)
		return;

	if (!gc->cq_table[id])
		return;

	gc->cq_table[id] = NULL;
}

int gdma_create_hwc_queue(struct gdma_dev *gd,
			  const struct gdma_queue_spec *spec,
			  struct gdma_queue **queue_ptr)
{
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_mem_info *gmi;
	struct gdma_queue *queue;
	int err;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = gdma_alloc_memory(gc, spec->queue_size, gmi);
	if (err)
		return err;

	queue->head = 0;
	queue->tail = 0;
	queue->queue_mem_ptr = gmi->virt_addr;
	queue->queue_size = spec->queue_size;
	queue->monitor_avl_buf = spec->monitor_avl_buf;

	queue->type = spec->type;
	queue->gdma_dev = gd;

	if (spec->type == GDMA_EQ)
		err = gdma_create_eq(gd, spec, false, queue);
	else if (spec->type == GDMA_CQ)
		gdma_create_cq(spec, queue);

	if (err)
		goto out;

	*queue_ptr = queue;
	return 0;

out:
	gdma_free_memory(gmi);
	kfree(queue);
	return err;
}

static void gdma_destroy_dma_region(struct gdma_context *gc, u64 dma_region)
{
	struct gdma_destroy_dma_region_req req = { 0 };
	struct gdma_general_resp resp = { 0 };
	int err;

	if (dma_region == GDMA_INVALID_DMA_REGION)
		return;

	gdma_init_req_hdr(&req.hdr, GDMA_DESTROY_DMA_REGION, sizeof(req),
			  sizeof(resp));
	req.dma_region = dma_region;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status)
		pr_err("Failed to destroy DMA region: %d, 0x%x\n",
		       err, resp.hdr.status);
}

static int gdma_create_dma_region(struct gdma_dev *gd,
				  struct gdma_mem_info *gmi)
{
	struct gdma_context *gc = ana_to_gdma_context(gd);
	struct hw_channel_context *hwc = gc->hwc.driver_data;

	struct gdma_create_dma_region_req *req = NULL;
	struct gdma_create_dma_region_resp resp = { 0 };

	unsigned int num_page = gmi->length / PAGE_SIZE;
	u32 length = gmi->length;
	u32 req_msg_size;
	int err;
	int i;

	if (length < PAGE_SIZE || !is_power_of_2(length))
		return -EINVAL;

	if (offset_in_page(gmi->virt_addr) != 0)
		return -EINVAL;

	req_msg_size = sizeof(*req) + num_page * sizeof(u64);
	if (req_msg_size > hwc->max_req_msg_size)
		return -EINVAL;

	req = kzalloc(req_msg_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	gdma_init_req_hdr(&req->hdr, GDMA_CREATE_DMA_REGION,
			  req_msg_size, sizeof(resp));
	req->length = length;
	req->offset_in_page = 0;
	req->gdma_page_type = GDMA_PAGE_TYPE_4K;
	req->page_count = num_page;
	req->page_addr_list_len = num_page;

	for (i = 0; i < num_page; i++)
		req->page_addr_list[i] = gmi->dma_handle +  i * PAGE_SIZE;

	err = gdma_send_request(gc, req_msg_size, req, sizeof(resp), &resp);
	if (err)
		goto out;

	if (resp.hdr.status || resp.dma_region == GDMA_INVALID_DMA_REGION) {
		pr_err("Failed to create DMA region: 0x%x\n", resp.hdr.status);
		err = -EPROTO;
		goto out;
	}

	gmi->dma_region = resp.dma_region;

out:
	kfree(req);
	return err;
}

int gdma_create_ana_eq(struct gdma_dev *gd, const struct gdma_queue_spec *spec,
		       struct gdma_queue **queue_ptr)
{
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_mem_info *gmi;
	struct gdma_queue *queue;
	int err;

	if (spec->type != GDMA_EQ)
		return -EINVAL;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = gdma_alloc_memory(gc, spec->queue_size, gmi);
	if (err)
		return err;

	err = gdma_create_dma_region(gd, gmi);
	if (err)
		goto out;

	queue->head = 0;
	queue->tail = 0;
	queue->queue_mem_ptr = gmi->virt_addr;
	queue->queue_size = spec->queue_size;
	queue->monitor_avl_buf = spec->monitor_avl_buf;

	queue->type = spec->type;
	queue->gdma_dev = gd;

	err = gdma_create_eq(gd, spec, true, queue);
	if (err)
		goto out;

	*queue_ptr = queue;
	return 0;
out:
	gdma_free_memory(gmi);
	kfree(queue);
	return err;
}

int gdma_create_ana_wq_cq(struct gdma_dev *gd,
			  const struct gdma_queue_spec *spec,
			  struct gdma_queue **queue_ptr)
{
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_queue *queue;
	struct gdma_mem_info *gmi;
	int err;

	if (spec->type != GDMA_CQ && spec->type != GDMA_SQ &&
	    spec->type != GDMA_RQ)
		return -EINVAL;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return -ENOMEM;

	gmi = &queue->mem_info;
	err = gdma_alloc_memory(gc, spec->queue_size, gmi);
	if (err)
		return err;

	err = gdma_create_dma_region(gd, gmi);
	if (err)
		goto out;

	queue->head = 0;
	queue->tail = 0;
	queue->queue_mem_ptr = gmi->virt_addr;
	queue->queue_size = spec->queue_size;
	queue->monitor_avl_buf = spec->monitor_avl_buf;

	queue->type = spec->type;
	queue->gdma_dev = gd;

	if (spec->type == GDMA_CQ)
		gdma_create_cq(spec, queue);

	*queue_ptr = queue;
	return 0;

out:
	gdma_free_memory(gmi);
	kfree(queue);
	return err;
}

void gdma_destroy_queue(struct gdma_context *gc, struct gdma_queue *queue)
{
	struct gdma_mem_info *gmi = &queue->mem_info;

	switch (queue->type) {
	case GDMA_EQ:
		gdma_destroy_eq(gc, queue->eq.disable_needed, queue);
		break;

	case GDMA_CQ:
		gdma_destroy_cq(gc, queue);
		break;

	case GDMA_RQ:
		break;

	case GDMA_SQ:
		break;

	default:
		pr_err("Can't destroy unknown queue: type=%d\n", queue->type);
		return;
	}

	gdma_destroy_dma_region(gc, gmi->dma_region);

	gdma_free_memory(gmi);

	kfree(queue);
}

int gdma_verify_vf_version(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_verify_ver_req req = { 0 };
	struct gdma_verify_ver_resp resp = { 0 };
	int err;

	gdma_init_req_hdr(&req.hdr, GDMA_VERIFY_VF_DRIVER_VERSION,
			  sizeof(req), sizeof(resp));

	req.protocol_ver_min = GDMA_PROTOCOL_FIRST;
	req.protocol_ver_max = GDMA_PROTOCOL_LAST;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("VfVerifyVersionOutput: %d, status=0x%x\n", err,
		       resp.hdr.status);
		return -EPROTO;
	}

	return 0;
}

int gdma_register_device(struct gdma_dev *gd)
{
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_general_req req = { 0 };
	struct gdma_register_device_resp resp = { 0 };
	int err;

	gdma_init_req_hdr(&req.hdr, GDMA_REGISTER_DEVICE, sizeof(req),
			  sizeof(resp));

	req.hdr.dev_id = gd->dev_id;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("gdma_register_device_resp failed: %d, 0x%x\n", err,
		       resp.hdr.status);
		return -EPROTO;
	}

	gd->pdid = resp.pdid;
	gd->gpa_mkey = resp.gpa_mkey;
	gd->doorbell = resp.db_id;

	return 0;
}

int gdma_deregister_device(struct gdma_dev *gd)
{
	struct gdma_context *gc = gdma_dev_to_context(gd);
	struct gdma_general_req req = { 0 };
	struct gdma_general_resp resp = { 0 };
	int err;

	if (WARN_ON(gd->pdid == INVALID_PDID))
		return -EINVAL;

	gdma_init_req_hdr(&req.hdr, GDMA_DEREGISTER_DEVICE, sizeof(req),
			  sizeof(resp));

	req.hdr.dev_id = gd->dev_id;

	err = gdma_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		pr_err("Failed to deregister device: %d, 0x%x\n", err,
		       resp.hdr.status);
		return -EPROTO;
	}

	gd->pdid = INVALID_PDID;
	gd->doorbell = INVALID_DOORBELL;
	gd->gpa_mkey = INVALID_MEM_KEY;

	return 0;
}

static u32 gdma_calc_sgl_size(const struct gdma_wqe_request *wqe_req)
{
	u32 sgl_data_size = 0;
	u32 i;

	if (wqe_req->flags & GDMA_WR_SGL_DIRECT) {
		for (i = 0; i < wqe_req->num_sge; i++)
			sgl_data_size += wqe_req->sgl[i].size;
	} else {
		sgl_data_size += sizeof(struct gdma_sge) *
				 max_t(u32, 1, wqe_req->num_sge);
	}

	return sgl_data_size;
}

u32 gdma_wq_avail_space(struct gdma_queue *wq)
{
	u32 wq_size = wq->queue_size;
	u32 used_space = (wq->head - wq->tail) * GDMA_WQE_BU_SIZE;

	WARN_ON(used_space > wq_size);

	return wq_size - used_space;
}

u8 *gdma_get_wqe_ptr(const struct gdma_queue *wq, u32 wqe_offset)
{
	u32 offset = (wqe_offset * GDMA_WQE_BU_SIZE) & (wq->queue_size - 1);

	WARN_ON((offset + GDMA_WQE_BU_SIZE) > wq->queue_size);

	return wq->queue_mem_ptr + offset;
}

static u32 gdma_write_client_oob(u8 *wqe_ptr,
				 const struct gdma_wqe_request *wqe_req,
				 enum gdma_queue_type q_type,
				 u32 client_oob_size, u32 sgl_data_size)
{
	bool pad_data = !!(wqe_req->flags & GDMA_WR_PAD_DATA_BY_FIRST_SGE);
	bool sgl_direct = !!(wqe_req->flags & GDMA_WR_SGL_DIRECT);
	bool oob_in_sgl = !!(wqe_req->flags & GDMA_WR_OOB_IN_SGL);
	u8 *p = wqe_ptr;
	struct gdma_wqe *header = (struct gdma_wqe *)p;

	memset(header, 0, sizeof(struct gdma_wqe));

	WARN_ON(client_oob_size != INLINE_OOB_SMALL_SIZE &&
		client_oob_size != INLINE_OOB_LARGE_SIZE);

	if (sgl_direct) {
		header->num_sge = sgl_data_size / sizeof(struct gdma_sge);
		header->last_vbytes = sgl_data_size % sizeof(struct gdma_sge);

		if (header->last_vbytes)
			header->num_sge++;
	} else {
		header->num_sge = wqe_req->num_sge;
	}

	/* Support for empty SGL: account for the dummy SGE to be written. */
	if (wqe_req->num_sge == 0)
		header->num_sge = 1;

	header->inline_oob_size_div4 = client_oob_size / sizeof(u32);

	if (oob_in_sgl) {
		WARN_ON(!pad_data || wqe_req->num_sge <= 0);

		header->client_oob_in_sgl = 1;

		if (wqe_req->num_sge == 1) {
			/* Support for empty SGL with oob_in_sgl */
			header->num_sge = 2;
		}

		if (pad_data)
			header->last_vbytes = wqe_req->sgl[0].size;
	}

	if (q_type == GDMA_SQ)
		header->client_data_unit = wqe_req->client_data_unit;

	header->consume_credit = !!(wqe_req->flags & GDMA_WR_CONSUME_CREDIT);
	header->fence = !!(wqe_req->flags & GDMA_WR_FENCE);
	header->check_sn = !!(wqe_req->flags & GDMA_WR_CHECK_SN);
	header->sgl_direct = sgl_direct;

	/* The size of gdma_wqe + client_oob_size must be less than or equal
	 * to the basic unit, so the pointer here won't be beyond the queue
	 * buffer boundary.
	 */
	p += sizeof(header);

	if (wqe_req->inline_oob_data && wqe_req->inline_oob_size > 0) {
		memcpy(p, wqe_req->inline_oob_data, wqe_req->inline_oob_size);

		if (client_oob_size > wqe_req->inline_oob_size)
			memset(p + wqe_req->inline_oob_size, 0,
			       client_oob_size - wqe_req->inline_oob_size);
	}

	return sizeof(header) + client_oob_size;
}

static u32 gdma_write_sgl(struct gdma_queue *wq, u8 *wqe_ptr,
			  const struct gdma_wqe_request *wqe_req)
{
	u8 *wq_base_ptr = wq->queue_mem_ptr;
	u8 *wq_end_ptr = wq_base_ptr + wq->queue_size;
	const struct gdma_sge *sgl = wqe_req->sgl;
	bool sgl_direct = !!(wqe_req->flags & GDMA_WR_SGL_DIRECT);
	bool oob_in_sgl = !!(wqe_req->flags & GDMA_WR_OOB_IN_SGL);
	u32 num_sge = wqe_req->num_sge;
	u32 size_to_queue_end = (u32)(wq_end_ptr - wqe_ptr);
	u32 queue_size = wq->queue_size;
	struct gdma_sge dummy_sgl[2];
	const u8 *address;
	u32 sgl_size;
	u32 size;
	u32 i;

	if (num_sge == 0 || (oob_in_sgl && num_sge == 1)) {
		/* Per spec, the case of an empty SGL should be handled as
		 * follows to avoid corrupted WQE errors:
		 * Write one dummy SGL entry;
		 * Set the address to 1, leave the rest as 0.
		 */
		dummy_sgl[num_sge].address = 1;
		dummy_sgl[num_sge].size = 0;
		dummy_sgl[num_sge].mem_key = 0;
		if (num_sge == 1)
			memcpy(dummy_sgl, wqe_req->sgl,
			       sizeof(struct gdma_sge));

		num_sge++;
		sgl = dummy_sgl;
		sgl_direct = false;
	}

	sgl_size = 0;

	if (sgl_direct) {
		for (i = 0; i < num_sge; i++) {
			address = (u8 *)wqe_req->sgl[i].address;
			size = wqe_req->sgl[i].size;

			if (size_to_queue_end < size) {
				memcpy(wqe_ptr, address, size_to_queue_end);
				wqe_ptr = wq_base_ptr;
				address += size_to_queue_end;
				size -= size_to_queue_end;
			}

			memcpy(wqe_ptr, address, size);

			wqe_ptr += size;

			if (wqe_ptr >= wq_end_ptr)
				wqe_ptr -= queue_size;

			size_to_queue_end = (u32)(wq_end_ptr - wqe_ptr);

			sgl_size += size;
		}
	} else {
		address = (u8 *)sgl;

		size = sizeof(struct gdma_sge) * num_sge;

		if (size_to_queue_end < size) {
			memcpy(wqe_ptr, address, size_to_queue_end);

			wqe_ptr = wq_base_ptr;
			address += size_to_queue_end;
			size -= size_to_queue_end;
		}

		memcpy(wqe_ptr, address, size);

		sgl_size = size;
	}

	return sgl_size;
}

int gdma_post_work_request(struct gdma_queue *wq,
			   const struct gdma_wqe_request *wqe_req,
			   struct gdma_posted_wqe_info *wqe_info)
{
	bool sgl_direct = !!(wqe_req->flags & GDMA_WR_SGL_DIRECT);
	bool oob_in_sgl = !!(wqe_req->flags & GDMA_WR_OOB_IN_SGL);
	u32 client_oob_size;
	u32 sgl_data_size;
	u32 max_wqe_size;
	u32 wqe_size;
	u8 *wqe_ptr;

	if (sgl_direct && (wq->type != GDMA_SQ || oob_in_sgl))
		return -EINVAL;

	if (wqe_req->inline_oob_size > INLINE_OOB_LARGE_SIZE)
		return -EINVAL;

	if (oob_in_sgl && wqe_req->num_sge == 0)
		return -EINVAL;

	client_oob_size = gdma_align_inline_oobsize(wqe_req->inline_oob_size);

	sgl_data_size = gdma_calc_sgl_size(wqe_req);

	wqe_size = ALIGN(sizeof(struct gdma_wqe) + client_oob_size +
			 sgl_data_size, GDMA_WQE_BU_SIZE);

	if (wq->type == GDMA_RQ)
		max_wqe_size = GDMA_MAX_RQE_SIZE;
	else
		max_wqe_size = GDMA_MAX_SQE_SIZE;

	if (wqe_size > max_wqe_size)
		return -EINVAL;

	if (wq->monitor_avl_buf && wqe_size > gdma_wq_avail_space(wq)) {
		pr_err("unsuccessful flow control!\n");
		return -ENOSPC;
	}

	if (wqe_info)
		wqe_info->wqe_size_in_bu = wqe_size / GDMA_WQE_BU_SIZE;

	wqe_ptr = gdma_get_wqe_ptr(wq, wq->head);

	wqe_ptr += gdma_write_client_oob(wqe_ptr, wqe_req, wq->type,
					 client_oob_size, sgl_data_size);

	if (wqe_ptr >= (u8 *)wq->queue_mem_ptr + wq->queue_size)
		wqe_ptr -= wq->queue_size;

	gdma_write_sgl(wq, wqe_ptr, wqe_req);

	wq->head += wqe_size / GDMA_WQE_BU_SIZE;

	return 0;
}

int gdma_post_and_ring(struct gdma_queue *queue,
		       const struct gdma_wqe_request *wqe,
		       struct gdma_posted_wqe_info *wqe_info)
{
	struct gdma_context *gc = gdma_dev_to_context(queue->gdma_dev);

	int err = gdma_post_work_request(queue, wqe, wqe_info);

	if (err)
		return err;

	gdma_wq_ring_doorbell(gc, queue);

	return 0;
}

static int gdma_read_cqe(struct gdma_queue *cq, struct gdma_comp *comp)
{
	struct gdma_cqe *cq_cqe = cq->queue_mem_ptr;
	unsigned int cq_num_cqe = cq->queue_size / sizeof(struct gdma_cqe);
	struct gdma_cqe *cqe = &cq_cqe[cq->head % cq_num_cqe];
	u32 owner_bits, new_bits, old_bits;

	new_bits = (cq->head / cq_num_cqe) & GDMA_CQE_OWNER_MASK;
	old_bits = (cq->head / cq_num_cqe - 1) & GDMA_CQE_OWNER_MASK;
	owner_bits = cqe->cqe_info.owner_bits;

	/* Return 0 if no new entry. */
	if (owner_bits == old_bits)
		return 0;

	/* Return -1 if overflow detected. */
	if (owner_bits != new_bits)
		return -1;

	comp->wq_num = cqe->cqe_info.wq_num;
	comp->is_sq = cqe->cqe_info.is_sq;
	memcpy(comp->cqe_data, cqe->cqe_data, GDMA_COMP_DATA_SIZE);

	return 1;
}

int gdma_poll_cq(struct gdma_queue *cq, struct gdma_comp *comp, int num_cqe)
{
	int cqe_idx;
	int ret;

	for (cqe_idx = 0; cqe_idx < num_cqe; cqe_idx++) {
		ret = gdma_read_cqe(cq, &comp[cqe_idx]);

		if (ret < 0) {
			cq->head -= cqe_idx;
			return ret;
		}

		if (ret == 0)
			break;

		cq->head++;
	}

	return cqe_idx;
}

static irqreturn_t gdma_intr(int irq, void *arg)
{
	struct gdma_irq_context *gic = arg;

	if (gic->handler)
		gic->handler(gic->arg);

	return IRQ_HANDLED;
}

int gdma_alloc_res_map(u32 res_avail, struct gdma_resource *r)
{
	r->map = bitmap_zalloc(res_avail, GFP_KERNEL);
	if (!r->map)
		return -ENOMEM;

	r->size = res_avail;
	spin_lock_init(&r->lock);

	return 0;
}

void gdma_free_res_map(struct gdma_resource *r)
{
	bitmap_free(r->map);
	r->map = NULL;
	r->size = 0;
}

static int gdma_setup_irqs(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	struct gdma_irq_context *gic;
	int nvec, irq;
	int max_irqs;
	int err, i, j;

	max_irqs = min_t(uint, ANA_MAX_NUM_QUEUE + 1, num_online_cpus() + 1);
	nvec = pci_alloc_irq_vectors(pdev, 2, max_irqs, PCI_IRQ_MSIX);
	if (nvec < 0)
		return nvec;

	gc->irq_contexts = kcalloc(nvec, sizeof(struct gdma_irq_context),
				   GFP_KERNEL);
	if (!gc->irq_contexts) {
		err = -ENOMEM;
		goto free_irq_vector;
	}

	for (i = 0; i < nvec; i++) {
		gic = &gc->irq_contexts[i];
		gic->handler = NULL;
		gic->arg = NULL;

		irq = pci_irq_vector(pdev, i);
		if (irq < 0) {
			err = irq;
			goto free_irq;
		}

		err = request_irq(irq, gdma_intr, 0, "gdma_intr", gic);
		if (err)
			goto free_irq;
	}

	err = gdma_alloc_res_map(nvec, &gc->msix_resource);
	if (err)
		goto free_irq;

	gc->max_num_msix = nvec;
	gc->num_msix_usable = nvec;

	return 0;

free_irq:
	for (j = i - 1; j >= 0; j--) {
		irq = pci_irq_vector(pdev, j);
		gic = &gc->irq_contexts[j];
		free_irq(irq, gic);
	}

	kfree(gc->irq_contexts);
	gc->irq_contexts = NULL;
free_irq_vector:
	pci_free_irq_vectors(pdev);
	return err;
}

static void gdma_remove_irqs(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);
	struct gdma_irq_context *gic;
	int irq, i;

	if (gc->max_num_msix < 1)
		return;

	gdma_free_res_map(&gc->msix_resource);

	for (i = 0; i < gc->max_num_msix; i++) {
		irq = pci_irq_vector(pdev, i);
		if (WARN_ON(irq < 0))
			continue;

		gic = &gc->irq_contexts[i];
		free_irq(irq, gic);
	}

	pci_free_irq_vectors(pdev);

	gc->max_num_msix = 0;
	gc->num_msix_usable = 0;
	kfree(gc->irq_contexts);
	gc->irq_contexts = NULL;
}

static int gdma_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct gdma_context *gc;
	void __iomem *bar0_va;
	int bar = 0;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return -ENXIO;

	pci_set_master(pdev);

	err = pci_request_regions(pdev, "gdma");
	if (err)
		goto disable_dev;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		goto release_region;

	err = -ENOMEM;
	gc = vzalloc(sizeof(*gc));
	if (!gc)
		goto release_region;

	bar0_va = pci_iomap(pdev, bar, 0);
	if (!bar0_va)
		goto free_gc;

	gc->bar0_va = bar0_va;
	gc->pci_dev = pdev;

	pci_set_drvdata(pdev, gc);

	gdma_init_registers(pdev);

	shm_channel_init(&gc->shm_channel, gc->shm_base);

	err = gdma_setup_irqs(pdev);
	if (err)
		goto unmap_bar;

	mutex_init(&gc->eq_test_event_mutex);

	err = hwc_create_channel(gc);
	if (err)
		goto remove_irq;

	err = gdma_verify_vf_version(pdev);
	if (err)
		goto remove_irq;

	err = gdma_query_max_resources(pdev);
	if (err)
		goto remove_irq;

	err = gdma_detect_devices(pdev);
	if (err)
		goto remove_irq;

	err = ana_probe(&gc->ana);
	if (err)
		goto clean_up_gdma;

	return 0;

clean_up_gdma:
	hwc_destroy_channel(gc);
	vfree(gc->cq_table);
	gc->cq_table = NULL;
remove_irq:
	gdma_remove_irqs(pdev);
unmap_bar:
	pci_iounmap(pdev, bar0_va);
free_gc:
	vfree(gc);
release_region:
	pci_release_regions(pdev);
disable_dev:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	pr_err("gdma probe failed: err = %d\n", err);
	return err;
}

static void gdma_remove(struct pci_dev *pdev)
{
	struct gdma_context *gc = pci_get_drvdata(pdev);

	ana_remove(&gc->ana);

	hwc_destroy_channel(gc);
	vfree(gc->cq_table);
	gc->cq_table = NULL;

	gdma_remove_irqs(pdev);

	pci_iounmap(pdev, gc->bar0_va);

	vfree(gc);

	pci_release_regions(pdev);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

#ifndef PCI_VENDOR_ID_MICROSOFT
#define PCI_VENDOR_ID_MICROSOFT 0x1414
#endif

static const struct pci_device_id mana_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MICROSOFT, 0x00ba) },
	{ }
};

static struct pci_driver mana_driver = {
	.name		= "mana",
	.id_table	= mana_id_table,
	.probe		= gdma_probe,
	.remove		= gdma_remove,
};

module_pci_driver(mana_driver);

MODULE_DEVICE_TABLE(pci, mana_id_table);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Microsoft Azure Network Adapter driver");
