// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "iecm.h"

/**
 * iecm_recv_event_msg - Receive virtchnl event message
 * @vport: virtual port structure
 *
 * Receive virtchnl event message
 */
static void iecm_recv_event_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl_pf_event *vpe;
	struct virtchnl2_event *v2e;
	bool link_status;
	u32 event;

	if (adapter->virt_ver_maj < VIRTCHNL_VERSION_MAJOR_2) {
		vpe = (struct virtchnl_pf_event *)adapter->vc_msg;
		event = vpe->event;
	} else {
		v2e = (struct virtchnl2_event *)adapter->vc_msg;
		event = le32_to_cpu(v2e->event);
	}

	switch (event) {
	case VIRTCHNL2_EVENT_LINK_CHANGE:
		if (adapter->virt_ver_maj < VIRTCHNL_VERSION_MAJOR_2) {
			if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS,
					    VIRTCHNL2_CAP_LINK_SPEED)) {
				adapter->link_speed_mbps =
				vpe->event_data.link_event_adv.link_speed;
				link_status =
				vpe->event_data.link_event_adv.link_status;
			} else {
				adapter->link_speed =
				vpe->event_data.link_event.link_speed;
				link_status =
				vpe->event_data.link_event.link_status;
			}
		} else {
			adapter->link_speed_mbps = le32_to_cpu(v2e->link_speed);
			link_status = v2e->link_status;
		}
		if (adapter->link_up != link_status) {
			adapter->link_up = link_status;
			if (adapter->state == __IECM_UP) {
				if (adapter->link_up) {
					netif_tx_start_all_queues(vport->netdev);
					netif_carrier_on(vport->netdev);
				} else {
					netif_tx_stop_all_queues(vport->netdev);
					netif_carrier_off(vport->netdev);
				}
			}
		}
		break;
	case VIRTCHNL_EVENT_RESET_IMPENDING:
		set_bit(__IECM_HR_CORE_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
		break;
	default:
		dev_err(&vport->adapter->pdev->dev,
			"Unknown event %d from PF\n", event);
		break;
	}
	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
}

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
 * iecm_send_mb_msg - Send message over mailbox
 * @adapter: Driver specific private structure
 * @op: virtchnl opcode
 * @msg_size: size of the payload
 * @msg: pointer to buffer holding the payload
 *
 * Will prepare the control queue message and initiates the send api
 *
 * Returns 0 on success, negative on failure
 */
int iecm_send_mb_msg(struct iecm_adapter *adapter, enum virtchnl_ops op,
		     u16 msg_size, u8 *msg)
{
	struct iecm_ctlq_msg *ctlq_msg;
	struct iecm_dma_mem *dma_mem;
	int err = 0;

	if (iecm_is_reset_detected(adapter))
		return -EBUSY;

	err = iecm_mb_clean(adapter);
	if (err)
		return err;

	ctlq_msg = kzalloc(sizeof(*ctlq_msg), GFP_KERNEL);
	if (!ctlq_msg)
		return -ENOMEM;

	dma_mem = kzalloc(sizeof(*dma_mem), GFP_KERNEL);
	if (!dma_mem) {
		err = -ENOMEM;
		goto dma_mem_error;
	}

	memset(ctlq_msg, 0, sizeof(struct iecm_ctlq_msg));
	ctlq_msg->opcode = iecm_mbq_opc_send_msg_to_pf;
	ctlq_msg->func_id = 0;
	ctlq_msg->data_len = msg_size;
	ctlq_msg->cookie.mbx.chnl_opcode = op;
	ctlq_msg->cookie.mbx.chnl_retval = VIRTCHNL_STATUS_SUCCESS;
	dma_mem->size = IECM_DFLT_MBX_BUF_SIZE;
	dma_mem->va = dmam_alloc_coherent(&adapter->pdev->dev, dma_mem->size,
					  &dma_mem->pa, GFP_KERNEL);
	if (!dma_mem->va) {
		err = -ENOMEM;
		goto dma_alloc_error;
	}
	memcpy(dma_mem->va, msg, msg_size);
	ctlq_msg->ctx.indirect.payload = dma_mem;

	err = iecm_ctlq_send(&adapter->hw, adapter->hw.asq, 1, ctlq_msg);
	if (err)
		goto send_error;

	return 0;
send_error:
	dmam_free_coherent(&adapter->pdev->dev, dma_mem->size, dma_mem->va,
			   dma_mem->pa);
dma_alloc_error:
	kfree(dma_mem);
dma_mem_error:
	kfree(ctlq_msg);
	return err;
}
EXPORT_SYMBOL(iecm_send_mb_msg);

/**
 * iecm_set_msg_pending_bit - Wait for clear and set msg pending
 * @adapter: driver specific private structure
 *
 * If clear sets msg pending bit, otherwise waits for it to clear before
 * setting it again. Returns 0 on success, negative on failure.
 */
static int iecm_set_msg_pending_bit(struct iecm_adapter *adapter)
{
	unsigned int retries = 100;

	/* If msg pending bit already set, there's a message waiting to be
	 * parsed and we must wait for it to be cleared before copying a new
	 * message into the vc_msg buffer or else we'll stomp all over the
	 * previous message.
	 */
	while (retries) {
		if (!test_and_set_bit(__IECM_VC_MSG_PENDING,
				      adapter->flags))
			break;
		msleep(20);
		retries--;
	}
	return retries ? 0 : -ETIMEDOUT;
}

/**
 * iecm_set_msg_pending - Wait for msg pending bit and copy msg to buf
 * @adapter: driver specific private structure
 * @ctlq_msg: msg to copy from
 * @err_enum: err bit to set on error
 *
 * Copies payload from ctlq_msg into vc_msg buf in adapter and sets msg pending
 * bit. Returns 0 on success, negative on failure.
 */
int iecm_set_msg_pending(struct iecm_adapter *adapter,
			 struct iecm_ctlq_msg *ctlq_msg,
			 enum iecm_vport_vc_state err_enum)
{
	if (ctlq_msg->cookie.mbx.chnl_retval) {
		set_bit(err_enum, adapter->vc_state);
		return -EINVAL;
	}

	if (iecm_set_msg_pending_bit(adapter)) {
		set_bit(err_enum, adapter->vc_state);
		dev_info(&adapter->pdev->dev, "Timed out setting msg pending\n");
		return -ETIMEDOUT;
	}

	memcpy(adapter->vc_msg, ctlq_msg->ctx.indirect.payload->va,
	       min_t(int, ctlq_msg->ctx.indirect.payload->size,
		     IECM_DFLT_MBX_BUF_SIZE));
	return 0;
}
EXPORT_SYMBOL(iecm_set_msg_pending);

/**
 * iecm_recv_mb_msg - Receive message over mailbox
 * @adapter: Driver specific private structure
 * @op: virtchannel operation code
 * @msg: Received message holding buffer
 * @msg_size: message size
 *
 * Will receive control queue message and posts the receive buffer. Returns 0
 * on success and negative on failure.
 */
