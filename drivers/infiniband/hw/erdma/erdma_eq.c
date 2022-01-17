// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_hw.h"
#include "erdma_verbs.h"

static int erdma_poll_aeq_event(struct erdma_eq *aeq, void *out)
{
	struct erdma_aeqe *aeqe = (struct erdma_aeqe *)aeq->qbuf + (aeq->ci & (aeq->depth - 1));
	u32 val;

	val = le32_to_cpu(READ_ONCE(aeqe->hdr));
	if (FIELD_GET(ERDMA_AEQE_HDR_O_MASK, val) == aeq->owner) {
		dma_rmb();
		aeq->ci++;
		if ((aeq->ci & (aeq->depth - 1)) == 0)
			aeq->owner = !aeq->owner;

		atomic64_inc(&aeq->event_num);
		if (out)
			memcpy(out, aeqe, sizeof(struct erdma_aeqe));

		return 1;
	}

	return 0;
}

void erdma_aeq_event_handler(struct erdma_pci_drvdata *drvdata)
{
	struct erdma_aeqe aeqe;
	u32 cqn, qpn;
	struct erdma_qp *qp;
	struct erdma_cq *cq;
	struct ib_event event;

	memset(&event, 0, sizeof(event));
	while (erdma_poll_aeq_event(&drvdata->aeq.eq, &aeqe)) {
		if (FIELD_GET(ERDMA_AEQE_HDR_TYPE_MASK, aeqe.hdr) == ERDMA_AE_TYPE_CQ_ERR) {
			cqn = aeqe.event_data0;
			cq = find_cq_by_cqn(drvdata->dev, cqn);
			if (!cq)
				continue;
			event.device = cq->ibcq.device;
			event.element.cq = &cq->ibcq;
			event.event = IB_EVENT_CQ_ERR;
			if (cq->ibcq.event_handler)
				cq->ibcq.event_handler(&event, cq->ibcq.cq_context);
		} else {
			qpn = aeqe.event_data0;
			qp = find_qp_by_qpn(drvdata->dev, qpn);
			if (!qp)
				continue;

			event.device = qp->ibqp.device;
			event.element.qp = &qp->ibqp;
			event.event = IB_EVENT_QP_FATAL;
			if (qp->ibqp.event_handler)
				qp->ibqp.event_handler(&event, qp->ibqp.qp_context);
		}
	}

	notify_eq(&drvdata->aeq.eq);
}

int erdma_aeq_init(struct erdma_pci_drvdata *drvdata)
{
	struct erdma_eq *eq = &drvdata->aeq.eq;
	u32 buf_size = ERDMA_DEFAULT_EQ_DEPTH << EQE_SHIFT;

	eq->qbuf = dma_alloc_coherent(&drvdata->pdev->dev, buf_size + ERDMA_EXTRA_BUFFER_SIZE,
				      &eq->qbuf_dma_addr, GFP_KERNEL);
	if (!eq->qbuf)
		return -ENOMEM;

	eq->db_info = eq->qbuf + buf_size;

	memset(eq->qbuf, 0, buf_size);
	memset(eq->db_info, 0, 8);

	spin_lock_init(&eq->lock);
	atomic64_set(&eq->event_num, 0);
	atomic64_set(&eq->notify_num, 0);

	eq->depth = ERDMA_DEFAULT_EQ_DEPTH;
	eq->db_addr = (u64 __iomem *)(drvdata->func_bar + ERDMA_REGS_AEQ_DB_REG);
	eq->ci = 0;

	eq->owner = 1;
	drvdata->aeq.dev = drvdata;

	drvdata->aeq.ready = 1;

	erdma_reg_write32(drvdata, ERDMA_REGS_AEQ_ADDR_H_REG, upper_32_bits(eq->qbuf_dma_addr));
	erdma_reg_write32(drvdata, ERDMA_REGS_AEQ_ADDR_L_REG, lower_32_bits(eq->qbuf_dma_addr));
	erdma_reg_write32(drvdata, ERDMA_REGS_AEQ_DEPTH_REG, eq->depth);
	erdma_reg_write64(drvdata, ERDMA_AEQ_DB_HOST_ADDR_REG, eq->qbuf_dma_addr + buf_size);

	return 0;
}

