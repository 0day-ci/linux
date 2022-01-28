// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "iecm.h"

/**
 * iecm_mb_clean - Reclaim the send mailbox queue entries
 * @adapter: Driver specific private structure
 *
 * Reclaim the send mailbox queue entries to be used to send further messages
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_mb_clean(struct iecm_adapter *adapter)
{
	u16 i, num_q_msg = IECM_DFLT_MBX_Q_LEN;
	struct iecm_ctlq_msg **q_msg;
	struct iecm_dma_mem *dma_mem;
	int err = 0;

	q_msg = kcalloc(num_q_msg, sizeof(struct iecm_ctlq_msg *), GFP_KERNEL);
	if (!q_msg)
		return -ENOMEM;

	err = iecm_ctlq_clean_sq(adapter->hw.asq, &num_q_msg, q_msg);
	if (err)
		goto error;

	for (i = 0; i < num_q_msg; i++) {
		dma_mem = q_msg[i]->ctx.indirect.payload;
		if (dma_mem)
			dmam_free_coherent(&adapter->pdev->dev, dma_mem->size,
					   dma_mem->va, dma_mem->pa);
		kfree(q_msg[i]);
		kfree(dma_mem);
	}
error:
	kfree(q_msg);
	return err;
}

/**
 * iecm_find_ctlq - Given a type and id, find ctlq info
 * @hw: hardware struct
 * @type: type of ctrlq to find
 * @id: ctlq id to find
 *
 * Returns pointer to found ctlq info struct, NULL otherwise.
 */
static struct iecm_ctlq_info *iecm_find_ctlq(struct iecm_hw *hw,
					     enum iecm_ctlq_type type, int id)
{
	struct iecm_ctlq_info *cq, *tmp;

	list_for_each_entry_safe(cq, tmp, &hw->cq_list_head, cq_list) {
		if (cq->q_id == id && cq->cq_type == type)
			return cq;
	}

	return NULL;
}

/**
 * iecm_init_dflt_mbx - Setup default mailbox parameters and make request
 * @adapter: adapter info struct
 *
 * Returns 0 on success, negative otherwise
 */
int iecm_init_dflt_mbx(struct iecm_adapter *adapter)
{
	struct iecm_ctlq_create_info ctlq_info[] = {
		{
			.type = IECM_CTLQ_TYPE_MAILBOX_TX,
			.id = IECM_DFLT_MBX_ID,
			.len = IECM_DFLT_MBX_Q_LEN,
			.buf_size = IECM_DFLT_MBX_BUF_SIZE
		},
		{
			.type = IECM_CTLQ_TYPE_MAILBOX_RX,
			.id = IECM_DFLT_MBX_ID,
			.len = IECM_DFLT_MBX_Q_LEN,
			.buf_size = IECM_DFLT_MBX_BUF_SIZE
		}
	};
	struct iecm_hw *hw = &adapter->hw;
	int err;

	adapter->dev_ops.reg_ops.ctlq_reg_init(ctlq_info);

#define NUM_Q 2
	err = iecm_ctlq_init(hw, NUM_Q, ctlq_info);
	if (err)
		return err;

	hw->asq = iecm_find_ctlq(hw, IECM_CTLQ_TYPE_MAILBOX_TX,
				 IECM_DFLT_MBX_ID);
	hw->arq = iecm_find_ctlq(hw, IECM_CTLQ_TYPE_MAILBOX_RX,
				 IECM_DFLT_MBX_ID);

	if (!hw->asq || !hw->arq) {
		iecm_ctlq_deinit(hw);
		return -ENOENT;
	}
	adapter->state = __IECM_STARTUP;
	/* Skew the delay for init tasks for each function based on fn number
	 * to prevent every function from making the same call simulatenously.
	 */
	queue_delayed_work(adapter->init_wq, &adapter->init_task,
			   msecs_to_jiffies(5 * (adapter->pdev->devfn & 0x07)));
	return 0;
}

/**
 * iecm_deinit_dflt_mbx - Free up ctlqs setup
 * @adapter: Driver specific private data structure
 */
void iecm_deinit_dflt_mbx(struct iecm_adapter *adapter)
{
	if (adapter->hw.arq && adapter->hw.asq) {
		iecm_mb_clean(adapter);
		iecm_ctlq_deinit(&adapter->hw);
	}
	adapter->hw.arq = NULL;
	adapter->hw.asq = NULL;
}

/**
 * iecm_vport_params_buf_alloc - Allocate memory for MailBox resources
 * @adapter: Driver specific private data structure
 *
 * Will alloc memory to hold the vport parameters received on MailBox
 */
int iecm_vport_params_buf_alloc(struct iecm_adapter *adapter)
{
	adapter->vport_params_reqd = kcalloc(IECM_MAX_NUM_VPORTS,
					     sizeof(*adapter->vport_params_reqd),
					     GFP_KERNEL);
	if (!adapter->vport_params_reqd)
		return -ENOMEM;

	adapter->vport_params_recvd = kcalloc(IECM_MAX_NUM_VPORTS,
					      sizeof(*adapter->vport_params_recvd),
					      GFP_KERNEL);
	if (!adapter->vport_params_recvd) {
		kfree(adapter->vport_params_reqd);
		return -ENOMEM;
	}

	return 0;
}

/**
 * iecm_vport_params_buf_rel - Release memory for MailBox resources
 * @adapter: Driver specific private data structure
 *
 * Will release memory to hold the vport parameters received on MailBox
 */
void iecm_vport_params_buf_rel(struct iecm_adapter *adapter)
{
	int i = 0;

	for (i = 0; i < IECM_MAX_NUM_VPORTS; i++) {
		kfree(adapter->vport_params_recvd[i]);
		kfree(adapter->vport_params_reqd[i]);
	}

	kfree(adapter->vport_params_recvd);
	kfree(adapter->vport_params_reqd);

	kfree(adapter->caps);
	kfree(adapter->config_data.req_qs_chunks);
}