int iecm_recv_mb_msg(struct iecm_adapter *adapter, enum virtchnl_ops op,
		     void *msg, int msg_size)
{
	struct iecm_ctlq_msg ctlq_msg;
	struct iecm_dma_mem *dma_mem;
	struct iecm_vport *vport;
	bool work_done = false;
	int num_retry = 2000;
	int payload_size = 0;
	u16 num_q_msg;
	int err = 0;

	vport = adapter->vports[0];
	while (1) {
		/* Try to get one message */
		num_q_msg = 1;
		dma_mem = NULL;
		err = iecm_ctlq_recv(adapter->hw.arq, &num_q_msg, &ctlq_msg);
		/* If no message then decide if we have to retry based on
		 * opcode
		 */
		if (err || !num_q_msg) {
			/* Increasing num_retry to consider the delayed
			 * responses because of large number of VF's mailbox
			 * messages. If the mailbox message is received from
			 * the other side, we come out of the sleep cycle
			 * immediately else we wait for more time.
			 */
			if (op && num_retry-- &&
			    !test_bit(__IECM_REL_RES_IN_PROG, adapter->flags)) {
				msleep(20);
				continue;
			} else {
				break;
			}
		}

		/* If we are here a message is received. Check if we are looking
		 * for a specific message based on opcode. If it is different
		 * ignore and post buffers
		 */
		if (op && ctlq_msg.cookie.mbx.chnl_opcode != op)
			goto post_buffs;

		if (ctlq_msg.data_len)
			payload_size = ctlq_msg.ctx.indirect.payload->size;

		/* All conditions are met. Either a message requested is
		 * received or we received a message to be processed
		 */
		switch (ctlq_msg.cookie.mbx.chnl_opcode) {
		case VIRTCHNL_OP_VERSION:
		case VIRTCHNL2_OP_GET_CAPS:
		case VIRTCHNL2_OP_CREATE_VPORT:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_info(&adapter->pdev->dev, "Failure initializing, vc op: %u retval: %u\n",
					 ctlq_msg.cookie.mbx.chnl_opcode,
					 ctlq_msg.cookie.mbx.chnl_retval);
				err = -EBADMSG;
			} else if (msg) {
				memcpy(msg, ctlq_msg.ctx.indirect.payload->va,
				       min_t(int,
					     payload_size, msg_size));
			}
			work_done = true;
			break;
		case VIRTCHNL2_OP_ENABLE_VPORT:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_ENA_VPORT_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_ENA_VPORT, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_DISABLE_VPORT:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_DIS_VPORT_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_DIS_VPORT, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_DESTROY_VPORT:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_DESTROY_VPORT_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_DESTROY_VPORT, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_CONFIG_TX_QUEUES:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_CONFIG_TXQ_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_CONFIG_TXQ, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_CONFIG_RX_QUEUES:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_CONFIG_RXQ_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_CONFIG_RXQ, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_ENABLE_QUEUES:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_ENA_QUEUES_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_ENA_QUEUES, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_DISABLE_QUEUES:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_DIS_QUEUES_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_DIS_QUEUES, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_ADD_QUEUES:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_ADD_QUEUES_ERR);
			set_bit(IECM_VC_ADD_QUEUES, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_DEL_QUEUES:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_DEL_QUEUES_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_DEL_QUEUES, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_MAP_QUEUE_VECTOR:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_MAP_IRQ_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_MAP_IRQ, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_UNMAP_QUEUE_VECTOR:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_UNMAP_IRQ_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_UNMAP_IRQ, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_GET_STATS:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_GET_STATS_ERR);
			set_bit(IECM_VC_GET_STATS, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_GET_RSS_HASH:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_GET_RSS_HASH_ERR);
			set_bit(IECM_VC_GET_RSS_HASH, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_SET_RSS_HASH:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_SET_RSS_HASH_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_SET_RSS_HASH, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_GET_RSS_LUT:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_GET_RSS_LUT_ERR);
			set_bit(IECM_VC_GET_RSS_LUT, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_SET_RSS_LUT:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_SET_RSS_LUT_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_SET_RSS_LUT, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_GET_RSS_KEY:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_GET_RSS_KEY_ERR);
			set_bit(IECM_VC_GET_RSS_KEY, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_SET_RSS_KEY:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_SET_RSS_KEY_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_SET_RSS_KEY, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_ALLOC_VECTORS:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_ALLOC_VECTORS_ERR);
			set_bit(IECM_VC_ALLOC_VECTORS, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_DEALLOC_VECTORS:
			if (ctlq_msg.cookie.mbx.chnl_retval)
				set_bit(IECM_VC_DEALLOC_VECTORS_ERR,
					adapter->vc_state);
			set_bit(IECM_VC_DEALLOC_VECTORS, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_GET_PTYPE_INFO:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_GET_PTYPE_INFO_ERR);
			set_bit(IECM_VC_GET_PTYPE_INFO, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL2_OP_EVENT:
		case VIRTCHNL_OP_EVENT:
			if (iecm_set_msg_pending_bit(adapter)) {
				dev_info(&adapter->pdev->dev, "Timed out setting msg pending\n");
			} else {
				memcpy(adapter->vc_msg,
				       ctlq_msg.ctx.indirect.payload->va,
				       min_t(int, payload_size,
					     IECM_DFLT_MBX_BUF_SIZE));
				iecm_recv_event_msg(vport);
			}
			break;
		case VIRTCHNL_OP_ADD_ETH_ADDR:
			if (test_and_clear_bit(__IECM_ADD_ETH_REQ, adapter->flags)) {
				/* Message was sent asynchronously. We don't
				 * normally print errors here, instead
				 * preferring to handle errors in the function
				 * calling wait_for_event. However, we will
				 * have lost the context in which we sent the
				 * message if asynchronous. We can't really do
				 * anything about at it this point, but we
				 * should at a minimum indicate that it looks
				 * like something went wrong. Also don't bother
				 * setting ERR bit or waking vchnl_wq since no
				 * one will be waiting to read the async
				 * message.
				 */
				if (ctlq_msg.cookie.mbx.chnl_retval) {
					dev_err(&adapter->pdev->dev, "Failed to add MAC address: %d\n",
						ctlq_msg.cookie.mbx.chnl_retval);
				}
				break;
			}
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_ADD_ETH_ADDR_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_ADD_ETH_ADDR, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DEL_ETH_ADDR:
			if (test_and_clear_bit(__IECM_DEL_ETH_REQ, adapter->flags)) {
				/* Message was sent asynchronously. We don't
				 * normally print errors here, instead
				 * preferring to handle errors in the function
				 * calling wait_for_event. However, we will
				 * have lost the context in which we sent the
				 * message if asynchronous. We can't really do
				 * anything about at it this point, but we
				 * should at a minimum indicate that it looks
				 * like something went wrong. Also don't bother
				 * setting ERR bit or waking vchnl_wq since no
				 * one will be waiting to read the async
				 * message.
				 */
				if (ctlq_msg.cookie.mbx.chnl_retval) {
					dev_err(&adapter->pdev->dev, "Failed to delete MAC address: %d\n",
						ctlq_msg.cookie.mbx.chnl_retval);
				}
				break;
			}
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_DEL_ETH_ADDR_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_DEL_ETH_ADDR, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_OFFLOAD_VLAN_V2_CAPS_ERR, adapter->vc_state);
			} else {
				memcpy(adapter->vc_msg,
				       ctlq_msg.ctx.indirect.payload->va,
				       min_t(int, payload_size,
					     IECM_DFLT_MBX_BUF_SIZE));
			}
			set_bit(IECM_VC_OFFLOAD_VLAN_V2_CAPS, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_ADD_VLAN_V2:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to add vlan filter: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
			}
			break;
		case VIRTCHNL_OP_DEL_VLAN_V2:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to delete vlan filter: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
			}
			break;
		case VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_INSERTION_ENA_VLAN_V2_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_INSERTION_ENA_VLAN_V2,
				adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_INSERTION_DIS_VLAN_V2_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_INSERTION_DIS_VLAN_V2,
				adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_STRIPPING_ENA_VLAN_V2_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_STRIPPING_ENA_VLAN_V2,
				adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				set_bit(IECM_VC_STRIPPING_DIS_VLAN_V2_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_STRIPPING_DIS_VLAN_V2,
				adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
			/* This message can only be sent asynchronously. As
			 * such we'll have lost the context in which it was
			 * called and thus can only really report if it looks
			 * like an error occurred. Don't bother setting ERR bit
			 * or waking chnl_wq since no will be waiting to
			 * reading message.
			 */
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to set promiscuous mode: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
			}
			break;
		case VIRTCHNL_OP_ADD_CLOUD_FILTER:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to add cloud filter: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
				set_bit(IECM_VC_ADD_CLOUD_FILTER_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_ADD_CLOUD_FILTER, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DEL_CLOUD_FILTER:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to delete cloud filter: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
				set_bit(IECM_VC_DEL_CLOUD_FILTER_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_DEL_CLOUD_FILTER, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_ADD_RSS_CFG:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to add RSS configuration: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
				set_bit(IECM_VC_ADD_RSS_CFG_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_ADD_RSS_CFG, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DEL_RSS_CFG:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to delete RSS configuration: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
				set_bit(IECM_VC_DEL_RSS_CFG_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_DEL_RSS_CFG, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_ADD_FDIR_FILTER:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_ADD_FDIR_FILTER_ERR);
			set_bit(IECM_VC_ADD_FDIR_FILTER, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DEL_FDIR_FILTER:
			iecm_set_msg_pending(adapter, &ctlq_msg,
					     IECM_VC_DEL_FDIR_FILTER_ERR);
			set_bit(IECM_VC_DEL_FDIR_FILTER, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_ENABLE_CHANNELS:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to enable channels: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
				set_bit(IECM_VC_ENA_CHANNELS_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_ENA_CHANNELS, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		case VIRTCHNL_OP_DISABLE_CHANNELS:
			if (ctlq_msg.cookie.mbx.chnl_retval) {
				dev_err(&adapter->pdev->dev, "Failed to disable channels: %d\n",
					ctlq_msg.cookie.mbx.chnl_retval);
				set_bit(IECM_VC_DIS_CHANNELS_ERR,
					adapter->vc_state);
			}
			set_bit(IECM_VC_DIS_CHANNELS, adapter->vc_state);
			wake_up(&adapter->vchnl_wq);
			break;
		default:
			if (adapter->dev_ops.vc_ops.recv_mbx_msg)
				err =
				adapter->dev_ops.vc_ops.recv_mbx_msg(adapter,
								   msg,
								   msg_size,
								   &ctlq_msg,
								   &work_done);
			else
				dev_warn(&adapter->pdev->dev,
					 "Unhandled virtchnl response %d\n",
					 ctlq_msg.cookie.mbx.chnl_opcode);
			break;
		} /* switch v_opcode */
post_buffs:
		if (ctlq_msg.data_len)
			dma_mem = ctlq_msg.ctx.indirect.payload;
		else
			num_q_msg = 0;

		err = iecm_ctlq_post_rx_buffs(&adapter->hw, adapter->hw.arq,
					      &num_q_msg, &dma_mem);
		/* If post failed clear the only buffer we supplied */
		if (err && dma_mem)
			dmam_free_coherent(&adapter->pdev->dev, dma_mem->size,
					   dma_mem->va, dma_mem->pa);
		/* Applies only if we are looking for a specific opcode */
		if (work_done)
			break;
	}

	return err;
}
EXPORT_SYMBOL(iecm_recv_mb_msg);

/**
 * iecm_send_ver_msg - send virtchnl version message
 * @adapter: Driver specific private structure
 *
 * Send virtchnl version message.  Returns 0 on success, negative on failure.
 */
static int iecm_send_ver_msg(struct iecm_adapter *adapter)
{
	struct virtchnl_version_info vvi;

	if (adapter->virt_ver_maj) {
		vvi.major = adapter->virt_ver_maj;
		vvi.minor = adapter->virt_ver_min;
	} else {
		vvi.major = IECM_VIRTCHNL_VERSION_MAJOR;
		vvi.minor = IECM_VIRTCHNL_VERSION_MINOR;
	}

	return iecm_send_mb_msg(adapter, VIRTCHNL_OP_VERSION, sizeof(vvi),
				(u8 *)&vvi);
}

/**
 * iecm_recv_ver_msg - Receive virtchnl version message
 * @adapter: Driver specific private structure
 *
 * Receive virtchnl version message. Returns 0 on success, -EAGAIN if we need
 * to send version message again, otherwise negative on failure.
 */
static int iecm_recv_ver_msg(struct iecm_adapter *adapter)
{
	struct virtchnl_version_info vvi;
	int err = 0;

	err = iecm_recv_mb_msg(adapter, VIRTCHNL_OP_VERSION, &vvi, sizeof(vvi));
	if (err)
		return err;

	if (vvi.major > IECM_VIRTCHNL_VERSION_MAJOR) {
		dev_warn(&adapter->pdev->dev, "Virtchnl major version greater than supported\n");
		return -EINVAL;
	}
	if (vvi.major == IECM_VIRTCHNL_VERSION_MAJOR &&
	    vvi.minor > IECM_VIRTCHNL_VERSION_MINOR)
		dev_warn(&adapter->pdev->dev, "Virtchnl minor version not matched\n");

	/* If we have a mismatch, resend version to update receiver on what
	 * version we will use.
	 */
	if (!adapter->virt_ver_maj &&
	    vvi.major != IECM_VIRTCHNL_VERSION_MAJOR &&
	    vvi.minor != IECM_VIRTCHNL_VERSION_MINOR)
		err = -EAGAIN;

	adapter->virt_ver_maj = vvi.major;
	adapter->virt_ver_min = vvi.minor;

	return err;
}

/**
 * iecm_send_get_caps_msg - Send virtchnl get capabilities message
 * @adapter: Driver specific private structure
 *
 * Send virtchl get capabilities message. Returns 0 on success, negative on
 * failure.
 */
int iecm_send_get_caps_msg(struct iecm_adapter *adapter)
{
	struct virtchnl2_get_capabilities caps = {0};
	int buf_size;

	buf_size = sizeof(struct virtchnl2_get_capabilities);
	adapter->caps = kzalloc(buf_size, GFP_KERNEL);
	if (!adapter->caps)
		return -ENOMEM;

	caps.csum_caps =
		cpu_to_le32(VIRTCHNL2_CAP_TX_CSUM_L3_IPV4	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_TCP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_UDP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV4_SCTP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_TCP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_UDP	|
			    VIRTCHNL2_CAP_TX_CSUM_L4_IPV6_SCTP	|
			    VIRTCHNL2_CAP_TX_CSUM_GENERIC	|
			    VIRTCHNL2_CAP_RX_CSUM_L3_IPV4	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_TCP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_UDP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV4_SCTP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_TCP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_UDP	|
			    VIRTCHNL2_CAP_RX_CSUM_L4_IPV6_SCTP	|
			    VIRTCHNL2_CAP_RX_CSUM_GENERIC);

	caps.seg_caps =
		cpu_to_le32(VIRTCHNL2_CAP_SEG_IPV4_TCP		|
			    VIRTCHNL2_CAP_SEG_IPV4_UDP		|
			    VIRTCHNL2_CAP_SEG_IPV4_SCTP		|
			    VIRTCHNL2_CAP_SEG_IPV6_TCP		|
			    VIRTCHNL2_CAP_SEG_IPV6_UDP		|
			    VIRTCHNL2_CAP_SEG_IPV6_SCTP		|
			    VIRTCHNL2_CAP_SEG_GENERIC);

	caps.rss_caps =
		cpu_to_le64(VIRTCHNL2_CAP_RSS_IPV4_TCP		|
			    VIRTCHNL2_CAP_RSS_IPV4_UDP		|
			    VIRTCHNL2_CAP_RSS_IPV4_SCTP		|
			    VIRTCHNL2_CAP_RSS_IPV4_OTHER	|
			    VIRTCHNL2_CAP_RSS_IPV6_TCP		|
			    VIRTCHNL2_CAP_RSS_IPV6_UDP		|
			    VIRTCHNL2_CAP_RSS_IPV6_SCTP		|
			    VIRTCHNL2_CAP_RSS_IPV6_OTHER	|
			    VIRTCHNL2_CAP_RSS_IPV4_AH		|
			    VIRTCHNL2_CAP_RSS_IPV4_ESP		|
			    VIRTCHNL2_CAP_RSS_IPV4_AH_ESP	|
			    VIRTCHNL2_CAP_RSS_IPV6_AH		|
			    VIRTCHNL2_CAP_RSS_IPV6_ESP		|
			    VIRTCHNL2_CAP_RSS_IPV6_AH_ESP);

	caps.hsplit_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RX_HSPLIT_AT_L2	|
			    VIRTCHNL2_CAP_RX_HSPLIT_AT_L3	|
			    VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V4	|
			    VIRTCHNL2_CAP_RX_HSPLIT_AT_L4V6);

	caps.rsc_caps =
		cpu_to_le32(VIRTCHNL2_CAP_RSC_IPV4_TCP		|
			    VIRTCHNL2_CAP_RSC_IPV4_SCTP		|
			    VIRTCHNL2_CAP_RSC_IPV6_TCP		|
			    VIRTCHNL2_CAP_RSC_IPV6_SCTP);

	caps.other_caps =
		cpu_to_le64(VIRTCHNL2_CAP_RDMA			|
			    VIRTCHNL2_CAP_SRIOV			|
			    VIRTCHNL2_CAP_MACFILTER		|
			    VIRTCHNL2_CAP_FLOW_DIRECTOR		|
			    VIRTCHNL2_CAP_SPLITQ_QSCHED		|
			    VIRTCHNL2_CAP_CRC			|
			    VIRTCHNL2_CAP_ADQ			|
			    VIRTCHNL2_CAP_WB_ON_ITR		|
			    VIRTCHNL2_CAP_PROMISC		|
			    VIRTCHNL2_CAP_INLINE_IPSEC		|
			    VIRTCHNL2_CAP_VLAN			|
			    VIRTCHNL2_CAP_RX_FLEX_DESC);

	return iecm_send_mb_msg(adapter, VIRTCHNL2_OP_GET_CAPS, sizeof(caps),
				(u8 *)&caps);
}
EXPORT_SYMBOL(iecm_send_get_caps_msg);

/**
 * iecm_recv_get_caps_msg - Receive virtchnl get capabilities message
 * @adapter: Driver specific private structure
 *
 * Receive virtchnl get capabilities message.  Returns 0 on succes, negative on
 * failure.
 */
static int iecm_recv_get_caps_msg(struct iecm_adapter *adapter)
{
	return iecm_recv_mb_msg(adapter, VIRTCHNL2_OP_GET_CAPS, adapter->caps,
				sizeof(struct virtchnl2_get_capabilities));
}

/**
 * iecm_vport_init_max_qs - Initialize max queues supported on this device
 * @adapter: Driver specific private structure
 */
static void iecm_vport_init_max_qs(struct iecm_adapter *adapter)
{
	adapter->max_queue_limit = IECM_MAX_Q;
}

/**
 * iecm_get_reg_intr_vecs - Get vector queue register offset
 * @vport: virtual port structure
 * @reg_vals: Register offsets to store in
 * @num_vecs: Number of vector registers
 *
 * Returns number of regsiters that got populated
 */
int iecm_get_reg_intr_vecs(struct iecm_vport *vport,
			   struct iecm_vec_regs *reg_vals, int num_vecs)
{
	struct virtchnl2_vector_chunks *chunks;
	struct iecm_vec_regs reg_val;
	u16 num_vchunks, num_vec;
	int num_regs = 0, i, j;

	chunks = &vport->adapter->req_vec_chunks->vchunks;
	num_vchunks = le16_to_cpu(chunks->num_vchunks);

	for (j = 0; j < num_vchunks; j++) {
		struct virtchnl2_vector_chunk *chunk = &chunks->vchunks[j];

		num_vec = le16_to_cpu(chunk->num_vectors);
		reg_val.dyn_ctl_reg = le32_to_cpu(chunk->dynctl_reg_start);
		reg_val.itrn_reg = le32_to_cpu(chunk->itrn_reg_start);
		for (i = 0; i < num_vec; i++) {
			if (num_regs == num_vecs)
				break;
			reg_vals[i].dyn_ctl_reg = reg_val.dyn_ctl_reg;
			reg_vals[i].itrn_reg = reg_val.itrn_reg;
			reg_val.dyn_ctl_reg +=
				le32_to_cpu(chunk->dynctl_reg_spacing);
			reg_val.itrn_reg +=
				le32_to_cpu(chunk->itrn_reg_spacing);
			num_regs++;
		}
	}

	return num_regs;
}
EXPORT_SYMBOL(iecm_get_reg_intr_vecs);

/**
 * iecm_vport_get_q_reg - Get the queue registers for the vport
 * @reg_vals: register values needing to be set
 * @num_regs: amount we expect to fill
 * @q_type: queue model
 * @chunks: queue regs received over mailbox
 */
static int
iecm_vport_get_q_reg(u32 *reg_vals, int num_regs, u32 q_type,
		     struct virtchnl2_queue_reg_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_chunks);
	int reg_filled = 0, i;
	u32 reg_val;
	u16 num_q;

	while (num_chunks) {
		struct virtchnl2_queue_reg_chunk *chunk = &chunks->chunks[num_chunks - 1];

		if (le32_to_cpu(chunk->type) == q_type) {
			num_q = le32_to_cpu(chunk->num_queues);
			reg_val = le64_to_cpu(chunk->qtail_reg_start);
			for (i = 0; i < num_q; i++) {
				if (reg_filled == num_regs)
					break;
				reg_vals[reg_filled++] = reg_val;
				reg_val +=
					le32_to_cpu(chunk->qtail_reg_spacing);
			}
		}
		num_chunks--;
	}

	return reg_filled;
}

/**
 * __iecm_queue_reg_init - initialize queue registers
 * @vport: virtual port structure
 * @reg_vals: registers we are initializing
 * @num_regs: how many registers there are in total
 * @q_type: queue model
 *
 * Return number of queues that are initialized
 */
static int
__iecm_queue_reg_init(struct iecm_vport *vport, u32 *reg_vals,
		      int num_regs, u32 q_type)
{
	struct iecm_hw *hw = &vport->adapter->hw;
	struct iecm_queue *q;
	int i, j, k = 0;

	switch (q_type) {
	case VIRTCHNL2_QUEUE_TYPE_TX:
		for (i = 0; i < vport->num_txq_grp; i++) {
			struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];

			for (j = 0; j < tx_qgrp->num_txq; j++) {
				if (k == num_regs)
					break;

				tx_qgrp->txqs[j]->tail =
				  (__force u8 __iomem *)(hw->hw_addr +
							 reg_vals[k]);
				k++;
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			int num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq; j++) {
				if (k == num_regs)
					break;

				q = rx_qgrp->singleq.rxqs[j];
				q->tail = (__force u8 __iomem *)(hw->hw_addr +
								 reg_vals[k]);
				k++;
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];

			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				if (k == num_regs)
					break;

				q = &rx_qgrp->splitq.bufq_sets[j].bufq;
				q->tail = (__force u8 __iomem *)(hw->hw_addr +
								 reg_vals[k]);
				k++;
			}
		}
		break;
	default:
		break;
	}

	return k;
}

/**
 * iecm_queue_reg_init - initialize queue registers
 * @vport: virtual port structure
 *
 * Return 0 on success, negative on failure
 */
static int iecm_queue_reg_init(struct iecm_vport *vport)
{
	struct virtchnl2_create_vport *vport_params;
	struct virtchnl2_queue_reg_chunks *chunks;
	int num_regs, ret = 0;
	u32 *reg_vals;

	/* We may never deal with more than 256 same type of queues */
	reg_vals = kmalloc(sizeof(void *) * IECM_LARGE_MAX_Q,
			   GFP_KERNEL);
	if (!reg_vals)
		return -ENOMEM;

	if (vport->adapter->config_data.req_qs_chunks) {
		struct virtchnl2_add_queues *vc_aq =
		  (struct virtchnl2_add_queues *)
		  vport->adapter->config_data.req_qs_chunks;
		chunks = &vc_aq->chunks;
	} else {
		vport_params = (struct virtchnl2_create_vport *)
			vport->adapter->vport_params_recvd[0];
		chunks = &vport_params->chunks;
	}

	/* Initialize Tx queue tail register address */
	num_regs = iecm_vport_get_q_reg(reg_vals, IECM_LARGE_MAX_Q,
					VIRTCHNL2_QUEUE_TYPE_TX,
					chunks);
	if (num_regs < vport->num_txq) {
		ret = -EINVAL;
		goto free_reg_vals;
	}

	num_regs = __iecm_queue_reg_init(vport, reg_vals, num_regs,
					 VIRTCHNL2_QUEUE_TYPE_TX);
	if (num_regs < vport->num_txq) {
		ret = -EINVAL;
		goto free_reg_vals;
	}

	/* Initialize Rx/buffer queue tail register address based on Rx queue
	 * model
	 */
	if (iecm_is_queue_model_split(vport->rxq_model)) {
		num_regs = iecm_vport_get_q_reg(reg_vals, IECM_LARGE_MAX_Q,
						VIRTCHNL2_QUEUE_TYPE_RX_BUFFER,
						chunks);
		if (num_regs < vport->num_bufq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}

		num_regs = __iecm_queue_reg_init(vport, reg_vals, num_regs,
						 VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);
		if (num_regs < vport->num_bufq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}
	} else {
		num_regs = iecm_vport_get_q_reg(reg_vals, IECM_LARGE_MAX_Q,
						VIRTCHNL2_QUEUE_TYPE_RX,
						chunks);
		if (num_regs < vport->num_rxq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}

		num_regs = __iecm_queue_reg_init(vport, reg_vals, num_regs,
						 VIRTCHNL2_QUEUE_TYPE_RX);
		if (num_regs < vport->num_rxq) {
			ret = -EINVAL;
			goto free_reg_vals;
		}
	}

free_reg_vals:
	kfree(reg_vals);
	return ret;
}