void erdma_aeq_destroy(struct erdma_pci_drvdata *drvdata)
{
	struct erdma_eq *eq = &drvdata->aeq.eq;
	u32 buf_size = ERDMA_DEFAULT_EQ_DEPTH << EQE_SHIFT;

	drvdata->aeq.ready = 0;

	dma_free_coherent(&drvdata->pdev->dev, buf_size + ERDMA_EXTRA_BUFFER_SIZE, eq->qbuf,
			  eq->qbuf_dma_addr);
}

#define MAX_POLL_CHUNK_SIZE 16
void erdma_ceq_completion_handler(struct erdma_eq_cb *ceq_cb)
{
	int cqn;
	struct erdma_cq *cq;
	struct erdma_dev *dev = ceq_cb->dev;
	u32 poll_cnt = 0;

	if (!ceq_cb->ready)
		return;

	while ((cqn = erdma_poll_ceq_event(&ceq_cb->eq)) != -1) {
		poll_cnt++;
		if (cqn == 0)
			continue;

		cq = find_cq_by_cqn(dev, cqn);
		if (!cq)
			continue;

		if (cq->is_kernel_cq)
			cq->kern_cq.cmdsn++;

		if (cq->ibcq.comp_handler)
			cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);

		if (poll_cnt >= MAX_POLL_CHUNK_SIZE)
			break;
	}

	notify_eq(&ceq_cb->eq);
}

static irqreturn_t erdma_intr_ceq_handler(int irq, void *data)
{
	struct erdma_eq_cb *ceq_cb = data;

	tasklet_schedule(&ceq_cb->tasklet);

	return IRQ_HANDLED;
}

static void erdma_intr_ceq_task(unsigned long data)
{
	erdma_ceq_completion_handler((struct erdma_eq_cb *)data);
}

static int erdma_set_ceq_irq(struct erdma_pci_drvdata *drvdata, u16 eqn)
{
	u32 cpu;
	int err;
	struct erdma_irq_info *irq_info = &drvdata->ceqs[eqn - 1].irq_info;

	snprintf(irq_info->name, ERDMA_IRQNAME_SIZE, "erdma-ceq%u@pci:%s",
		eqn - 1, pci_name(drvdata->pdev));
	irq_info->handler = erdma_intr_ceq_handler;
	irq_info->data = &drvdata->ceqs[eqn - 1];
	irq_info->msix_vector = pci_irq_vector(drvdata->pdev, eqn);

	tasklet_init(&drvdata->ceqs[eqn - 1].tasklet, erdma_intr_ceq_task,
		     (unsigned long)&drvdata->ceqs[eqn - 1]);

	cpu = cpumask_local_spread(eqn, drvdata->numa_node);
	irq_info->cpu = cpu;
	cpumask_set_cpu(cpu, &irq_info->affinity_hint_mask);
	dev_info(&drvdata->pdev->dev, "setup irq:%p vector:%d name:%s\n", irq_info,
		 irq_info->msix_vector, irq_info->name);

	err = request_irq(irq_info->msix_vector, irq_info->handler, 0, irq_info->name,
			  irq_info->data);
	if (err) {
		dev_err(&drvdata->pdev->dev, "failed to request_irq(%d)\n", err);
		return err;
	}

	irq_set_affinity_hint(irq_info->msix_vector, &irq_info->affinity_hint_mask);

	return 0;
}

static void erdma_free_ceq_irq(struct erdma_pci_drvdata *drvdata, u16 eqn)
{
	struct erdma_irq_info *irq_info = &drvdata->ceqs[eqn - 1].irq_info;

	irq_set_affinity_hint(irq_info->msix_vector, NULL);
	free_irq(irq_info->msix_vector, irq_info->data);
}

static int create_eq_cmd(struct erdma_pci_drvdata *drvdata, u32 eqn, struct erdma_eq *eq)
{
	int err;
	struct erdma_cmdq_create_eq_req req;
	dma_addr_t db_info_dma_addr;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_COMMON, CMDQ_OPCODE_CREATE_EQ);
	req.eqn = eqn;
	req.depth = ilog2(eq->depth);
	req.qbuf_addr = eq->qbuf_dma_addr;
	req.qtype = 1; /* CEQ */
	/* Vector index is the same sa EQN. */
	req.vector_idx = eqn;
	db_info_dma_addr = eq->qbuf_dma_addr + (eq->depth << EQE_SHIFT);
	req.db_dma_addr_l = lower_32_bits(db_info_dma_addr);
	req.db_dma_addr_h = upper_32_bits(db_info_dma_addr);

	err = erdma_post_cmd_wait(&drvdata->cmdq, (u64 *)&req,
				  sizeof(struct erdma_cmdq_create_eq_req), NULL, NULL);
	if (err) {
		dev_err(&drvdata->pdev->dev, "ERROR: err code = %d, cmd of create eq failed.\n",
			err);
		return err;
	}

	return 0;
}

