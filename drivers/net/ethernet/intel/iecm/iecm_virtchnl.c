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
 * iecm_send_create_vport_msg - Send virtchnl create vport message
 * @adapter: Driver specific private structure
 *
 * send virtchnl creae vport message
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_send_create_vport_msg(struct iecm_adapter *adapter)
{
	/* stub */
	return 0;
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
	/* stub */
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
	/* stub */
	return 0;
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
	vc_ops->get_reserved_vecs = NULL;
	vc_ops->config_queues = NULL;
	vc_ops->enable_queues = NULL;
	vc_ops->disable_queues = NULL;
	vc_ops->add_queues = NULL;
	vc_ops->delete_queues = NULL;
	vc_ops->irq_map_unmap = NULL;
	vc_ops->enable_vport = NULL;
	vc_ops->disable_vport = NULL;
	vc_ops->destroy_vport = NULL;
	vc_ops->get_ptype = NULL;
	vc_ops->get_set_rss_key = NULL;
	vc_ops->get_set_rss_lut = NULL;
	vc_ops->get_set_rss_hash = NULL;
	vc_ops->adjust_qs = NULL;
	vc_ops->add_del_vlans = NULL;
	vc_ops->strip_vlan_msg = NULL;
	vc_ops->insert_vlan_msg = NULL;
	vc_ops->init_max_queues = NULL;
	vc_ops->get_max_tx_bufs = NULL;
	vc_ops->vportq_reg_init = NULL;
	vc_ops->alloc_vectors = NULL;
	vc_ops->dealloc_vectors = NULL;
	vc_ops->get_supported_desc_ids = NULL;
	vc_ops->get_stats_msg = NULL;
	vc_ops->recv_mbx_msg = NULL;
}
EXPORT_SYMBOL(iecm_vc_ops_init);