/**
 * iecm_send_create_vport_msg - Send virtchnl create vport message
 * @adapter: Driver specific private structure
 *
 * send virtchnl creae vport message
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_send_create_vport_msg(struct iecm_adapter *adapter)
{
	struct virtchnl2_create_vport *vport_msg;
	int buf_size;

	buf_size = sizeof(struct virtchnl2_create_vport);
	if (!adapter->vport_params_reqd[0]) {
		adapter->vport_params_reqd[0] = kzalloc(buf_size, GFP_KERNEL);
		if (!adapter->vport_params_reqd[0])
			return -ENOMEM;
	}

	vport_msg = (struct virtchnl2_create_vport *)
			adapter->vport_params_reqd[0];
	vport_msg->vport_type = cpu_to_le16(VIRTCHNL2_VPORT_TYPE_DEFAULT);

	if (test_bit(__IECM_REQ_TX_SPLITQ, adapter->flags))
		vport_msg->txq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SPLIT);
	else
		vport_msg->txq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SINGLE);

	if (test_bit(__IECM_REQ_RX_SPLITQ, adapter->flags))
		vport_msg->rxq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SPLIT);
	else
		vport_msg->rxq_model = cpu_to_le16(VIRTCHNL2_QUEUE_MODEL_SINGLE);

	adapter->dev_ops.vc_ops.init_max_queues(adapter);

	iecm_vport_calc_total_qs(adapter, vport_msg);

	return iecm_send_mb_msg(adapter, VIRTCHNL2_OP_CREATE_VPORT, buf_size,
				(u8 *)vport_msg);
}

/**
 * iecm_check_descs - Verify we have the descriptor support required
 * @vport: virtual port structure
 * @rx_desc_ids: Rx descriptor ids to check
 * @tx_desc_ids: Tx descriptor ids to check
 * @rxq_model: Rx queue model
 * @txq_model: Tx queue model
 *
 * Returns 0 on success, negative if we didn't get sufficient descriptor
 * support.
 */
int iecm_check_descs(struct iecm_vport *vport, u64 rx_desc_ids,
		     u64 tx_desc_ids, u16 rxq_model, u16 txq_model)
{
	struct iecm_adapter *adapter = vport->adapter;

	if (rxq_model == VIRTCHNL2_QUEUE_MODEL_SPLIT) {
		if (!(rx_desc_ids & VIRTCHNL2_RXDID_1_FLEX_SPLITQ_M)) {
			dev_err(&adapter->pdev->dev, "No supported RX descriptors provided");
			return -EINVAL;
		}
	} else {
		if (!(rx_desc_ids & VIRTCHNL2_RXDID_2_FLEX_SQ_NIC_M))
			vport->base_rxd = true;
	}

	if (txq_model == VIRTCHNL2_QUEUE_MODEL_SPLIT) {
#define MIN_SUPPORT_TXDID (\
		VIRTCHNL2_TXDID_FLEX_FLOW_SCHED |\
		VIRTCHNL2_TXDID_FLEX_TSO_CTX |\
		VIRTCHNL2_TXDID_FLEX_DATA)
		if ((tx_desc_ids & MIN_SUPPORT_TXDID) != MIN_SUPPORT_TXDID) {
			dev_err(&adapter->pdev->dev, "Minimum TX descriptor support not provided");
			return -EINVAL;
		}
	}
	return 0;
}
EXPORT_SYMBOL(iecm_check_descs);

/**
 * iecm_get_supported_desc_ids - Get supported Rx and Tx descriptor ids
 * @vport: virtual port structure
 *
 * Return 0 on success, error on failure
 */
static int iecm_get_supported_desc_ids(struct iecm_vport *vport)
{
	struct virtchnl2_create_vport *vport_msg;

	vport_msg = (struct virtchnl2_create_vport *)
			vport->adapter->vport_params_recvd[0];
	vport_msg->rx_desc_ids = cpu_to_le64(VIRTCHNL2_RXDID_1_FLEX_SPLITQ_M);
	vport_msg->tx_desc_ids = cpu_to_le64(MIN_SUPPORT_TXDID);

	return iecm_check_descs(vport, le64_to_cpu(vport_msg->rx_desc_ids),
				le64_to_cpu(vport_msg->tx_desc_ids),
				vport->rxq_model, vport->txq_model);
}

/**
 * iecm_recv_create_vport_msg - Receive virtchnl create vport message
 * @adapter: Driver specific private structure
 * @vport_id: Virtual port identifier
 *
 * Receive virtchnl create vport message.  Returns 0 on success, negative on
 * failure.
 */
static int iecm_recv_create_vport_msg(struct iecm_adapter *adapter,
				      int *vport_id)
{
	struct virtchnl2_create_vport *vport_msg;
	int err;

	if (!adapter->vport_params_recvd[0]) {
		adapter->vport_params_recvd[0] = kzalloc(IECM_DFLT_MBX_BUF_SIZE,
							 GFP_KERNEL);
		if (!adapter->vport_params_recvd[0])
			return -ENOMEM;
	}

	vport_msg = (struct virtchnl2_create_vport *)
			adapter->vport_params_recvd[0];

	err = iecm_recv_mb_msg(adapter, VIRTCHNL2_OP_CREATE_VPORT, vport_msg,
			       IECM_DFLT_MBX_BUF_SIZE);
	if (err)
		return err;

	*vport_id = le32_to_cpu(vport_msg->vport_id);
	return 0;
}

/**
 * __iecm_wait_for_event - wrapper function for wait on virtchannel response
 * @adapter: Driver private data structure
 * @state: check on state upon timeout
 * @err_check: check if this specific error bit is set
 * @timeout: Max time to wait
 *
 * Checks if state is set upon expiry of timeout.  Returns 0 on success,
 * negative on failure.
 */
static int __iecm_wait_for_event(struct iecm_adapter *adapter,
				 enum iecm_vport_vc_state state,
				 enum iecm_vport_vc_state err_check,
				 int timeout)
{
	int event;

	event = wait_event_timeout(adapter->vchnl_wq,
				   test_and_clear_bit(state, adapter->vc_state),
				   msecs_to_jiffies(timeout));
	if (event) {
		if (test_and_clear_bit(err_check, adapter->vc_state)) {
			dev_err(&adapter->pdev->dev, "VC response error %s\n",
				iecm_vport_vc_state_str[err_check]);
			return -EINVAL;
		}
		return 0;
	}

	/* Timeout occurred */
	dev_err(&adapter->pdev->dev, "VC timeout, state = %s\n",
		iecm_vport_vc_state_str[state]);
	return -ETIMEDOUT;
}

/**
 * iecm_min_wait_for_event - wait for virtchannel response
 * @adapter: Driver private data structure
 * @state: check on state upon timeout
 * @err_check: check if this specific error bit is set
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_min_wait_for_event(struct iecm_adapter *adapter,
			    enum iecm_vport_vc_state state,
			    enum iecm_vport_vc_state err_check)
{
	int timeout = 2000;

	return __iecm_wait_for_event(adapter, state, err_check, timeout);
}
EXPORT_SYMBOL(iecm_min_wait_for_event);

/**
 * iecm_wait_for_event - wait for virtchannel response
 * @adapter: Driver private data structure
 * @state: check on state upon timeout after 500ms
 * @err_check: check if this specific error bit is set
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_wait_for_event(struct iecm_adapter *adapter,
			enum iecm_vport_vc_state state,
			enum iecm_vport_vc_state err_check)
{
	/* Increasing the timeout in __IECM_INIT_SW flow to consider large
	 * number of VF's mailbox message responses. When a message is received
	 * on mailbox, this thread is wake up by the iecm_recv_mb_msg before the
	 * timeout expires. Only in the error case i.e. if no message is
	 * received on mailbox, we wait for the complete timeout which is
	 * less likely to happen.
	 */
	int timeout = 60000;

	return __iecm_wait_for_event(adapter, state, err_check, timeout);
}
EXPORT_SYMBOL(iecm_wait_for_event);

/**
 * iecm_wait_for_marker_event - wait for software marker response
 * @vport: virtual port data structure
 *
 * Returns 0 success, negative on failure.
 **/
static int iecm_wait_for_marker_event(struct iecm_vport *vport)
{
	int event = 0;
	int i;

	for (i = 0; i < vport->num_txq; i++)
		set_bit(__IECM_Q_SW_MARKER, vport->txqs[i]->flags);

	event = wait_event_timeout(vport->adapter->sw_marker_wq,
				   test_and_clear_bit(__IECM_SW_MARKER,
						      vport->adapter->flags),
				   msecs_to_jiffies(500));
	if (event)
		return 0;
	return -ETIMEDOUT;
}

/**
 * iecm_send_destroy_vport_msg - Send virtchnl destroy vport message
 * @vport: virtual port data structure
 *
 * Send virtchnl destroy vport message.  Returns 0 on success, negative on
 * failure.
 */
int iecm_send_destroy_vport_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_vport v_id;
	int err;

	v_id.vport_id = cpu_to_le32(vport->vport_id);

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_DESTROY_VPORT,
			       sizeof(v_id), (u8 *)&v_id);
	if (err)
		return err;

	return iecm_min_wait_for_event(adapter, IECM_VC_DESTROY_VPORT,
				       IECM_VC_DESTROY_VPORT_ERR);
}

/**
 * iecm_send_enable_vport_msg - Send virtchnl enable vport message
 * @vport: virtual port data structure
 *
 * Send enable vport virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int iecm_send_enable_vport_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_vport v_id;
	int err;

	v_id.vport_id = cpu_to_le32(vport->vport_id);

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_ENABLE_VPORT,
			       sizeof(v_id), (u8 *)&v_id);
	if (err)
		return err;

	return iecm_wait_for_event(adapter, IECM_VC_ENA_VPORT,
				   IECM_VC_ENA_VPORT_ERR);
}

/**
 * iecm_send_disable_vport_msg - Send virtchnl disable vport message
 * @vport: virtual port data structure
 *
 * Send disable vport virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int iecm_send_disable_vport_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_vport v_id;
	int err;

	v_id.vport_id = cpu_to_le32(vport->vport_id);

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_DISABLE_VPORT,
			       sizeof(v_id), (u8 *)&v_id);
	if (err)
		return err;

	return iecm_min_wait_for_event(adapter, IECM_VC_DIS_VPORT,
				       IECM_VC_DIS_VPORT_ERR);
}

/**
 * iecm_send_config_tx_queues_msg - Send virtchnl config tx queues message
 * @vport: virtual port data structure
 *
 * Send config tx queues virtchnl message. Returns 0 on success, negative on
 * failure.
 */
int iecm_send_config_tx_queues_msg(struct iecm_vport *vport)
{
	struct virtchnl2_config_tx_queues *ctq = NULL;
	int config_data_size, chunk_size, buf_size = 0;
	int totqs, num_msgs, num_chunks;
	struct virtchnl2_txq_info *qi;
	int err = 0, i, k = 0;
	bool alloc = false;

	totqs = vport->num_txq + vport->num_complq;
	qi = kcalloc(totqs, sizeof(struct virtchnl2_txq_info), GFP_KERNEL);
	if (!qi)
		return -ENOMEM;

	/* Populate the queue info buffer with all queue context info */
	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];
		int j;

		for (j = 0; j < tx_qgrp->num_txq; j++, k++) {
			qi[k].queue_id =
				cpu_to_le32(tx_qgrp->txqs[j]->q_id);
			qi[k].model =
				cpu_to_le16(vport->txq_model);
			qi[k].type =
				cpu_to_le32(tx_qgrp->txqs[j]->q_type);
			qi[k].ring_len =
				cpu_to_le16(tx_qgrp->txqs[j]->desc_count);
			qi[k].dma_ring_addr =
				cpu_to_le64(tx_qgrp->txqs[j]->dma);
			if (iecm_is_queue_model_split(vport->txq_model)) {
				struct iecm_queue *q = tx_qgrp->txqs[j];

				qi[k].tx_compl_queue_id =
					cpu_to_le16(tx_qgrp->complq->q_id);

				if (test_bit(__IECM_Q_FLOW_SCH_EN, q->flags))
					qi[k].sched_mode =
					cpu_to_le16(VIRTCHNL2_TXQ_SCHED_MODE_FLOW);
				else
					qi[k].sched_mode =
					cpu_to_le16(VIRTCHNL2_TXQ_SCHED_MODE_QUEUE);
			} else {
				qi[k].sched_mode =
					cpu_to_le16(VIRTCHNL2_TXQ_SCHED_MODE_QUEUE);
			}
		}

		if (iecm_is_queue_model_split(vport->txq_model)) {
			qi[k].queue_id =
				cpu_to_le32(tx_qgrp->complq->q_id);
			qi[k].model =
				cpu_to_le16(vport->txq_model);
			qi[k].type =
				cpu_to_le32(tx_qgrp->complq->q_type);
			qi[k].ring_len =
				cpu_to_le16(tx_qgrp->complq->desc_count);
			qi[k].dma_ring_addr =
				cpu_to_le64(tx_qgrp->complq->dma);
			k++;
		}
	}

	/* Make sure accounting agrees */
	if (k != totqs) {
		err = -EINVAL;
		goto error;
	}

	/* Chunk up the queue contexts into multiple messages to avoid
	 * sending a control queue message buffer that is too large
	 */
	config_data_size = sizeof(struct virtchnl2_config_tx_queues);
	chunk_size = sizeof(struct virtchnl2_txq_info);

	num_chunks = IECM_NUM_CHUNKS_PER_MSG(config_data_size, chunk_size) + 1;
	if (totqs < num_chunks)
		num_chunks = totqs;

	num_msgs = totqs / num_chunks;
	if (totqs % num_chunks)
		num_msgs++;

	for (i = 0, k = 0; i < num_msgs; i++) {
		if (!ctq || alloc) {
			buf_size = (chunk_size * (num_chunks - 1)) +
					config_data_size;
			kfree(ctq);
			ctq = kzalloc(buf_size, GFP_KERNEL);
			if (!ctq) {
				err = -ENOMEM;
				goto error;
			}
		} else {
			memset(ctq, 0, buf_size);
		}

		ctq->vport_id = cpu_to_le32(vport->vport_id);
		ctq->num_qinfo = cpu_to_le16(num_chunks);
		memcpy(ctq->qinfo, &qi[k], chunk_size * num_chunks);

		err = iecm_send_mb_msg(vport->adapter,
				       VIRTCHNL2_OP_CONFIG_TX_QUEUES,
				       buf_size, (u8 *)ctq);
		if (err)
			goto mbx_error;

		err = iecm_wait_for_event(vport->adapter, IECM_VC_CONFIG_TXQ,
					  IECM_VC_CONFIG_TXQ_ERR);
		if (err)
			goto mbx_error;

		k += num_chunks;
		totqs -= num_chunks;
		if (totqs < num_chunks) {
			num_chunks = totqs;
			alloc = true;
		}
	}

mbx_error:
	kfree(ctq);
error:
	kfree(qi);
	return err;
}