static int erdma_ceq_init_one(struct erdma_pci_drvdata *drvdata, u16 eqn)
{
	/* CEQ indexed from 1, 0 rsvd for CMDQ-EQ. */
	struct erdma_eq *eq = &drvdata->ceqs[eqn - 1].eq;
	u32 buf_size = ERDMA_DEFAULT_EQ_DEPTH << EQE_SHIFT;
	int ret;

	eq->qbuf = dma_alloc_coherent(&drvdata->pdev->dev, buf_size + ERDMA_EXTRA_BUFFER_SIZE,
				      &eq->qbuf_dma_addr, GFP_KERNEL);
	if (!eq->qbuf)
		return -ENOMEM;

	eq->db_info = eq->qbuf + ERDMA_EXTRA_BUFFER_SIZE;

	memset(eq->qbuf, 0, buf_size);
	memset(eq->db_info, 0, ERDMA_EXTRA_BUFFER_SIZE);

	spin_lock_init(&eq->lock);
	atomic64_set(&eq->event_num, 0);
	atomic64_set(&eq->notify_num, 0);

	eq->depth = ERDMA_DEFAULT_EQ_DEPTH;
	eq->db_addr = (u64 __iomem *)(drvdata->func_bar + ERDMA_REGS_CEQ_DB_BASE_REG + eqn * 8);
	eq->ci = 0;
	eq->owner = 1;
	drvdata->ceqs[eqn - 1].dev = drvdata;

	ret = create_eq_cmd(drvdata, eqn, eq);
	if (ret) {
		drvdata->ceqs[eqn - 1].ready = 0;
		return ret;
	}

	drvdata->ceqs[eqn - 1].ready = 1;

	return ret;
}

static void erdma_ceq_uninit_one(struct erdma_pci_drvdata *drvdata, u16 eqn)
{
	struct erdma_eq *eq = &drvdata->ceqs[eqn - 1].eq;
	u32 buf_size = ERDMA_DEFAULT_EQ_DEPTH << EQE_SHIFT;
	struct erdma_cmdq_destroy_eq_req req;
	int err;

	drvdata->ceqs[eqn - 1].ready = 0;

	ERDMA_CMDQ_BUILD_REQ_HDR(&req, CMDQ_SUBMOD_COMMON, CMDQ_OPCODE_DESTROY_EQ);
	req.eqn = eqn;
	req.qtype = 1;
	req.vector_idx = eqn;

	err = erdma_post_cmd_wait(&drvdata->cmdq, (u64 *)&req, sizeof(req), NULL, NULL);
	if (err) {
		dev_err(&drvdata->pdev->dev, "ERROR: err code = %d, cmd of destroy eq failed.\n",
			err);
		return;
	}

	dma_free_coherent(&drvdata->pdev->dev, buf_size + ERDMA_EXTRA_BUFFER_SIZE, eq->qbuf,
			  eq->qbuf_dma_addr);
}

int erdma_ceqs_init(struct erdma_pci_drvdata *drvdata)
{
	u32 i, j;
	int err = 0;

	for (i = 1; i < drvdata->irq_num; i++) {
		err = erdma_ceq_init_one(drvdata, i);
		if (err)
			goto out_err;

		err = erdma_set_ceq_irq(drvdata, i);
		if (err) {
			erdma_ceq_uninit_one(drvdata, i);
			goto out_err;
		}
	}

	return 0;

out_err:
	for (j = 1; j < i; j++) {
		erdma_free_ceq_irq(drvdata, j);
		erdma_ceq_uninit_one(drvdata, j);
	}

	return err;
}

void erdma_ceqs_uninit(struct erdma_pci_drvdata *drvdata)
{
	u32 i;

	for (i = 1; i < drvdata->irq_num; i++) {
		erdma_free_ceq_irq(drvdata, i);
		erdma_ceq_uninit_one(drvdata, i);
	}
}