/**
 * iecm_send_config_rx_queues_msg - Send virtchnl config rx queues message
 * @vport: virtual port data structure
 *
 * Send config rx queues virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
int iecm_send_config_rx_queues_msg(struct iecm_vport *vport)
{
	struct virtchnl2_config_rx_queues *crq = NULL;
	int config_data_size, chunk_size, buf_size = 0;
	int totqs, num_msgs, num_chunks;
	struct virtchnl2_rxq_info *qi;
	int err = 0, i, k = 0;
	bool alloc = false;

	totqs = vport->num_rxq + vport->num_bufq;
	qi = kcalloc(totqs, sizeof(struct virtchnl2_rxq_info), GFP_KERNEL);
	if (!qi)
		return -ENOMEM;

	/* Populate the queue info buffer with all queue context info */
	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		int num_rxq;
		int j;

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++, k++) {
				struct iecm_queue *bufq =
					&rx_qgrp->splitq.bufq_sets[j].bufq;

				qi[k].queue_id =
					cpu_to_le32(bufq->q_id);
				qi[k].model =
					cpu_to_le16(vport->rxq_model);
				qi[k].type =
					cpu_to_le32(bufq->q_type);
				qi[k].desc_ids =
					cpu_to_le64(VIRTCHNL2_RXDID_1_FLEX_SPLITQ_M);
				qi[k].ring_len =
					cpu_to_le16(bufq->desc_count);
				qi[k].dma_ring_addr =
					cpu_to_le64(bufq->dma);
				qi[k].data_buffer_size =
					cpu_to_le32(bufq->rx_buf_size);
				qi[k].buffer_notif_stride =
					bufq->rx_buf_stride;
				qi[k].rx_buffer_low_watermark =
					cpu_to_le16(bufq->rx_buffer_low_watermark);
				if (iecm_is_feature_ena(vport, NETIF_F_GRO_HW))
					qi[k].qflags |=
						cpu_to_le16(VIRTCHNL2_RXQ_RSC);
			}
		}

		if (iecm_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++, k++) {
			struct iecm_queue *rxq;

			if (iecm_is_queue_model_split(vport->rxq_model)) {
				rxq = &rx_qgrp->splitq.rxq_sets[j]->rxq;
				qi[k].rx_bufq1_id =
				  cpu_to_le16(rxq->rxq_grp->splitq.bufq_sets[0].bufq.q_id);
				qi[k].rx_bufq2_id =
				  cpu_to_le16(rxq->rxq_grp->splitq.bufq_sets[1].bufq.q_id);
				qi[k].hdr_buffer_size =
					cpu_to_le16(rxq->rx_hbuf_size);
				qi[k].rx_buffer_low_watermark =
					cpu_to_le16(rxq->rx_buffer_low_watermark);

				if (rxq->rx_hsplit_en) {
					qi[k].qflags =
						cpu_to_le16(VIRTCHNL2_RXQ_HDR_SPLIT);
					qi[k].hdr_buffer_size =
						cpu_to_le16(rxq->rx_hbuf_size);
				}
				if (iecm_is_feature_ena(vport, NETIF_F_GRO_HW))
					qi[k].qflags |=
						cpu_to_le16(VIRTCHNL2_RXQ_RSC);
			} else {
				rxq = rx_qgrp->singleq.rxqs[j];
			}

			qi[k].queue_id =
				cpu_to_le32(rxq->q_id);
			qi[k].model =
				cpu_to_le16(vport->rxq_model);
			qi[k].type =
				cpu_to_le32(rxq->q_type);
			qi[k].ring_len =
				cpu_to_le16(rxq->desc_count);
			qi[k].dma_ring_addr =
				cpu_to_le64(rxq->dma);
			qi[k].max_pkt_size =
				cpu_to_le32(rxq->rx_max_pkt_size);
			qi[k].data_buffer_size =
				cpu_to_le32(rxq->rx_buf_size);
			qi[k].qflags |=
				cpu_to_le16(VIRTCHNL2_RX_DESC_SIZE_32BYTE);
			qi[k].desc_ids =
				cpu_to_le64(rxq->rxdids);
		}
	}

	/* Make sure accounting agrees */
	if (k != totqs) {
		err = -EINVAL;
		goto error;
	}

	/* Chunk up the queue contexts into multiple messages to avoid
	 * sending a control queue message buffer that is too large
	 */
	config_data_size = sizeof(struct virtchnl2_config_rx_queues);
	chunk_size = sizeof(struct virtchnl2_rxq_info);

	num_chunks = IECM_NUM_CHUNKS_PER_MSG(config_data_size, chunk_size) + 1;
	if (totqs < num_chunks)
		num_chunks = totqs;

	num_msgs = totqs / num_chunks;
	if (totqs % num_chunks)
		num_msgs++;

	for (i = 0, k = 0; i < num_msgs; i++) {
		if (!crq || alloc) {
			buf_size = (chunk_size * (num_chunks - 1)) +
					config_data_size;
			kfree(crq);
			crq = kzalloc(buf_size, GFP_KERNEL);
			if (!crq) {
				err = -ENOMEM;
				goto error;
			}
		} else {
			memset(crq, 0, buf_size);
		}

		crq->vport_id = cpu_to_le32(vport->vport_id);
		crq->num_qinfo = cpu_to_le16(num_chunks);
		memcpy(crq->qinfo, &qi[k], chunk_size * num_chunks);

		err = iecm_send_mb_msg(vport->adapter,
				       VIRTCHNL2_OP_CONFIG_RX_QUEUES,
				       buf_size, (u8 *)crq);
		if (err)
			goto mbx_error;

		err = iecm_wait_for_event(vport->adapter, IECM_VC_CONFIG_RXQ,
					  IECM_VC_CONFIG_RXQ_ERR);
		if (err)
			goto mbx_error;

		k += num_chunks;
		totqs -= num_chunks;
		if (totqs < num_chunks) {
			num_chunks = totqs;
			alloc = true;
		}
	}

mbx_error:
	kfree(crq);
error:
	kfree(qi);
	return err;
}

/**
 * iecm_send_ena_dis_queues_msg - Send virtchnl enable or disable
 * queues message
 * @vport: virtual port data structure
 * @vc_op: virtchnl op code to send
 *
 * Send enable or disable queues virtchnl message. Returns 0 on success,
 * negative on failure.
 */
static int iecm_send_ena_dis_queues_msg(struct iecm_vport *vport,
					enum virtchnl_ops vc_op)
{
	int num_msgs, num_chunks, config_data_size, chunk_size;
	int num_txq, num_rxq, num_q, buf_size, err = 0;
	struct virtchnl2_del_ena_dis_queues *eq = NULL;
	struct virtchnl2_queue_chunk *qc;
	bool alloc = false;
	int i, j, k = 0;

	/* validate virtchnl op */
	switch (vc_op) {
	case VIRTCHNL2_OP_ENABLE_QUEUES:
	case VIRTCHNL2_OP_DISABLE_QUEUES:
		break;
	default:
		return -EINVAL;
	}

	num_txq = vport->num_txq + vport->num_complq;
	num_rxq = vport->num_rxq + vport->num_bufq;
	num_q = num_txq + num_rxq;
	buf_size = sizeof(struct virtchnl2_queue_chunk) * (num_q);
	qc = kzalloc(buf_size, GFP_KERNEL);
	if (!qc)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];

		for (j = 0; j < tx_qgrp->num_txq; j++, k++) {
			qc[k].type = cpu_to_le32(tx_qgrp->txqs[j]->q_type);
			qc[k].start_queue_id =
					cpu_to_le32(tx_qgrp->txqs[j]->q_id);
			qc[k].num_queues = cpu_to_le32(1);
		}
	}
	if (vport->num_txq != k) {
		err = -EINVAL;
		goto error;
	}

	if (iecm_is_queue_model_split(vport->txq_model)) {
		for (i = 0; i < vport->num_txq_grp; i++, k++) {
			struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];

			qc[k].type = cpu_to_le32(tx_qgrp->complq->q_type);
			qc[k].start_queue_id =
					cpu_to_le32(tx_qgrp->complq->q_id);
			qc[k].num_queues = cpu_to_le32(1);
		}
		if (vport->num_complq != (k - vport->num_txq)) {
			err = -EINVAL;
			goto error;
		}
	}

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];

		if (iecm_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++, k++) {
			if (iecm_is_queue_model_split(vport->rxq_model)) {
				qc[k].start_queue_id =
				cpu_to_le32(rx_qgrp->splitq.rxq_sets[j]->rxq.q_id);
				qc[k].type =
				cpu_to_le32(rx_qgrp->splitq.rxq_sets[j]->rxq.q_type);
			} else {
				qc[k].start_queue_id =
				cpu_to_le32(rx_qgrp->singleq.rxqs[j]->q_id);
				qc[k].type =
				cpu_to_le32(rx_qgrp->singleq.rxqs[j]->q_type);
			}
			qc[k].num_queues = cpu_to_le32(1);
		}
	}
	if (vport->num_rxq != k - (vport->num_txq + vport->num_complq)) {
		err = -EINVAL;
		goto error;
	}

	if (iecm_is_queue_model_split(vport->rxq_model)) {
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];

			for (j = 0; j < vport->num_bufqs_per_qgrp; j++, k++) {
				struct iecm_queue *q = &rx_qgrp->splitq.bufq_sets[j].bufq;

				qc[k].type = cpu_to_le32(q->q_type);
				qc[k].start_queue_id = cpu_to_le32(q->q_id);
				qc[k].num_queues = cpu_to_le32(1);
			}
		}
		if (vport->num_bufq != k - (vport->num_txq +
					       vport->num_complq +
					       vport->num_rxq)) {
			err = -EINVAL;
			goto error;
		}
	}

	/* Chunk up the queue info into multiple messages */
	config_data_size = sizeof(struct virtchnl2_del_ena_dis_queues);
	chunk_size = sizeof(struct virtchnl2_queue_chunk);

	num_chunks = IECM_NUM_CHUNKS_PER_MSG(config_data_size, chunk_size) + 1;
	if (num_q < num_chunks)
		num_chunks = num_q;

	num_msgs = num_q / num_chunks;
	if (num_q % num_chunks)
		num_msgs++;

	for (i = 0, k = 0; i < num_msgs; i++) {
		if (!eq || alloc) {
			buf_size = (chunk_size * (num_chunks - 1)) +
					config_data_size;
			kfree(eq);
			eq = kzalloc(buf_size, GFP_KERNEL);
			if (!eq) {
				err = -ENOMEM;
				goto error;
			}
		} else {
			memset(eq, 0, buf_size);
		}
		eq->vport_id = cpu_to_le32(vport->vport_id);
		eq->chunks.num_chunks = cpu_to_le16(num_chunks);
		memcpy(eq->chunks.chunks, &qc[k], chunk_size * num_chunks);

		err = iecm_send_mb_msg(vport->adapter, vc_op, buf_size,
				       (u8 *)eq);
		if (err)
			goto mbx_error;
		k += num_chunks;
		num_q -= num_chunks;
		if (num_q < num_chunks) {
			num_chunks = num_q;
			alloc = true;
		}
	}
mbx_error:
	kfree(eq);
error:
	kfree(qc);
	return err;
}

/**
 * iecm_send_map_unmap_queue_vector_msg - Send virtchnl map or unmap queue
 * vector message
 * @vport: virtual port data structure
 * @map: true for map and false for unmap
 *
 * Send map or unmap queue vector virtchnl message.  Returns 0 on success,
 * negative on failure.
 */
int iecm_send_map_unmap_queue_vector_msg(struct iecm_vport *vport, bool map)
{
	int num_msgs, num_chunks, config_data_size, chunk_size;
	struct virtchnl2_queue_vector_maps *vqvm = NULL;
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_queue_vector *vqv;
	int buf_size, num_q, err = 0;
	bool alloc = false;
	int i, j, k = 0;

	num_q = vport->num_txq + vport->num_rxq;

	buf_size = sizeof(struct virtchnl2_queue_vector) * num_q;
	vqv = kzalloc(buf_size, GFP_KERNEL);
	if (!vqv)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];

		for (j = 0; j < tx_qgrp->num_txq; j++, k++) {
			vqv[k].queue_type = cpu_to_le32(tx_qgrp->txqs[j]->q_type);
			vqv[k].queue_id = cpu_to_le32(tx_qgrp->txqs[j]->q_id);

			if (iecm_is_queue_model_split(vport->txq_model)) {
				vqv[k].vector_id =
				cpu_to_le16(tx_qgrp->complq->q_vector->v_idx);
				vqv[k].itr_idx =
				cpu_to_le32(tx_qgrp->complq->q_vector->tx_itr_idx);
			} else {
				vqv[k].vector_id =
				cpu_to_le16(tx_qgrp->txqs[j]->q_vector->v_idx);
				vqv[k].itr_idx =
				cpu_to_le32(tx_qgrp->txqs[j]->q_vector->tx_itr_idx);
			}
		}
	}

	if (vport->num_txq != k) {
		err = -EINVAL;
		goto error;
	}

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		int num_rxq;

		if (iecm_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++, k++) {
			struct iecm_queue *rxq;

			if (iecm_is_queue_model_split(vport->rxq_model))
				rxq = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				rxq = rx_qgrp->singleq.rxqs[j];

			vqv[k].queue_type = cpu_to_le32(rxq->q_type);
			vqv[k].queue_id = cpu_to_le32(rxq->q_id);
			vqv[k].vector_id = cpu_to_le16(rxq->q_vector->v_idx);
			vqv[k].itr_idx = cpu_to_le32(rxq->q_vector->rx_itr_idx);
		}
	}

	if (iecm_is_queue_model_split(vport->txq_model)) {
		if (vport->num_rxq != k - vport->num_complq) {
			err = -EINVAL;
			goto error;
		}
	} else {
		if (vport->num_rxq != k - vport->num_txq) {
			err = -EINVAL;
			goto error;
		}
	}

	/* Chunk up the vector info into multiple messages */
	config_data_size = sizeof(struct virtchnl2_queue_vector_maps);
	chunk_size = sizeof(struct virtchnl2_queue_vector);

	num_chunks = IECM_NUM_CHUNKS_PER_MSG(config_data_size, chunk_size) + 1;
	if (num_q < num_chunks)
		num_chunks = num_q;

	num_msgs = num_q / num_chunks;
	if (num_q % num_chunks)
		num_msgs++;

	for (i = 0, k = 0; i < num_msgs; i++) {
		if (!vqvm || alloc) {
			buf_size = (chunk_size * (num_chunks - 1)) +
					config_data_size;
			kfree(vqvm);
			vqvm = kzalloc(buf_size, GFP_KERNEL);
			if (!vqvm) {
				err = -ENOMEM;
				goto error;
			}
		} else {
			memset(vqvm, 0, buf_size);
		}
		vqvm->vport_id = cpu_to_le32(vport->vport_id);
		vqvm->num_qv_maps = cpu_to_le16(num_chunks);
		memcpy(vqvm->qv_maps, &vqv[k], chunk_size * num_chunks);

		if (map) {
			err = iecm_send_mb_msg(adapter,
					       VIRTCHNL2_OP_MAP_QUEUE_VECTOR,
					       buf_size, (u8 *)vqvm);
			if (!err)
				err = iecm_wait_for_event(adapter,
							  IECM_VC_MAP_IRQ,
							  IECM_VC_MAP_IRQ_ERR);
		} else {
			err = iecm_send_mb_msg(adapter,
					       VIRTCHNL2_OP_UNMAP_QUEUE_VECTOR,
					       buf_size, (u8 *)vqvm);
			if (!err)
				err =
				iecm_min_wait_for_event(adapter,
							IECM_VC_UNMAP_IRQ,
							IECM_VC_UNMAP_IRQ_ERR);
		}
		if (err)
			goto mbx_error;

		k += num_chunks;
		num_q -= num_chunks;
		if (num_q < num_chunks) {
			num_chunks = num_q;
			alloc = true;
		}
	}
mbx_error:
	kfree(vqvm);
error:
	kfree(vqv);
	return err;
}
EXPORT_SYMBOL(iecm_send_map_unmap_queue_vector_msg);

/**
 * iecm_send_enable_queues_msg - send enable queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send enable queues virtchnl message.  Returns 0 on success, negative on
 * failure.
 */
static int iecm_send_enable_queues_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	int err;

	err = iecm_send_ena_dis_queues_msg(vport,
					   VIRTCHNL2_OP_ENABLE_QUEUES);
	if (err)
		return err;

	return iecm_wait_for_event(adapter, IECM_VC_ENA_QUEUES,
				   IECM_VC_ENA_QUEUES_ERR);
}

/**
 * iecm_send_disable_queues_msg - send disable queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send disable queues virtchnl message.  Returns 0 on success, negative
 * on failure.
 */
static int iecm_send_disable_queues_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	int err;

	err = iecm_send_ena_dis_queues_msg(vport,
					   VIRTCHNL2_OP_DISABLE_QUEUES);
	if (err)
		return err;

	err = iecm_min_wait_for_event(adapter, IECM_VC_DIS_QUEUES,
				      IECM_VC_DIS_QUEUES_ERR);
	if (err)
		return err;

	return iecm_wait_for_marker_event(vport);
}

/**
 * iecm_convert_reg_to_queue_chunks - Copy queue chunk information to the right
 * structure
 * @dchunks: Destination chunks to store data to
 * @schunks: Source chunks to copy data from
 * @num_chunks: number of chunks to copy
 */
static void
iecm_convert_reg_to_queue_chunks(struct virtchnl2_queue_chunk *dchunks,
				 struct virtchnl2_queue_reg_chunk *schunks,
				 u16 num_chunks)
{
	u16 i;

	for (i = 0; i < num_chunks; i++) {
		dchunks[i].type = schunks[i].type;
		dchunks[i].start_queue_id = schunks[i].start_queue_id;
		dchunks[i].num_queues = schunks[i].num_queues;
	}
}

/**
 * iecm_send_delete_queues_msg - send delete queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send delete queues virtchnl message. Return 0 on success, negative on
 * failure.
 */
int iecm_send_delete_queues_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_create_vport *vport_params;
	struct virtchnl2_queue_reg_chunks *chunks;
	struct virtchnl2_del_ena_dis_queues *eq;
	int buf_size, err;
	u16 num_chunks;

	if (vport->adapter->config_data.req_qs_chunks) {
		struct virtchnl2_add_queues *vc_aq =
			(struct virtchnl2_add_queues *)
			vport->adapter->config_data.req_qs_chunks;
		chunks = &vc_aq->chunks;
	} else {
		vport_params = (struct virtchnl2_create_vport *)
				vport->adapter->vport_params_recvd[0];
		 chunks = &vport_params->chunks;
	}

	num_chunks = le16_to_cpu(chunks->num_chunks);
	buf_size = sizeof(struct virtchnl2_del_ena_dis_queues) +
			  (sizeof(struct virtchnl2_queue_chunk) *
			  (num_chunks - 1));

	eq = kzalloc(buf_size, GFP_KERNEL);
	if (!eq)
		return -ENOMEM;

	eq->vport_id = cpu_to_le32(vport->vport_id);
	eq->chunks.num_chunks = cpu_to_le16(num_chunks);

	iecm_convert_reg_to_queue_chunks(eq->chunks.chunks, chunks->chunks,
					 num_chunks);

	err = iecm_send_mb_msg(vport->adapter, VIRTCHNL2_OP_DEL_QUEUES,
			       buf_size, (u8 *)eq);
	if (err)
		goto error;

	err = iecm_min_wait_for_event(adapter, IECM_VC_DEL_QUEUES,
				      IECM_VC_DEL_QUEUES_ERR);
error:
	kfree(eq);
	return err;
}

/**
 * iecm_send_config_queues_msg - Send config queues virtchnl message
 * @vport: Virtual port private data structure
 *
 * Will send config queues virtchnl message. Returns 0 on success, negative on
 * failure.
 */
static int iecm_send_config_queues_msg(struct iecm_vport *vport)
{
	int err;

	err = iecm_send_config_tx_queues_msg(vport);
	if (err)
		return err;

	return iecm_send_config_rx_queues_msg(vport);
}

/**
 * iecm_send_add_queues_msg - Send virtchnl add queues message
 * @vport: Virtual port private data structure
 * @num_tx_q: number of transmit queues
 * @num_complq: number of transmit completion queues
 * @num_rx_q: number of receive queues
 * @num_rx_bufq: number of receive buffer queues
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_add_queues_msg(struct iecm_vport *vport, u16 num_tx_q,
			     u16 num_complq, u16 num_rx_q, u16 num_rx_bufq)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_add_queues aq = {0};
	struct virtchnl2_add_queues *vc_msg;
	int size, err;

	vc_msg = (struct virtchnl2_add_queues *)adapter->vc_msg;

	aq.vport_id = cpu_to_le32(vport->vport_id);
	aq.num_tx_q = cpu_to_le16(num_tx_q);
	aq.num_tx_complq = cpu_to_le16(num_complq);
	aq.num_rx_q = cpu_to_le16(num_rx_q);
	aq.num_rx_bufq = cpu_to_le16(num_rx_bufq);

	err = iecm_send_mb_msg(adapter,
			       VIRTCHNL2_OP_ADD_QUEUES,
			       sizeof(struct virtchnl2_add_queues), (u8 *)&aq);
	if (err)
		return err;

	err = iecm_wait_for_event(adapter, IECM_VC_ADD_QUEUES,
				  IECM_VC_ADD_QUEUES_ERR);
	if (err)
		return err;

	kfree(adapter->config_data.req_qs_chunks);
	adapter->config_data.req_qs_chunks = NULL;

	/* compare vc_msg num queues with vport num queues */
	if (le16_to_cpu(vc_msg->num_tx_q) != num_tx_q ||
	    le16_to_cpu(vc_msg->num_rx_q) != num_rx_q ||
	    le16_to_cpu(vc_msg->num_tx_complq) != num_complq ||
	    le16_to_cpu(vc_msg->num_rx_bufq) != num_rx_bufq) {
		err = -EINVAL;
		goto error;
	}

	size = sizeof(struct virtchnl2_add_queues) +
			((le16_to_cpu(vc_msg->chunks.num_chunks) - 1) *
			sizeof(struct virtchnl2_queue_reg_chunk));
	adapter->config_data.req_qs_chunks =
		kzalloc(size, GFP_KERNEL);
	if (!adapter->config_data.req_qs_chunks) {
		err = -ENOMEM;
		goto error;
	}
	memcpy(adapter->config_data.req_qs_chunks,
	       adapter->vc_msg, size);
error:
	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	return err;
}

/**
 * iecm_send_alloc_vectors_msg - Send virtchnl alloc vectors message
 * @adapter: Driver specific private structure
 * @num_vectors: number of vectors to be allocated
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_alloc_vectors_msg(struct iecm_adapter *adapter, u16 num_vectors)
{
	struct virtchnl2_alloc_vectors *alloc_vec;
	struct virtchnl2_alloc_vectors ac = {0};
	int size, err;

	ac.num_vectors = cpu_to_le16(num_vectors);

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_ALLOC_VECTORS,
			       sizeof(ac), (u8 *)&ac);
	if (err)
		return err;

	err = iecm_wait_for_event(adapter, IECM_VC_ALLOC_VECTORS,
				  IECM_VC_ALLOC_VECTORS_ERR);
	if (err)
		return err;

	size = sizeof(struct virtchnl2_alloc_vectors) +
		((num_vectors - 1) *
		sizeof(struct virtchnl2_vector_chunk));

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;
	adapter->req_vec_chunks = kzalloc(size, GFP_KERNEL);
	if (!adapter->req_vec_chunks) {
		err = -ENOMEM;
		goto error;
	}
	memcpy(adapter->req_vec_chunks, adapter->vc_msg, size);

	alloc_vec = adapter->req_vec_chunks;
	if (le16_to_cpu(alloc_vec->num_vectors) < num_vectors) {
		kfree(adapter->req_vec_chunks);
		adapter->req_vec_chunks = NULL;
		err = -EINVAL;
	}
error:
	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	return err;
}

/**
 * iecm_send_dealloc_vectors_msg - Send virtchnl de allocate vectors message
 * @adapter: Driver specific private structure
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_dealloc_vectors_msg(struct iecm_adapter *adapter)
{
	struct virtchnl2_vector_chunks *vcs;
	struct virtchnl2_alloc_vectors *ac;
	int buf_size, err;

	ac = adapter->req_vec_chunks;
	vcs = &ac->vchunks;

	buf_size = sizeof(struct virtchnl2_vector_chunks) +
			((le16_to_cpu(vcs->num_vchunks) - 1) *
			sizeof(struct virtchnl2_vector_chunk));

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_DEALLOC_VECTORS, buf_size,
			       (u8 *)vcs);
	if (err)
		return err;
	err = iecm_min_wait_for_event(adapter, IECM_VC_DEALLOC_VECTORS,
				      IECM_VC_DEALLOC_VECTORS_ERR);
	if (err)
		return err;

	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;
	return 0;
}

/**
 * iecm_send_get_stats_msg - Send virtchnl get statistics message
 * @vport: vport to get stats for
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_get_stats_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_vport_stats *stats;
	int err = 0;

	stats = (struct virtchnl2_vport_stats *)adapter->vc_msg;

	/* Don't send get_stats message if one is pending or the
	 * link is down
	 */
	if (test_bit(IECM_VC_GET_STATS, adapter->vc_state) ||
	    adapter->state <= __IECM_DOWN)
		goto error;

	stats->vport_id = cpu_to_le32(vport->vport_id);

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_GET_STATS,
			       sizeof(stats), (u8 *)&stats);
	if (err)
		goto error;

	err = iecm_wait_for_event(adapter, IECM_VC_GET_STATS,
				  IECM_VC_GET_STATS_ERR);
	if (err)
		goto error;

	vport->netstats.rx_packets = le64_to_cpu(stats->rx_unicast) +
				     le64_to_cpu(stats->rx_multicast) +
				     le64_to_cpu(stats->rx_broadcast);
	vport->netstats.tx_packets = le64_to_cpu(stats->tx_unicast) +
				     le64_to_cpu(stats->tx_multicast) +
				     le64_to_cpu(stats->tx_broadcast);
	vport->netstats.rx_bytes = le64_to_cpu(stats->rx_bytes);
	vport->netstats.tx_bytes = le64_to_cpu(stats->tx_bytes);
	vport->netstats.tx_errors = le64_to_cpu(stats->tx_errors);
	vport->netstats.rx_dropped = le64_to_cpu(stats->rx_discards);
	vport->netstats.tx_dropped = le64_to_cpu(stats->tx_discards);
	vport->port_stats.vport_stats = *stats;
	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
error:
	clear_bit(__IECM_MB_STATS_PENDING, vport->adapter->flags);

	return err;
}

/**
 * iecm_send_get_set_rss_hash_msg - Send set or get rss hash message
 * @vport: virtual port data structure
 * @get: flag to get or set rss hash
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_get_set_rss_hash_msg(struct iecm_vport *vport, bool get)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_rss_hash rh = {0};
	int err;

	rh.vport_id = cpu_to_le32(vport->vport_id);
	rh.ptype_groups = cpu_to_le64(adapter->rss_data.rss_hash);

	if (get) {
		err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_GET_RSS_HASH,
				       sizeof(rh), (u8 *)&rh);
		if (err)
			return err;

		err = iecm_wait_for_event(adapter, IECM_VC_GET_RSS_HASH,
					  IECM_VC_GET_RSS_HASH_ERR);
		if (err)
			return err;

		memcpy(&rh, adapter->vc_msg, sizeof(rh));
		adapter->rss_data.rss_hash = le64_to_cpu(rh.ptype_groups);
		/* Leave the buffer clean for next message */
		memset(adapter->vc_msg, 0, IECM_DFLT_MBX_BUF_SIZE);
		clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);

		return 0;
	}

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_SET_RSS_HASH,
			       sizeof(rh), (u8 *)&rh);
	if (err)
		return err;

	return  iecm_wait_for_event(adapter, IECM_VC_SET_RSS_HASH,
				    IECM_VC_SET_RSS_HASH_ERR);
}

/**
 * iecm_send_get_set_rss_lut_msg - Send virtchnl get or set rss lut message
 * @vport: virtual port data structure
 * @get: flag to set or get rss look up table
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_get_set_rss_lut_msg(struct iecm_vport *vport, bool get)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_rss_lut *recv_rl;
	struct virtchnl2_rss_lut *rl;
	int buf_size, lut_buf_size;
	int i, err = 0;

	buf_size = sizeof(struct virtchnl2_rss_lut) +
		       (sizeof(u32) * (adapter->rss_data.rss_lut_size - 1));
	rl = kzalloc(buf_size, GFP_KERNEL);
	if (!rl)
		return -ENOMEM;

	if (!get) {
		rl->lut_entries = cpu_to_le16(adapter->rss_data.rss_lut_size);
		for (i = 0; i < adapter->rss_data.rss_lut_size; i++)
			rl->lut[i] = cpu_to_le32(adapter->rss_data.rss_lut[i]);
	}
	rl->vport_id = cpu_to_le32(vport->vport_id);

	if (get) {
		err = iecm_send_mb_msg(vport->adapter, VIRTCHNL2_OP_GET_RSS_LUT,
				       buf_size, (u8 *)rl);
		if (err)
			goto error;

		err = iecm_wait_for_event(adapter, IECM_VC_GET_RSS_LUT,
					  IECM_VC_GET_RSS_LUT_ERR);
		if (err)
			goto error;

		recv_rl = (struct virtchnl2_rss_lut *)adapter->vc_msg;
		if (adapter->rss_data.rss_lut_size !=
		    le16_to_cpu(recv_rl->lut_entries)) {
			adapter->rss_data.rss_lut_size =
				le16_to_cpu(recv_rl->lut_entries);
			kfree(adapter->rss_data.rss_lut);

			lut_buf_size = adapter->rss_data.rss_lut_size *
					sizeof(u32);
			adapter->rss_data.rss_lut = kzalloc(lut_buf_size,
							    GFP_KERNEL);
			if (!adapter->rss_data.rss_lut) {
				adapter->rss_data.rss_lut_size = 0;
				/* Leave the buffer clean */
				memset(adapter->vc_msg, 0,
				       IECM_DFLT_MBX_BUF_SIZE);
				clear_bit(__IECM_VC_MSG_PENDING,
					  adapter->flags);
				err = -ENOMEM;
				goto error;
			}
		}
		memcpy(adapter->rss_data.rss_lut, adapter->vc_msg,
		       adapter->rss_data.rss_lut_size);
		/* Leave the buffer clean for next message */
		memset(adapter->vc_msg, 0, IECM_DFLT_MBX_BUF_SIZE);
		clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	} else {
		err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_SET_RSS_LUT,
				       buf_size, (u8 *)rl);
		if (err)
			goto error;

		err = iecm_wait_for_event(adapter, IECM_VC_SET_RSS_LUT,
					  IECM_VC_SET_RSS_LUT_ERR);
	}
error:
	kfree(rl);
	return err;
}

/**
 * iecm_send_get_set_rss_key_msg - Send virtchnl get or set rss key message
 * @vport: virtual port data structure
 * @get: flag to set or get rss look up table
 *
 * Returns 0 on success, negative on failure
 */
int iecm_send_get_set_rss_key_msg(struct iecm_vport *vport, bool get)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl2_rss_key *recv_rk;
	struct virtchnl2_rss_key *rk;
	int i, buf_size, err = 0;

	buf_size = sizeof(struct virtchnl2_rss_key) +
		       (sizeof(u8) * (adapter->rss_data.rss_key_size - 1));
	rk = kzalloc(buf_size, GFP_KERNEL);
	if (!rk)
		return -ENOMEM;
	rk->vport_id = cpu_to_le32(vport->vport_id);

	if (get) {
		err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_GET_RSS_KEY,
				       buf_size, (u8 *)rk);
		if (err)
			goto error;

		err = iecm_wait_for_event(adapter, IECM_VC_GET_RSS_KEY,
					  IECM_VC_GET_RSS_KEY_ERR);
		if (err)
			goto error;

		recv_rk = (struct virtchnl2_rss_key *)adapter->vc_msg;
		if (adapter->rss_data.rss_key_size !=
		    le16_to_cpu(recv_rk->key_len)) {
			adapter->rss_data.rss_key_size =
				min_t(u16, NETDEV_RSS_KEY_LEN,
				      le16_to_cpu(recv_rk->key_len));
			kfree(adapter->rss_data.rss_key);
			adapter->rss_data.rss_key = kzalloc(adapter->rss_data.rss_key_size,
							    GFP_KERNEL);
			if (!adapter->rss_data.rss_key) {
				adapter->rss_data.rss_key_size = 0;
				/* Leave the buffer clean */
				memset(adapter->vc_msg, 0,
				       IECM_DFLT_MBX_BUF_SIZE);
				clear_bit(__IECM_VC_MSG_PENDING,
					  adapter->flags);
				err = -ENOMEM;
				goto error;
			}
		}
		memcpy(adapter->rss_data.rss_key, adapter->vc_msg,
		       adapter->rss_data.rss_key_size);
		/* Leave the buffer clean for next message */
		memset(adapter->vc_msg, 0, IECM_DFLT_MBX_BUF_SIZE);
		clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	} else {
		rk->key_len = cpu_to_le16(adapter->rss_data.rss_key_size);
		for (i = 0; i < adapter->rss_data.rss_key_size; i++)
			rk->key[i] = adapter->rss_data.rss_key[i];

		err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_SET_RSS_KEY,
				       buf_size, (u8 *)rk);
		if (err)
			goto error;

		err = iecm_wait_for_event(adapter, IECM_VC_SET_RSS_KEY,
					  IECM_VC_SET_RSS_KEY_ERR);
	}
error:
	kfree(rk);
	return err;
}

/**
 * iecm_tpid_to_ethertype - transform from VLAN TPID to virtchnl ethertype
 * @tpid: VLAN TPID (i.e. 0x8100, 0x88a8, etc.)
 *
 * Return virtchnl ethertype
 */
static u32 iecm_tpid_to_ethertype(u16 tpid)
{
	switch (tpid) {
	case ETH_P_8021Q:
		return VIRTCHNL_VLAN_ETHERTYPE_8100;
	case ETH_P_8021AD:
		return VIRTCHNL_VLAN_ETHERTYPE_88A8;
	}

	return 0;
}

/**
 * iecm_set_vlan_offload_ethertype - set ethertype for offload message
 * @adapter: adapter structure
 * @msg: message structure used for updating offloads
 * @offload_op: opcode used to determine which support structure to check
 *
 * Return 0 on success, negative on failure.
 */
static int
iecm_set_vlan_offload_ethertype(struct iecm_adapter *adapter,
				struct virtchnl_vlan_setting *msg,
				enum virtchnl_ops offload_op)
{
	struct virtchnl_vlan_supported_caps *offload_support;
	u16 tpid = adapter->config_data.vlan_ethertype;
	u32 vc_ethertype;

	vc_ethertype = iecm_tpid_to_ethertype(tpid);
	/* reference the correct offload support structure */
	switch (offload_op) {
	case VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2:
	case VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2:
		offload_support =
			&adapter->vlan_caps->offloads.stripping_support;
		break;
	case VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2:
	case VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2:
		offload_support =
			&adapter->vlan_caps->offloads.insertion_support;
		break;
	default:
		dev_err(&adapter->pdev->dev, "Invalid opcode %d for setting virtchnl ethertype to enable/disable VLAN offloads\n",
			offload_op);
		return -EINVAL;
	}

	/* make sure ethertype is supported and turning feature on/off
	 * is allowed
	 */
	if ((offload_support->outer & vc_ethertype) &&
	    (offload_support->outer & VIRTCHNL_VLAN_TOGGLE)) {
		msg->outer_ethertype_setting = vc_ethertype;
	} else if ((offload_support->inner & vc_ethertype) &&
		   (offload_support->inner & VIRTCHNL_VLAN_TOGGLE)) {
		msg->inner_ethertype_setting = vc_ethertype;
	} else {
		dev_err(&adapter->pdev->dev, "opcode %d unsupported for VLAN TPID 0x%04x\n",
			offload_op, tpid);
		return -EINVAL;
	}

	return 0;
}

/**
 * iecm_send_strip_vlan_msg - Send enable/disable vlan stripping message
 * @vport: vport structure
 * @ena: enable or disable vlan stripping
 *
 * Returns 0 on success, negative on failure.
 */
static int iecm_send_strip_vlan_msg(struct iecm_vport *vport, bool ena)
{
	struct iecm_adapter *adapter = vport->adapter;
	enum iecm_vport_vc_state vc, vc_err;
	struct virtchnl_vlan_setting *msg;
	enum virtchnl_ops vop;
	int err, len;

	len = sizeof(struct virtchnl_vlan_setting);
	msg = kzalloc(sizeof(*msg), GFP_KERNEL);

	if (!msg)
		return -ENOMEM;

	msg->vport_id = vport->vport_id;
	if (ena) {
		vop = VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2;
		vc = IECM_VC_STRIPPING_ENA_VLAN_V2;
		vc_err = IECM_VC_STRIPPING_ENA_VLAN_V2_ERR;
	} else {
		vop = VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2;
		vc = IECM_VC_STRIPPING_DIS_VLAN_V2;
		vc_err = IECM_VC_STRIPPING_DIS_VLAN_V2_ERR;
	}

	err = iecm_set_vlan_offload_ethertype(adapter, msg, vop);
	if (!err) {
		err = iecm_send_mb_msg(adapter, vop, len, (u8 *)msg);
		if (!err)
			err = iecm_wait_for_event(adapter, vc, vc_err);
	}

	kfree(msg);

	return err;
}

/**
 * iecm_send_insert_vlan_msg - Send enable/disable vlan insertion message
 * @vport: vport structure
 * @ena: enable/disable vlan insertion
 *
 * Returns 0 on success, negative on failure.
 */
static int iecm_send_insert_vlan_msg(struct iecm_vport *vport, bool ena)
{
	struct iecm_adapter *adapter = vport->adapter;
	enum iecm_vport_vc_state vc, vc_err;
	struct virtchnl_vlan_setting *msg;
	enum virtchnl_ops vop;
	int err, len;

	len = sizeof(struct virtchnl_vlan_setting);
	msg = kzalloc(sizeof(*msg), GFP_KERNEL);

	if (!msg)
		return -ENOMEM;

	msg->vport_id = vport->vport_id;

	if (ena) {
		vop = VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2;
		vc = IECM_VC_INSERTION_ENA_VLAN_V2;
		vc_err = IECM_VC_INSERTION_ENA_VLAN_V2_ERR;
	} else {
		vop = VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2;
		vc = IECM_VC_INSERTION_DIS_VLAN_V2;
		vc_err = IECM_VC_INSERTION_DIS_VLAN_V2_ERR;
	}

	err = iecm_set_vlan_offload_ethertype(adapter, msg, vop);
	if (!err) {
		err = iecm_send_mb_msg(adapter, vop, len, (u8 *)msg);
		if (!err)
			err = iecm_wait_for_event(adapter, vc, vc_err);
	}

	kfree(msg);

	return err;
}

/**
 * iecm_send_add_del_cloud_filter_msg: Send add/del cloud filter message
 * @vport: vport structure
 * @add: True to add, false to delete cloud filter
 *
 * Request the CP/PF to add/del cloud filters as specified by the user via
 * tc tool
 *
 * Return 0 on success, negative on failure
 **/
int iecm_send_add_del_cloud_filter_msg(struct iecm_vport *vport, bool add)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_cloud_filter_config *cf_config;
	struct iecm_cloud_filter *cf;
	struct virtchnl_filter f;
	int len = 0, err = 0;

	cf_config = &adapter->config_data.cf_config;

	while (true) {
		bool process_fltr = false;

		spin_lock_bh(&adapter->cloud_filter_list_lock);
		list_for_each_entry(cf, &cf_config->cloud_filter_list, list) {
			if (add && cf->add) {
				process_fltr = true;
				cf->add = false;
				f = cf->f;
				break;
			} else if (!add && cf->remove) {
				process_fltr = true;
				cf->remove = false;
				f = cf->f;
				break;
			}
		}
		spin_unlock_bh(&adapter->cloud_filter_list_lock);

		/* Don't send mailbox message when there are no filters to add/del */
		if (!process_fltr)
			goto error;

		if (add) {
			err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_ADD_CLOUD_FILTER,
					       len, (u8 *)&f);
			if (err)
				goto error;

			err = iecm_wait_for_event(adapter, IECM_VC_ADD_CLOUD_FILTER,
						  IECM_VC_ADD_CLOUD_FILTER_ERR);
		} else {
			err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_DEL_CLOUD_FILTER,
					       len, (u8 *)&f);
			if (err)
				goto error;

			err =
			iecm_min_wait_for_event(adapter, IECM_VC_DEL_CLOUD_FILTER,
						IECM_VC_DEL_CLOUD_FILTER_ERR);
		}
		if (err)
			break;
	}
error:
	return err;
}

/**
 * iecm_send_add_fdir_filter_msg: Send add Flow Director filter message
 * @vport: vport structure
 *
 * Request the CP/PF to add Flow Director as specified by the user via
 * ethtool
 *
 * Return 0 on success, negative on failure
 **/
int iecm_send_add_fdir_filter_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_fdir_fltr_config *fdir_config;
	struct iecm_fdir_fltr *fdir;
	struct virtchnl_fdir_add *f;
	int len, err = 0;

	fdir_config = &adapter->config_data.fdir_config;
	len = sizeof(struct virtchnl_fdir_add);
	/* kzalloc required because otherwise stack is over 2k */
	f = kzalloc(len, GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	while (true) {
		bool process_fltr = false;

		/* Only add a single Flow Director per call */
		spin_lock_bh(&adapter->fdir_fltr_list_lock);
		list_for_each_entry(fdir, &fdir_config->fdir_fltr_list, list) {
			if (fdir->add) {
				fdir->add = false;
				process_fltr = true;
				memcpy(f, &fdir->vc_add_msg, len);
				break;
			}
		}
		spin_unlock_bh(&adapter->fdir_fltr_list_lock);

		if (!process_fltr)
			break;

		err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_ADD_FDIR_FILTER,
				       len, (u8 *)f);
		if (err)
			break;

		err = iecm_wait_for_event(adapter, IECM_VC_ADD_FDIR_FILTER,
					  IECM_VC_ADD_FDIR_FILTER_ERR);
		if (err)
			break;

		memcpy(f, adapter->vc_msg, len);
		if (f->status == VIRTCHNL_FDIR_SUCCESS) {
			fdir->flow_id = f->flow_id;
		} else {
			err = -EIO;
			break;
		}
		clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	}

	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	kfree(f);
	return err;
}

/**
 * iecm_send_del_fdir_filter_msg: Send del Flow Director filter message
 * @vport: vport structure
 *
 * Request the CP/PF to del Flow Director as specified by the user via
 * ethtool
 *
 * Return 0 on success, negative on failure
 **/
int iecm_send_del_fdir_filter_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_fdir_fltr_config *fdir_config;
	struct iecm_fdir_fltr *fdir;
	struct virtchnl_fdir_del f;
	int err = 0;

	fdir_config = &adapter->config_data.fdir_config;

	while (true) {
		bool process_fltr = false;

		/* Only del a single Flow Director filter per call */
		spin_lock_bh(&adapter->fdir_fltr_list_lock);
		list_for_each_entry(fdir, &fdir_config->fdir_fltr_list, list) {
			if (fdir->remove) {
				process_fltr = true;
				fdir->remove = false;
				f.vsi_id = fdir->vc_add_msg.vsi_id;
				f.flow_id = fdir->flow_id;
				break;
			}
		}
		spin_unlock_bh(&adapter->fdir_fltr_list_lock);

		if (!process_fltr)
			break;

		err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_DEL_FDIR_FILTER,
				       sizeof(struct virtchnl_fdir_del), (u8 *)&f);
		if (err)
			break;

		err = iecm_wait_for_event(adapter, IECM_VC_DEL_FDIR_FILTER,
					  IECM_VC_DEL_FDIR_FILTER_ERR);
		clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	}

	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
	return err;
}

/**
 * iecm_send_enable_channels_msg - Send enable channels message
 * @vport: vport structure
 *
 * Request the PF/CP to enable channels as specified by the user via tc tool.
 * Returns 0 on success, negative on failure.
 **/
int iecm_send_enable_channels_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_channel_config *ch_config;
	struct virtchnl_tc_info *vti = NULL;
	int i, err;
	u16 len;

	ch_config = &adapter->config_data.ch_config;
	len = ((ch_config->num_tc - 1) * sizeof(struct virtchnl_channel_info)) +
		sizeof(struct virtchnl_tc_info);

	vti = kzalloc(len, GFP_KERNEL);
	if (!vti)
		return -ENOMEM;
	vti->num_tc = ch_config->num_tc;
	for (i = 0; i < vti->num_tc; i++) {
		vti->list[i].count = ch_config->ch_info[i].count;
		vti->list[i].offset = ch_config->ch_info[i].offset;
		vti->list[i].pad = 0;
		vti->list[i].max_tx_rate = ch_config->ch_info[i].max_tx_rate;
	}

	err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_ENABLE_CHANNELS, len,
			       (u8 *)vti);
	if (err)
		goto error;

	err = iecm_wait_for_event(adapter, IECM_VC_ENA_CHANNELS,
				  IECM_VC_ENA_CHANNELS_ERR);
error:
	kfree(vti);
	return err;
}

/**
 * iecm_send_disable_channels_msg - Send disable channels message
 * @vport: vport structure to disable channels on
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_disable_channels_msg(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	int err;

	err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_DISABLE_CHANNELS,
			       0, NULL);
	if (err)
		return err;

	err = iecm_min_wait_for_event(adapter, IECM_VC_DIS_CHANNELS,
				      IECM_VC_DIS_CHANNELS_ERR);
	return err;
}

/**
 * iecm_send_vlan_v2_caps_msg - send virtchnl get offload VLAN V2 caps message
 * @adapter: adapter info struct
 *
 * Returns 0 on success and if VLAN V1 capability is set`, negative on failure.
 */
int iecm_send_vlan_v2_caps_msg(struct iecm_adapter *adapter)
{
	int err = 0;

	if (!iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_VLAN))
		return err;

	err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS,
			       0, NULL);
	if (err)
		return err;

	err = iecm_min_wait_for_event(adapter, IECM_VC_OFFLOAD_VLAN_V2_CAPS,
				      IECM_VC_OFFLOAD_VLAN_V2_CAPS_ERR);

	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to recv get caps");
		return err;
	}

	if (!adapter->vlan_caps) {
		adapter->vlan_caps =
		  kzalloc(sizeof(*adapter->vlan_caps), GFP_KERNEL);
		if (!adapter->vlan_caps)
			return -ENOMEM;
	}

	memcpy(adapter->vlan_caps,
	       adapter->vc_msg, sizeof(*adapter->vlan_caps));

	return err;
}

/**
 * iecm_fill_ptype_lookup - Fill L3 specific fields in ptype lookup table
 * @ptype: ptype lookup table
 * @pstate: state machine for ptype lookup table
 * @ipv4: ipv4 or ipv6
 * @frag: fragmentation allowed
 *
 */
static void iecm_fill_ptype_lookup(struct iecm_rx_ptype_decoded *ptype,
				   struct iecm_ptype_state *pstate,
				   bool ipv4, bool frag)
{
	if (!pstate->outer_ip || !pstate->outer_frag) {
		ptype->outer_ip = IECM_RX_PTYPE_OUTER_IP;
		pstate->outer_ip = true;

		if (ipv4)
			ptype->outer_ip_ver = IECM_RX_PTYPE_OUTER_IPV4;
		else
			ptype->outer_ip_ver = IECM_RX_PTYPE_OUTER_IPV6;

		if (frag) {
			ptype->outer_frag = IECM_RX_PTYPE_FRAG;
			pstate->outer_frag = true;
		}
	} else {
		ptype->tunnel_type = IECM_RX_PTYPE_TUNNEL_IP_IP;
		pstate->tunnel_state = IECM_PTYPE_TUNNEL_IP;

		if (ipv4)
			ptype->tunnel_end_prot =
					IECM_RX_PTYPE_TUNNEL_END_IPV4;
		else
			ptype->tunnel_end_prot =
					IECM_RX_PTYPE_TUNNEL_END_IPV6;

		if (frag)
			ptype->tunnel_end_frag = IECM_RX_PTYPE_FRAG;
	}
}

/**
 * iecm_send_get_rx_ptype_msg - Send virtchnl for ptype info
 * @vport: virtual port data structure
 *
 * Returns 0 on success, negative on failure.
 */
int iecm_send_get_rx_ptype_msg(struct iecm_vport *vport)
{
	struct iecm_rx_ptype_decoded *ptype_lkup = vport->rx_ptype_lkup;
	struct virtchnl2_get_ptype_info *get_ptype_info, *ptype_info;
	int max_ptype, ptypes_recvd = 0, len, ptype_offset;
	struct iecm_adapter *adapter = vport->adapter;
	int err = 0, i, j, k = 0;

	if (iecm_is_queue_model_split(vport->rxq_model))
		max_ptype = IECM_RX_MAX_PTYPE;
	else
		max_ptype = IECM_RX_MAX_BASE_PTYPE;

	for (i = 0; i < max_ptype; i++)
		ptype_lkup[i] = iecm_ptype_lookup[0];

	len = sizeof(struct virtchnl2_get_ptype_info);
	get_ptype_info = kzalloc(len, GFP_KERNEL);
	if (!get_ptype_info)
		return -ENOMEM;

	get_ptype_info->start_ptype_id = 0;
	get_ptype_info->num_ptypes = cpu_to_le16(max_ptype);

	err = iecm_send_mb_msg(adapter, VIRTCHNL2_OP_GET_PTYPE_INFO,
			       len, (u8 *)get_ptype_info);
	if (err)
		goto get_ptype_rel;

	while (ptypes_recvd < max_ptype) {
		err = iecm_wait_for_event(adapter, IECM_VC_GET_PTYPE_INFO,
					  IECM_VC_GET_PTYPE_INFO_ERR);
		if (err)
			goto get_ptype_rel;

		len = IECM_DFLT_MBX_BUF_SIZE;
		ptype_info = kzalloc(len, GFP_KERNEL);
		if (!ptype_info) {
			err = -ENOMEM;
			goto clear_vc_flag;
		}

		memcpy(ptype_info, adapter->vc_msg, len);

		ptypes_recvd += le16_to_cpu(ptype_info->num_ptypes);
		if (ptypes_recvd > max_ptype) {
			err = -EINVAL;
			goto ptype_rel;
		}

		ptype_offset = sizeof(struct virtchnl2_get_ptype_info) -
						sizeof(struct virtchnl2_ptype);

		for (i = 0; i < le16_to_cpu(ptype_info->num_ptypes); i++) {
			struct iecm_ptype_state pstate = { 0 };
			struct virtchnl2_ptype *ptype;
			u16 id;

			ptype = (struct virtchnl2_ptype *)
					((u8 *)ptype_info + ptype_offset);

			ptype_offset += IECM_GET_PTYPE_SIZE(ptype);
			if (ptype_offset > len) {
				err = -EINVAL;
				goto ptype_rel;
			}

			if (le16_to_cpu(ptype->ptype_id_10) == 0xFFFF)
				goto ptype_rel;

			if (iecm_is_queue_model_split(vport->rxq_model))
				k = le16_to_cpu(ptype->ptype_id_10);
			else
				k = ptype->ptype_id_8;

			if (ptype->proto_id_count)
				ptype_lkup[k].known = 1;

			for (j = 0; j < ptype->proto_id_count; j++) {
				id = le16_to_cpu(ptype->proto_id[j]);
				switch (id) {
				case VIRTCHNL2_PROTO_HDR_GRE:
					if (pstate.tunnel_state ==
							IECM_PTYPE_TUNNEL_IP) {
						ptype_lkup[k].tunnel_type =
						IECM_RX_PTYPE_TUNNEL_IP_GRENAT;
						pstate.tunnel_state |=
						IECM_PTYPE_TUNNEL_IP_GRENAT;
					}
					break;
				case VIRTCHNL2_PROTO_HDR_MAC:
					ptype_lkup[k].outer_ip =
						IECM_RX_PTYPE_OUTER_L2;
					if (pstate.tunnel_state ==
							IECM_TUN_IP_GRE) {
						ptype_lkup[k].tunnel_type =
						IECM_RX_PTYPE_TUNNEL_IP_GRENAT_MAC;
						pstate.tunnel_state |=
						IECM_PTYPE_TUNNEL_IP_GRENAT_MAC;
					}
					break;
				case VIRTCHNL2_PROTO_HDR_VLAN:
					if (pstate.tunnel_state ==
							IECM_TUN_IP_GRE_MAC) {
						ptype_lkup[k].tunnel_type =
						IECM_RX_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN;
						pstate.tunnel_state |=
						IECM_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN;
					}
					break;
				case VIRTCHNL2_PROTO_HDR_IPV4:
					iecm_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, true,
							       false);
					break;
				case VIRTCHNL2_PROTO_HDR_IPV6:
					iecm_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, false,
							       false);
					break;
				case VIRTCHNL2_PROTO_HDR_IPV4_FRAG:
					iecm_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, true,
							       true);
					break;
				case VIRTCHNL2_PROTO_HDR_IPV6_FRAG:
					iecm_fill_ptype_lookup(&ptype_lkup[k],
							       &pstate, false,
							       true);
					break;
				case VIRTCHNL2_PROTO_HDR_UDP:
					ptype_lkup[k].inner_prot =
					IECM_RX_PTYPE_INNER_PROT_UDP;
					break;
				case VIRTCHNL2_PROTO_HDR_TCP:
					ptype_lkup[k].inner_prot =
					IECM_RX_PTYPE_INNER_PROT_TCP;
					break;
				case VIRTCHNL2_PROTO_HDR_SCTP:
					ptype_lkup[k].inner_prot =
					IECM_RX_PTYPE_INNER_PROT_SCTP;
					break;
				case VIRTCHNL2_PROTO_HDR_ICMP:
					ptype_lkup[k].inner_prot =
					IECM_RX_PTYPE_INNER_PROT_ICMP;
					break;
				case VIRTCHNL2_PROTO_HDR_PAY:
					ptype_lkup[k].payload_layer =
						IECM_RX_PTYPE_PAYLOAD_LAYER_PAY2;
					break;
				case VIRTCHNL2_PROTO_HDR_ICMPV6:
				case VIRTCHNL2_PROTO_HDR_IPV6_EH:
				case VIRTCHNL2_PROTO_HDR_PRE_MAC:
				case VIRTCHNL2_PROTO_HDR_POST_MAC:
				case VIRTCHNL2_PROTO_HDR_ETHERTYPE:
				case VIRTCHNL2_PROTO_HDR_SVLAN:
				case VIRTCHNL2_PROTO_HDR_CVLAN:
				case VIRTCHNL2_PROTO_HDR_MPLS:
				case VIRTCHNL2_PROTO_HDR_MMPLS:
				case VIRTCHNL2_PROTO_HDR_PTP:
				case VIRTCHNL2_PROTO_HDR_CTRL:
				case VIRTCHNL2_PROTO_HDR_LLDP:
				case VIRTCHNL2_PROTO_HDR_ARP:
				case VIRTCHNL2_PROTO_HDR_ECP:
				case VIRTCHNL2_PROTO_HDR_EAPOL:
				case VIRTCHNL2_PROTO_HDR_PPPOD:
				case VIRTCHNL2_PROTO_HDR_PPPOE:
				case VIRTCHNL2_PROTO_HDR_IGMP:
				case VIRTCHNL2_PROTO_HDR_AH:
				case VIRTCHNL2_PROTO_HDR_ESP:
				case VIRTCHNL2_PROTO_HDR_IKE:
				case VIRTCHNL2_PROTO_HDR_NATT_KEEP:
				case VIRTCHNL2_PROTO_HDR_L2TPV2:
				case VIRTCHNL2_PROTO_HDR_L2TPV2_CONTROL:
				case VIRTCHNL2_PROTO_HDR_L2TPV3:
				case VIRTCHNL2_PROTO_HDR_GTP:
				case VIRTCHNL2_PROTO_HDR_GTP_EH:
				case VIRTCHNL2_PROTO_HDR_GTPCV2:
				case VIRTCHNL2_PROTO_HDR_GTPC_TEID:
				case VIRTCHNL2_PROTO_HDR_GTPU:
				case VIRTCHNL2_PROTO_HDR_GTPU_UL:
				case VIRTCHNL2_PROTO_HDR_GTPU_DL:
				case VIRTCHNL2_PROTO_HDR_ECPRI:
				case VIRTCHNL2_PROTO_HDR_VRRP:
				case VIRTCHNL2_PROTO_HDR_OSPF:
				case VIRTCHNL2_PROTO_HDR_TUN:
				case VIRTCHNL2_PROTO_HDR_NVGRE:
				case VIRTCHNL2_PROTO_HDR_VXLAN:
				case VIRTCHNL2_PROTO_HDR_VXLAN_GPE:
				case VIRTCHNL2_PROTO_HDR_GENEVE:
				case VIRTCHNL2_PROTO_HDR_NSH:
				case VIRTCHNL2_PROTO_HDR_QUIC:
				case VIRTCHNL2_PROTO_HDR_PFCP:
				case VIRTCHNL2_PROTO_HDR_PFCP_NODE:
				case VIRTCHNL2_PROTO_HDR_PFCP_SESSION:
				case VIRTCHNL2_PROTO_HDR_RTP:
				case VIRTCHNL2_PROTO_HDR_NO_PROTO:
				default:
					continue;
				}
			}
		}
		clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
		kfree(ptype_info);
	}
	kfree(get_ptype_info);
	return 0;

ptype_rel:
	kfree(ptype_info);
clear_vc_flag:
	clear_bit(__IECM_VC_MSG_PENDING, adapter->flags);
get_ptype_rel:
	kfree(get_ptype_info);
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

/**
 * iecm_vc_core_init - Initialize mailbox and get resources
 * @adapter: Driver specific private structure
 * @vport_id: Virtual port identifier
 *
 * Will check if HW is ready with reset complete. Initializes the mailbox and
 * communicate with master to get all the default vport parameters. Returns 0
 * on success, -EAGAIN function will get called again, otherwise negative on
 * failure.
 */
int iecm_vc_core_init(struct iecm_adapter *adapter, int *vport_id)
{
	int err;

	switch (adapter->state) {
	case __IECM_STARTUP:
		if (iecm_send_ver_msg(adapter))
			goto init_failed;
		adapter->state = __IECM_VER_CHECK;
		goto restart;
	case __IECM_VER_CHECK:
		err = iecm_recv_ver_msg(adapter);
		if (err == -EAGAIN) {
			adapter->state = __IECM_STARTUP;
			goto restart;
		} else if (err) {
			goto init_failed;
		}
		adapter->state = __IECM_GET_CAPS;
		if (adapter->dev_ops.vc_ops.get_caps(adapter))
			goto init_failed;
		goto restart;
	case __IECM_GET_CAPS:
		if (iecm_recv_get_caps_msg(adapter))
			goto init_failed;
		if (iecm_send_create_vport_msg(adapter))
			goto init_failed;
		adapter->state = __IECM_GET_DFLT_VPORT_PARAMS;
		goto restart;
	case __IECM_GET_DFLT_VPORT_PARAMS:
		if (iecm_recv_create_vport_msg(adapter, vport_id))
			goto init_failed;
		adapter->state = __IECM_INIT_SW;
		break;
	case __IECM_INIT_SW:
		break;
	default:
		dev_err(&adapter->pdev->dev, "Device is in bad state: %d\n",
			adapter->state);
		goto init_failed;
	}

	return 0;
restart:
	queue_delayed_work(adapter->init_wq, &adapter->init_task,
			   msecs_to_jiffies(30));
	/* Not an error. Using try again to continue with state machine */
	return -EAGAIN;
init_failed:
	if (++adapter->mb_wait_count > IECM_MB_MAX_ERR) {
		dev_err(&adapter->pdev->dev, "Failed to establish mailbox communications with hardware\n");
		return -EFAULT;
	}
	adapter->state = __IECM_STARTUP;
	/* If it reaches here, it is possible that mailbox queue initialization
	 * register writes might not have taken effect. Retry to initialize
	 * the mailbox again
	 */
	iecm_deinit_dflt_mbx(adapter);
	set_bit(__IECM_HR_DRV_LOAD, adapter->flags);
	queue_delayed_work(adapter->vc_event_wq, &adapter->vc_event_task,
			   msecs_to_jiffies(20));
	return -EAGAIN;
}
EXPORT_SYMBOL(iecm_vc_core_init);

/**
 * iecm_vport_init - Initialize virtual port
 * @vport: virtual port to be initialized
 * @vport_id: Unique identification number of vport
 *
 * Will initialize vport with the info received through MB earlier
 */
static void iecm_vport_init(struct iecm_vport *vport,
			    __always_unused int vport_id)
{
	struct virtchnl2_create_vport *vport_msg;
	u16 rx_itr[] = {2, 8, 32, 96, 128};
	u16 tx_itr[] = {2, 8, 64, 128, 256};

	vport_msg = (struct virtchnl2_create_vport *)
				vport->adapter->vport_params_recvd[0];
	vport->txq_model = le16_to_cpu(vport_msg->txq_model);
	vport->rxq_model = le16_to_cpu(vport_msg->rxq_model);
	vport->vport_type = le16_to_cpu(vport_msg->vport_type);
	vport->vport_id = le32_to_cpu(vport_msg->vport_id);
	vport->adapter->rss_data.rss_key_size =
				min_t(u16, NETDEV_RSS_KEY_LEN,
				      le16_to_cpu(vport_msg->rss_key_size));
	vport->adapter->rss_data.rss_lut_size =
				le16_to_cpu(vport_msg->rss_lut_size);
	ether_addr_copy(vport->default_mac_addr, vport_msg->default_mac_addr);
	vport->max_mtu = IECM_MAX_MTU;

	if (iecm_is_queue_model_split(vport->rxq_model)) {
		vport->num_bufqs_per_qgrp = IECM_MAX_BUFQS_PER_RXQ_GRP;
		/* Bufq[0] default buffer size is 4K
		 * Bufq[1] default buffer size is 2K
		 */
		vport->bufq_size[0] = IECM_RX_BUF_4096;
		vport->bufq_size[1] = IECM_RX_BUF_2048;
	} else {
		vport->num_bufqs_per_qgrp = 0;
		vport->bufq_size[0] = IECM_RX_BUF_2048;
	}

	/*Initialize Tx and Rx profiles for Dynamic Interrupt Moderation */
	memcpy(vport->rx_itr_profile, rx_itr, IECM_DIM_PROFILE_SLOTS);
	memcpy(vport->tx_itr_profile, tx_itr, IECM_DIM_PROFILE_SLOTS);

	iecm_vport_set_hsplit(vport, true);

	iecm_vport_init_num_qs(vport, vport_msg);
	iecm_vport_calc_num_q_desc(vport);
	iecm_vport_calc_num_q_groups(vport);
	iecm_vport_calc_num_q_vec(vport);
}

/**
 * iecm_get_vec_ids - Initialize vector id from Mailbox parameters
 * @adapter: adapter structure to get the mailbox vector id
 * @vecids: Array of vector ids
 * @num_vecids: number of vector ids
 * @chunks: vector ids received over mailbox
 *
 * Will initialize the mailbox vector id which is received from the
 * get capabilities and data queue vector ids with ids received as
 * mailbox parameters.
 * Returns number of ids filled
 */
int iecm_get_vec_ids(struct iecm_adapter *adapter,
		     u16 *vecids, int num_vecids,
		     struct virtchnl2_vector_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_vchunks);
	u16 start_vecid, num_vec;
	int num_vecid_filled = 0;
	int i, j;

	vecids[num_vecid_filled] = adapter->mb_vector.v_idx;
	num_vecid_filled++;

	for (j = 0; j < num_chunks; j++) {
		struct virtchnl2_vector_chunk *chunk = &chunks->vchunks[j];

		num_vec = le16_to_cpu(chunk->num_vectors);
		start_vecid = le16_to_cpu(chunk->start_vector_id);
		for (i = 0; i < num_vec; i++) {
			if ((num_vecid_filled + i) < num_vecids) {
				vecids[num_vecid_filled + i] = start_vecid;
				start_vecid++;
			} else {
				break;
			}
		}
		num_vecid_filled = num_vecid_filled + i;
	}

	return num_vecid_filled;
}

/**
 * iecm_vport_get_queue_ids - Initialize queue id from Mailbox parameters
 * @qids: Array of queue ids
 * @num_qids: number of queue ids
 * @q_type: queue model
 * @chunks: queue ids received over mailbox
 *
 * Will initialize all queue ids with ids received as mailbox parameters
 * Returns number of ids filled
 */
static int
iecm_vport_get_queue_ids(u32 *qids, int num_qids, u16 q_type,
			 struct virtchnl2_queue_reg_chunks *chunks)
{
	u16 num_chunks = le16_to_cpu(chunks->num_chunks);
	u32 num_q_id_filled = 0, i;
	u32 start_q_id, num_q;

	while (num_chunks) {
		struct virtchnl2_queue_reg_chunk *chunk = &chunks->chunks[num_chunks - 1];

		if (le32_to_cpu(chunk->type) == q_type) {
			num_q = le32_to_cpu(chunk->num_queues);
			start_q_id = le32_to_cpu(chunk->start_queue_id);
			for (i = 0; i < num_q; i++) {
				if ((num_q_id_filled + i) < num_qids) {
					qids[num_q_id_filled + i] = start_q_id;
					start_q_id++;
				} else {
					break;
				}
			}
			num_q_id_filled = num_q_id_filled + i;
		}
		num_chunks--;
	}

	return num_q_id_filled;
}

/**
 * __iecm_vport_queue_ids_init - Initialize queue ids from Mailbox parameters
 * @vport: virtual port for which the queues ids are initialized
 * @qids: queue ids
 * @num_qids: number of queue ids
 * @q_type: type of queue
 *
 * Will initialize all queue ids with ids received as mailbox
 * parameters. Returns number of queue ids initialized.
 */
static int
__iecm_vport_queue_ids_init(struct iecm_vport *vport, u32 *qids,
			    int num_qids, u32 q_type)
{
	struct iecm_queue *q;
	int i, j, k = 0;

	switch (q_type) {
	case VIRTCHNL2_QUEUE_TYPE_TX:
		for (i = 0; i < vport->num_txq_grp; i++) {
			struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];

			for (j = 0; j < tx_qgrp->num_txq; j++) {
				if (k < num_qids) {
					tx_qgrp->txqs[j]->q_id = qids[k];
					tx_qgrp->txqs[j]->q_type =
						VIRTCHNL2_QUEUE_TYPE_TX;
					k++;
				} else {
					break;
				}
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
			int num_rxq;

			if (iecm_is_queue_model_split(vport->rxq_model))
				num_rxq = rx_qgrp->splitq.num_rxq_sets;
			else
				num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq && k < num_qids; j++, k++) {
				if (iecm_is_queue_model_split(vport->rxq_model))
					q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
				else
					q = rx_qgrp->singleq.rxqs[j];
				q->q_id = qids[k];
				q->q_type = VIRTCHNL2_QUEUE_TYPE_RX;
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
		for (i = 0; i < vport->num_txq_grp; i++) {
			struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];

			if (k < num_qids) {
				tx_qgrp->complq->q_id = qids[k];
				tx_qgrp->complq->q_type =
					VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
				k++;
			} else {
				break;
			}
		}
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		for (i = 0; i < vport->num_rxq_grp; i++) {
			struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];

			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				if (k < num_qids) {
					q = &rx_qgrp->splitq.bufq_sets[j].bufq;
					q->q_id = qids[k];
					q->q_type =
						VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
					k++;
				} else {
					break;
				}
			}
		}
		break;
	default:
		break;
	}

	return k;
}

/**
 * iecm_vport_queue_ids_init - Initialize queue ids from Mailbox parameters
 * @vport: virtual port for which the queues ids are initialized
 *
 * Will initialize all queue ids with ids received as mailbox parameters.
 * Returns 0 on success, negative if all the queues are not initialized.
 */
static int iecm_vport_queue_ids_init(struct iecm_vport *vport)
{
	struct virtchnl2_create_vport *vport_params;
	struct virtchnl2_queue_reg_chunks *chunks;
	/* We may never deal with more than 256 same type of queues */
#define IECM_MAX_QIDS	256
	u32 qids[IECM_MAX_QIDS];
	int num_ids;
	u16 q_type;

	if (vport->adapter->config_data.req_qs_chunks) {
		struct virtchnl2_add_queues *vc_aq =
			(struct virtchnl2_add_queues *)
			vport->adapter->config_data.req_qs_chunks;
		chunks = &vc_aq->chunks;
	} else {
		vport_params = (struct virtchnl2_create_vport *)
				vport->adapter->vport_params_recvd[0];
		chunks = &vport_params->chunks;
	}

	num_ids = iecm_vport_get_queue_ids(qids, IECM_MAX_QIDS,
					   VIRTCHNL2_QUEUE_TYPE_TX,
					   chunks);
	if (num_ids != vport->num_txq)
		return -EINVAL;
	num_ids = __iecm_vport_queue_ids_init(vport, qids, num_ids,
					      VIRTCHNL2_QUEUE_TYPE_TX);
	if (num_ids != vport->num_txq)
		return -EINVAL;
	num_ids = iecm_vport_get_queue_ids(qids, IECM_MAX_QIDS,
					   VIRTCHNL2_QUEUE_TYPE_RX,
					   chunks);
	if (num_ids != vport->num_rxq)
		return -EINVAL;
	num_ids = __iecm_vport_queue_ids_init(vport, qids, num_ids,
					      VIRTCHNL2_QUEUE_TYPE_RX);
	if (num_ids != vport->num_rxq)
		return -EINVAL;

	if (iecm_is_queue_model_split(vport->txq_model)) {
		q_type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
		num_ids = iecm_vport_get_queue_ids(qids, IECM_MAX_QIDS, q_type,
						   chunks);
		if (num_ids != vport->num_complq)
			return -EINVAL;
		num_ids = __iecm_vport_queue_ids_init(vport, qids,
						      num_ids,
						      q_type);
		if (num_ids != vport->num_complq)
			return -EINVAL;
	}

	if (iecm_is_queue_model_split(vport->rxq_model)) {
		q_type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
		num_ids = iecm_vport_get_queue_ids(qids, IECM_MAX_QIDS, q_type,
						   chunks);
		if (num_ids != vport->num_bufq)
			return -EINVAL;
		num_ids = __iecm_vport_queue_ids_init(vport, qids, num_ids,
						      q_type);
		if (num_ids != vport->num_bufq)
			return -EINVAL;
	}

	return 0;
}

/**
 * iecm_vport_adjust_qs - Adjust to new requested queues
 * @vport: virtual port data struct
 *
 * Renegotiate queues.  Returns 0 on success, negative on failure.
 */
void iecm_vport_adjust_qs(struct iecm_vport *vport)
{
	struct virtchnl2_create_vport vport_msg;

	vport_msg.txq_model = cpu_to_le16(vport->txq_model);
	vport_msg.rxq_model = cpu_to_le16(vport->rxq_model);
	iecm_vport_calc_total_qs(vport->adapter, &vport_msg);

	iecm_vport_init_num_qs(vport, &vport_msg);
	iecm_vport_calc_num_q_groups(vport);
	iecm_vport_calc_num_q_vec(vport);
}

/**
 * iecm_is_capability_ena - Default implementation of capability checking
 * @adapter: Private data struct
 * @all: all or one flag
 * @field: caps field to check for flags
 * @flag: flag to check
 *
 * Return true if all capabilities are supported, false otherwise
 */
static bool iecm_is_capability_ena(struct iecm_adapter *adapter, bool all,
				   enum iecm_cap_field field, u64 flag)
{
	u8 *caps = (u8 *)adapter->caps;
	u32 *cap_field;

	if (field == IECM_BASE_CAPS)
		return false;
	if (field >= IECM_CAP_FIELD_LAST) {
		dev_err(&adapter->pdev->dev, "Bad capability field: %d\n",
			field);
		return false;
	}
	cap_field = (u32 *)(caps + field);

	if (all)
		return (*cap_field & flag) == flag;
	else
		return !!(*cap_field & flag);
}

/**
 * iecm_get_reserved_vectors - Default implementation to get reserved vectors
 * @adapter: Private data struct
 *
 * Return number of vectors reserved
 */
static u16 iecm_get_reserved_vectors(struct iecm_adapter *adapter)
{
	struct virtchnl2_get_capabilities *caps;

	caps = (struct virtchnl2_get_capabilities *)adapter->caps;
	return le16_to_cpu(caps->num_allocated_vectors);
}

/**
 * iecm_get_max_tx_bufs - Max scatter-gather TX buffers
 * @adapter: Private data struct
 *
 * Return maximum number of buffers that can be used in scatter-gather before
 * they need to be linearized for hardware.
 */
static unsigned int iecm_get_max_tx_bufs(struct iecm_adapter *adapter)
{
	return ((struct virtchnl2_get_capabilities *)adapter->caps)->max_sg_bufs_per_tx_pkt;
}

/**
 * iecm_add_del_ether_addrs
 * @vport: virtual port data structure
 * @add: Add or delete flag
 * @async: Don't wait for return message
 *
 * Request that the PF add or delete one or more addresses to our filters.
 **/
void iecm_add_del_ether_addrs(struct iecm_vport *vport, bool add, bool async)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl_ether_addr_list *veal = NULL;
	int num_entries, num_msgs, total_filters = 0;
	struct pci_dev *pdev = adapter->pdev;
	enum iecm_vport_vc_state vc, vc_err;
	struct virtchnl_ether_addr *eal;
	struct iecm_mac_filter *f, *tmp;
	int i = 0, k = 0, err = 0;
	enum virtchnl_ops vop;

	spin_lock_bh(&adapter->mac_filter_list_lock);

	/* Find the number of newly added filters */
	list_for_each_entry(f, &adapter->config_data.mac_filter_list, list) {
		if (add && f->add)
			total_filters++;
		else if (!add && f->remove)
			total_filters++;
	}
	if (!total_filters) {
		spin_unlock_bh(&adapter->mac_filter_list_lock);
		goto error;
	}

	/* Fill all the new filters into virtchannel message */
	eal = kcalloc(total_filters, sizeof(struct virtchnl_ether_addr),
		      GFP_ATOMIC);
	if (!eal) {
		err = -ENOMEM;
		spin_unlock_bh(&adapter->mac_filter_list_lock);
		goto error;
	}
	list_for_each_entry_safe(f, tmp, &adapter->config_data.mac_filter_list,
				 list) {
		if (add && f->add) {
			ether_addr_copy(eal[i].addr, f->macaddr);
			i++;
			f->add = false;
			if (i == total_filters)
				break;
		}
		if (!add && f->remove) {
			ether_addr_copy(eal[i].addr, f->macaddr);
			i++;
			list_del(&f->list);
			kfree(f);
			if (i == total_filters)
				break;
		}
	}

	spin_unlock_bh(&adapter->mac_filter_list_lock);

	/* Chunk up the filters into multiple messages to avoid
	 * sending a control queue message buffer that is too large
	 */
	if (total_filters < IECM_NUM_FILTERS_PER_MSG)
		num_entries = total_filters;
	else
		num_entries = IECM_NUM_FILTERS_PER_MSG;

	num_msgs = DIV_ROUND_UP(total_filters, IECM_NUM_FILTERS_PER_MSG);

	for (i = 0, k = 0; i < num_msgs || num_entries; i++) {
		int buf_size = sizeof(struct virtchnl_ether_addr_list) +
			(sizeof(struct virtchnl_ether_addr) * num_entries);
		if (!veal || num_entries != IECM_NUM_FILTERS_PER_MSG) {
			kfree(veal);
			veal = kzalloc(buf_size, GFP_KERNEL);
			if (!veal) {
				err = -ENOMEM;
				goto list_prep_error;
			}
		} else {
			memset(veal, 0, buf_size);
		}

		veal->vsi_id = vport->vport_id;
		veal->num_elements = num_entries;
		memcpy(veal->list, &eal[k],
		       sizeof(struct virtchnl_ether_addr) * num_entries);

		if (add) {
			vop = VIRTCHNL_OP_ADD_ETH_ADDR;
			vc = IECM_VC_ADD_ETH_ADDR;
			vc_err = IECM_VC_ADD_ETH_ADDR_ERR;
		} else  {
			vop = VIRTCHNL_OP_DEL_ETH_ADDR;
			vc = IECM_VC_DEL_ETH_ADDR;
			vc_err = IECM_VC_DEL_ETH_ADDR_ERR;
		}
		err = iecm_send_mb_msg(vport->adapter, vop, buf_size,
				       (u8 *)veal);
		if (err)
			goto mbx_error;

		if (!async) {
			err = iecm_wait_for_event(vport->adapter, vc, vc_err);
			if (err)
				goto mbx_error;
		}

		k += num_entries;
		total_filters -= num_entries;
		if (total_filters < IECM_NUM_FILTERS_PER_MSG)
			num_entries = total_filters;
	}
mbx_error:
	kfree(veal);
list_prep_error:
	kfree(eal);
error:
	if (err)
		dev_err(&pdev->dev, "Failed to add or del mac filters %d", err);
}

/**
 * iecm_set_promiscuous - set promiscuous and send message to mailbox
 * @adapter: Driver specific private structure
 *
 * Request that the PF enable promiscuous mode for our VSI.  Message is sent
 * asynchronously and won't wait for response.  Returns 0 on success, negative
 * on failure;
 */
int iecm_set_promiscuous(struct iecm_adapter *adapter)
{
	struct iecm_vport *vport = adapter->vports[0];
	struct virtchnl_promisc_info vpi;
	u16 flags = 0;
	int err = 0;

	if (test_bit(__IECM_PROMISC_UC, adapter->config_data.user_flags))
		flags |= FLAG_VF_UNICAST_PROMISC;
	if (test_bit(__IECM_PROMISC_MC,
		     adapter->config_data.user_flags))
		flags |= FLAG_VF_MULTICAST_PROMISC;

	vpi.vsi_id = vport->vport_id;
	vpi.flags = flags;
	err = iecm_send_mb_msg(adapter, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
			       sizeof(struct virtchnl_promisc_info),
			       (u8 *)&vpi);
	return err;
}

/**
 * iecm_add_del_vlans - Add or delete vlan filter
 * @vport: vport structure
 * @add: add or delete
 *
 * Request that the PF add one or more VLAN filters to our VSI.
 */
static void iecm_add_del_vlans(struct iecm_vport *vport, bool add)
{
	struct virtchnl_vlan_supported_caps *filtering_support;
	struct iecm_adapter *adapter = vport->adapter;
	struct virtchnl_vlan_filter_list_v2 *vvfl_v2;
	int total_vlans = 0, num_vlans, i, len;
	struct iecm_vlan_filter *f, *ftmp;
	struct virtchnl_vlan *vlan;
	enum virtchnl_ops vop;
	int err = 0;

	spin_lock_bh(&adapter->vlan_list_lock);

	list_for_each_entry(f, &adapter->config_data.vlan_filter_list, list) {
		if ((add && f->add) || (!add && f->remove))
			total_vlans++;
	}

	if (!total_vlans) {
		spin_unlock_bh(&adapter->vlan_list_lock);
		return;
	}

	len = sizeof(struct virtchnl_vlan_filter_list_v2) +
		(IECM_VLANS_PER_MSG * sizeof(struct virtchnl_vlan_filter));
	vvfl_v2 = kzalloc(len, GFP_ATOMIC);

	if (!vvfl_v2) {
		err = -ENOMEM;
		goto error;
	}

	if (add)
		vop = VIRTCHNL_OP_ADD_VLAN_V2;
	else
		vop = VIRTCHNL_OP_DEL_VLAN_V2;

	while (total_vlans) {
		if (total_vlans > IECM_VLANS_PER_MSG)
			num_vlans = IECM_VLANS_PER_MSG;
		else
			num_vlans = total_vlans;
		total_vlans -= num_vlans;

		len = sizeof(struct virtchnl_vlan_filter_list_v2) +
			((num_vlans - 1) * sizeof(struct virtchnl_vlan_filter));
		vvfl_v2->vport_id = vport->vport_id;
		vvfl_v2->num_elements = num_vlans;
		i = 0;
		list_for_each_entry_safe(f, ftmp,
					 &adapter->config_data.vlan_filter_list,
					 list) {
			filtering_support =
			&adapter->vlan_caps->filtering.filtering_support;
			if (add && f->add) {
				/* give priority over outer if it's enabled */
				if (filtering_support->outer)
					vlan = &vvfl_v2->filters[i].outer;
				else
					vlan = &vvfl_v2->filters[i].inner;
				vlan->tci = f->vlan.vid;
				vlan->tpid = f->vlan.tpid;
				i++;
				f->add = false;
			} else if (!add && f->remove) {
				/* give priority over outer if it's enabled */
				if (filtering_support->outer)
					vlan = &vvfl_v2->filters[i].outer;
				else
					vlan = &vvfl_v2->filters[i].inner;
				vlan->tci = f->vlan.vid;
				vlan->tpid = f->vlan.tpid;
				i++;
				f->remove = false;
			}
			if (i == num_vlans)
				break;
		}
		spin_unlock_bh(&adapter->vlan_list_lock);
		iecm_send_mb_msg(adapter, vop, len, (u8 *)vvfl_v2);
		spin_lock_bh(&adapter->vlan_list_lock);
	}
	spin_unlock_bh(&adapter->vlan_list_lock);
	kfree(vvfl_v2);
	return;
error:
	spin_unlock_bh(&adapter->vlan_list_lock);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to add or del vlan filters %d", err);
}

/**
 * iecm_vc_ops_init - Initialize virtchnl common api
 * @adapter: Driver specific private structure
 *
 * Initialize the function pointers with the extended feature set functions
 * as APF will deal only with new set of opcodes.
 */
void iecm_vc_ops_init(struct iecm_adapter *adapter)
{
	struct iecm_virtchnl_ops *vc_ops = &adapter->dev_ops.vc_ops;

	vc_ops->core_init = iecm_vc_core_init;
	vc_ops->vport_init = iecm_vport_init;
	vc_ops->vport_queue_ids_init = iecm_vport_queue_ids_init;
	vc_ops->get_caps = iecm_send_get_caps_msg;
	vc_ops->is_cap_ena = iecm_is_capability_ena;
	vc_ops->get_reserved_vecs = iecm_get_reserved_vectors;
	vc_ops->config_queues = iecm_send_config_queues_msg;
	vc_ops->enable_queues = iecm_send_enable_queues_msg;
	vc_ops->disable_queues = iecm_send_disable_queues_msg;
	vc_ops->add_queues = iecm_send_add_queues_msg;
	vc_ops->delete_queues = iecm_send_delete_queues_msg;
	vc_ops->irq_map_unmap = iecm_send_map_unmap_queue_vector_msg;
	vc_ops->enable_vport = iecm_send_enable_vport_msg;
	vc_ops->disable_vport = iecm_send_disable_vport_msg;
	vc_ops->destroy_vport = iecm_send_destroy_vport_msg;
	vc_ops->get_ptype = iecm_send_get_rx_ptype_msg;
	vc_ops->get_set_rss_key = iecm_send_get_set_rss_key_msg;
	vc_ops->get_set_rss_lut = iecm_send_get_set_rss_lut_msg;
	vc_ops->get_set_rss_hash = iecm_send_get_set_rss_hash_msg;
	vc_ops->adjust_qs = iecm_vport_adjust_qs;
	vc_ops->add_del_vlans = iecm_add_del_vlans;
	vc_ops->strip_vlan_msg = iecm_send_strip_vlan_msg;
	vc_ops->insert_vlan_msg = iecm_send_insert_vlan_msg;
	vc_ops->init_max_queues = iecm_vport_init_max_qs;
	vc_ops->get_max_tx_bufs = iecm_get_max_tx_bufs;
	vc_ops->vportq_reg_init = iecm_queue_reg_init;
	vc_ops->alloc_vectors = iecm_send_alloc_vectors_msg;
	vc_ops->dealloc_vectors = iecm_send_dealloc_vectors_msg;
	vc_ops->get_supported_desc_ids = iecm_get_supported_desc_ids;
	vc_ops->get_stats_msg = iecm_send_get_stats_msg;
	vc_ops->recv_mbx_msg = NULL;
}
EXPORT_SYMBOL(iecm_vc_ops_init);
