// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>

#include "bootimage.h"
#include "cmd_chan.h"
#include "device.h"
#include "host_chardev.h"
#include "ipc_c2h_events.h"
#include "msg_scheduler.h"
#include "nnp_boot_defs.h"

static DEFINE_IDA(dev_ida);

bool nnpdev_no_devices(void)
{
	return ida_is_empty(&dev_ida);
}

/**
 * process_query_version_reply() - process a "query_version_reply" response
 * @work: work struct of the calling work
 *
 * This function processes a "query_version_reply" response message from
 * the card which is sent as reply to query_version command submitted
 * earlier.
 * The function checks that the IPC protocol version that is supported by the
 * device matches the one supported by the driver. If there is no match the
 * device state is put in error.
 * There are two IPC protocol versions which are checked:
 * 'protocol_version': is IPC protocol version of command and response messages
 *         That are built (for commands) and processed by this kernel mode
 *         driver. The protocol is defined in ipc_include/ipc_protocol.h
 *         A mismatch is possible in cases that the device has booted with
 *         a wrong/older version of the card boot image.
 * 'chan_protocol_version': is IPC protocol of command and responses which are
 *         supported by the device but are built and processed in user-space.
 *         The structure of the commands and responses are mostly opaque to
 *         the kernel mode driver. This separation allows to update the
 *         device boot image and user-space library to support new sets
 *         of commands without changing the kernel driver.
 *         The restriction for such commands and responses is that the lowest
 *         16-bits of the command/response are defined to include the
 *         command/response opcode and the channel id.
 *         The kernel driver should also know for each possible command and
 *         response opcode the size of the message. This info is received
 *         from the device within this "query_version_reply" response
 *         encoded in the chan_resp_op_size and chan_cmd_op_size fields
 *         of the response.
 */
static void process_query_version_reply(struct work_struct *work)
{
	struct query_version_work *query_version_work;
	struct nnp_device *nnpdev;
	u32 protocol_version;
	u32 card_boot_state;
	u32 val;
	u64 chan_resp_op_size;
	u64 chan_cmd_op_size;
	int i;

	query_version_work =
		container_of(work, struct query_version_work, work);
	nnpdev = container_of(query_version_work, struct nnp_device,
			      query_version_work);
	protocol_version = NNP_IPC_PROTOCOL_VERSION;
	card_boot_state = FIELD_GET(NNP_CARD_BOOT_STATE_MASK,
				    nnpdev->card_doorbell_val);

	chan_resp_op_size = query_version_work->chan_resp_op_size;
	chan_cmd_op_size = query_version_work->chan_cmd_op_size;
	for (i = 0; i < NNP_IPC_NUM_USER_OPS; i++) {
		nnpdev->ipc_chan_resp_op_size[i] = chan_resp_op_size & 0x3;
		chan_resp_op_size >>= 2;
		nnpdev->ipc_chan_cmd_op_size[i] = chan_cmd_op_size & 0x3;
		chan_cmd_op_size >>= 2;
	}

	nnpdev->protocol_version =
		query_version_work->protocol_version;
	nnpdev->chan_protocol_version =
		query_version_work->chan_protocol_version;

	/*
	 * NOTE: The card firmware and host driver protocol version must
	 * exactly match in the major and minor version components.
	 * There is no backwards compatibility on the protocol!
	 * When a device is put in a protocol version error state, the
	 * user must install a matching device firmware and reset the device
	 * in order to allow the device to function.
	 */
	if (NNP_VERSION_MAJOR(query_version_work->protocol_version) !=
	    NNP_VERSION_MAJOR(protocol_version) ||
	    NNP_VERSION_MINOR(query_version_work->protocol_version) !=
	    NNP_VERSION_MINOR(protocol_version) ||
	    query_version_work->chan_resp_op_size == 0) {
		nnpdev_set_boot_state(nnpdev, NNP_DEVICE_FAILED_VERSION);
		/* set host driver state in doorbell register */
		val = FIELD_PREP(NNP_HOST_DRV_STATE_MASK,
				 NNP_HOST_DRV_STATE_VERSION_ERROR);
		nnpdev->ops->set_host_doorbell_value(nnpdev, val);
	} else if (card_boot_state == NNP_CARD_BOOT_STATE_DRV_READY) {
		nnpdev_set_boot_state(nnpdev, NNP_DEVICE_CARD_DRIVER_READY);
	} else if (card_boot_state == NNP_CARD_BOOT_STATE_CARD_READY) {
		/* Card driver finished initialization */
		nnpdev_set_boot_state(nnpdev,
				      NNP_DEVICE_CARD_DRIVER_READY |
				      NNP_DEVICE_CARD_READY |
				      NNP_DEVICE_CARD_ENABLED);
	}

	query_version_work->running = false;
}

static int handle_query_version_reply3(struct nnp_device *nnpdev,
				       const u64 *msgbuf, int avail_qwords)
{
	int msg_qwords = 3; /* QUERY_VERSION_REPLY3 response len is 3 qwords */

	if (avail_qwords < msg_qwords)
		return 0;

	/*
	 * This should not happen, but if it does, just ignore the message
	 * There is no fear in race condition on "running" flag as only
	 * single version reply message should be processed after each
	 * device reset.
	 */
	if (nnpdev->query_version_work.running)
		return msg_qwords;

	nnpdev->query_version_work.running = true;
	nnpdev->query_version_work.protocol_version =
		FIELD_GET(NNP_C2H_VERSION_REPLY_QW0_PROT_VER_MASK, msgbuf[0]);
	nnpdev->query_version_work.chan_protocol_version =
		FIELD_GET(NNP_C2H_VERSION_REPLY_QW0_CHAN_VER_MASK, msgbuf[0]);
	nnpdev->query_version_work.chan_resp_op_size = msgbuf[1];
	nnpdev->query_version_work.chan_cmd_op_size = msgbuf[2];

	queue_work(nnpdev->wq, &nnpdev->query_version_work.work);

	return msg_qwords;
}

/**
 * handle_bios_protocol() - process response coming from card's BIOS.
 * @nnpdev: The nnp device
 * @msgbuf: pointer to response message content
 * @avail_qwords: number of 64-bit units available in @msgbuf
 *
 * IPC protocol with card's BIOS may have different response sizes.
 * @avail_qwords is the number of 64-bit units available in @msgbuf buffer.
 * If the actual response size is larger then available data in the buffer,
 * the function returns 0 to indicate that this is a partial response. Otherwise
 * the actual response size is returned (in units of qwords).
 *
 * Return: 0 if @msgbuf contains a partial response otherwise the number of
 * qwords of the response in @msgbuf.
 */
static int handle_bios_protocol(struct nnp_device *nnpdev, const u64 *msgbuf,
				int avail_qwords)
{
	int msg_size, msg_qwords;

	msg_size = FIELD_GET(NNP_C2H_BIOS_PROTOCOL_TYPE_MASK, msgbuf[0]);

	/* The +1 is because size field does not include header */
	msg_qwords = DIV_ROUND_UP(msg_size, 8) + 1;

	if (msg_qwords > avail_qwords)
		return 0;

	return msg_qwords;
}

struct nnp_chan *nnpdev_find_channel(struct nnp_device *nnpdev, u16 chan_id)
{
	struct nnp_chan *cmd_chan;

	spin_lock(&nnpdev->lock);
	hash_for_each_possible(nnpdev->cmd_chan_hash, cmd_chan, hash_node, chan_id)
		if (cmd_chan->chan_id == chan_id) {
			nnp_chan_get(cmd_chan);
			spin_unlock(&nnpdev->lock);
			return cmd_chan;
		}
	spin_unlock(&nnpdev->lock);

	return NULL;
}

static void disconnect_all_channels(struct nnp_device *nnpdev)
{
	struct nnp_chan *cmd_chan;
	int i;

restart:
	spin_lock(&nnpdev->lock);
	hash_for_each(nnpdev->cmd_chan_hash, i, cmd_chan, hash_node) {
		spin_unlock(&nnpdev->lock);
		nnp_chan_disconnect(cmd_chan);
		nnp_chan_put(cmd_chan);
		goto restart;
	}
	spin_unlock(&nnpdev->lock);
}

static void nnpdev_submit_device_event_to_channels(struct nnp_device *nnpdev,
						   u64 event_msg)
{
	struct nnp_chan *cmd_chan;
	int i;
	unsigned int event_code;
	bool should_wake = false;
	bool is_card_fatal;

	event_code = FIELD_GET(NNP_C2H_EVENT_REPORT_CODE_MASK, event_msg);
	is_card_fatal = is_card_fatal_event(event_code);

	spin_lock(&nnpdev->lock);
	hash_for_each(nnpdev->cmd_chan_hash, i, cmd_chan, hash_node) {
		/*
		 * Update channel's card critical error,
		 * but do not override it if a more sever "fatal_drv" error
		 * event is already set.
		 */
		if (is_card_fatal &&
		    !is_card_fatal_drv_event(chan_broken(cmd_chan))) {
			cmd_chan->card_critical_error_msg = event_msg;
			should_wake = true;
		}

		/* Send the event message to the channel (if needed) */
		if (is_card_fatal || cmd_chan->get_device_events)
			nnp_chan_add_response(cmd_chan, &event_msg, sizeof(event_msg));
	}
	spin_unlock(&nnpdev->lock);

	if (should_wake)
		wake_up_all(&nnpdev->waitq);

	/*
	 * On card fatal event, we consider the device dead and there is
	 * no point communicating with it. The user will destroy the channel
	 * and initiate a device reset to fix this.
	 * We disconnect all channels and set each as "destroyed" since the
	 * NNP_IPC_CHANNEL_DESTROYED response, which normally do that, will
	 * never arrive.
	 */
	if (is_card_fatal_drv_event(event_code))
		disconnect_all_channels(nnpdev);
}

static void handle_channel_destroy(struct nnp_device *nnpdev, u64 event_msg)
{
	struct nnp_chan *cmd_chan;
	unsigned int chan_id;

	chan_id = FIELD_GET(NNP_C2H_EVENT_REPORT_OBJ_ID_MASK, event_msg);
	cmd_chan = nnpdev_find_channel(nnpdev, chan_id);
	if (!cmd_chan) {
		dev_err(nnpdev->dev,
			"Got channel destroyed reply for not existing channel %d\n",
			chan_id);
		return;
	}

	/*
	 * Channel is destroyed on device. Put the main ref of cmd_chan if it
	 * did not already done.
	 * There is one possible case that the channel will be already marked
	 * as destroyed when we get here. This is when we got some card fatal
	 * event, which caused us to flag the channel as destroyed, but later
	 * the "destroy channel" response has arrived from the device
	 * (unexpected).
	 */
	if (!nnp_chan_set_destroyed(cmd_chan))
		nnp_chan_put(cmd_chan);

	/* put against the get from find_channel */
	nnp_chan_put(cmd_chan);
}

/*
 * this function handle device-level event report message.
 * which is usually affect the entire device and not a single channel
 */
static void process_device_event(struct nnp_device *nnpdev, u64 event_msg)
{
	unsigned int event_code = FIELD_GET(NNP_C2H_EVENT_REPORT_CODE_MASK, event_msg);
	unsigned int obj_id, event_val;

	if (!is_card_fatal_event(event_code)) {
		switch (event_code) {
		case NNP_IPC_DESTROY_CHANNEL_FAILED:
			obj_id = FIELD_GET(NNP_C2H_EVENT_REPORT_OBJ_ID_MASK, event_msg);
			event_val = FIELD_GET(NNP_C2H_EVENT_REPORT_VAL_MASK, event_msg);
			dev_err(nnpdev->dev,
				"Channel destroyed failed channel %d val %d\n",
				obj_id, event_val);
			/*
			 * We should not enter this case never as the card will
			 * send this response only when the driver requested to
			 * destroy a not-exist channel, which means a driver
			 * bug.
			 * To handle the case we continue and destroy the channel
			 * on the host side.
			 */
			fallthrough;
		case NNP_IPC_CHANNEL_DESTROYED:
			handle_channel_destroy(nnpdev, event_msg);
			break;
		default:
			dev_err(nnpdev->dev,
				"Unknown event received - %u\n", event_code);
			return;
		}
	}

	/* submit the event to all channels requested to get device events */
	nnpdev_submit_device_event_to_channels(nnpdev, event_msg);
}

struct event_report_work {
	struct work_struct work;
	struct nnp_device  *nnpdev;
	u64                event_msg;
};

static void device_event_report_handler(struct work_struct *work)
{
	struct event_report_work *req =
		container_of(work, struct event_report_work, work);

	process_device_event(req->nnpdev, req->event_msg);

	kfree(req);
}

static int handle_event_report(struct nnp_device *nnpdev, const u64 *msgbuf,
			       int avail_qwords)
{
	int msg_qwords = 1; /* EVENT_REPORT response len is 1 qword */
	struct event_report_work *req;
	u64 event_msg;

	if (avail_qwords < msg_qwords)
		return 0;

	event_msg = msgbuf[0];
	if (FIELD_GET(NNP_C2H_EVENT_REPORT_CHAN_VALID_MASK, event_msg)) {
		struct nnp_chan *cmd_chan;
		unsigned int chan_id;

		chan_id = FIELD_GET(NNP_C2H_EVENT_REPORT_CHAN_ID_MASK, event_msg);
		cmd_chan = nnpdev_find_channel(nnpdev, chan_id);
		if (cmd_chan) {
			nnp_chan_add_response(cmd_chan, &event_msg, sizeof(event_msg));
			nnp_chan_put(cmd_chan);
		} else {
			dev_dbg(nnpdev->dev,
				"Got Event Report for non existing channel id %d\n",
				chan_id);
		}
		return msg_qwords;
	}

	req = kzalloc(sizeof(*req), GFP_NOWAIT);
	if (!req)
		return msg_qwords;

	req->event_msg = event_msg;
	req->nnpdev = nnpdev;
	INIT_WORK(&req->work, device_event_report_handler);
	queue_work(nnpdev->wq, &req->work);

	return msg_qwords;
}

typedef int (*response_handler)(struct nnp_device *nnpdev, const u64 *msgbuf,
				int avail_qwords);

static response_handler resp_handlers[NNP_IPC_C2H_OPCODE_LAST + 1] = {
	[NNP_IPC_C2H_OP_QUERY_VERSION_REPLY3] = handle_query_version_reply3,
	[NNP_IPC_C2H_OP_EVENT_REPORT] = handle_event_report,
	[NNP_IPC_C2H_OP_BIOS_PROTOCOL] = handle_bios_protocol
};

static int dispatch_chan_message(struct nnp_device *nnpdev, u64 *hw_msg,
				 unsigned int size)
{
	int op_code = FIELD_GET(NNP_C2H_CHAN_MSG_OP_MASK, hw_msg[0]);
	int chan_id = FIELD_GET(NNP_C2H_CHAN_MSG_CHAN_ID_MASK, hw_msg[0]);
	struct nnp_chan *chan;
	int msg_size;

	if (op_code < NNP_IPC_MIN_USER_OP ||
	    op_code > NNP_IPC_MAX_USER_OP) {
		/* Should not happen! */
		dev_err(nnpdev->dev,
			"chan response opcode out-of-range received %d (0x%llx)\n",
			op_code, *hw_msg);
		return -EINVAL;
	}

	msg_size = nnpdev->ipc_chan_resp_op_size[op_code - NNP_IPC_MIN_USER_OP];
	if (msg_size == 0) {
		/* Should not happen! */
		dev_err(nnpdev->dev,
			"Unknown response chan opcode received %d (0x%llx)\n",
			op_code, *hw_msg);
		return -EINVAL;
	}

	/* Check for partial message */
	if (size < msg_size)
		return -ENOSPC;

	chan = nnpdev_find_channel(nnpdev, chan_id);
	if (!chan) {
		dev_err(nnpdev->dev,
			"Got response for invalid channel chan_id=%d 0x%llx\n",
			chan_id, *hw_msg);
		return msg_size;
	}

	nnp_chan_add_response(chan, hw_msg, msg_size * 8);
	nnp_chan_put(chan);

	return msg_size;
}

/**
 * nnpdev_process_messages() - process response messages from nnpi device
 * @nnpdev: The nnp device
 * @hw_msg: pointer to response message content
 * @hw_nof_msg: number of 64-bit units available in hw_msg buffer.
 *
 * This function is called from the PCIe device driver when response messages
 * are arrived in the HWQ. It is called in sequence, should not be re-entrant.
 * The function may not block !
 */
void nnpdev_process_messages(struct nnp_device *nnpdev, u64 *hw_msg,
			     unsigned int hw_nof_msg)
{
	int j = 0;
	int msg_size;
	u64 *msg;
	unsigned int nof_msg;
	bool fatal_protocol_error = false;

	/* ignore any response if protocol error detected */
	if ((nnpdev->state & NNP_DEVICE_PROTOCOL_ERROR) != 0)
		return;

	/*
	 * If we have pending messages from previous round
	 * copy the new messages to the pending list and process
	 * the pending list.
	 * otherwise process the messages received from HW directly
	 */
	msg = hw_msg;
	nof_msg = hw_nof_msg;
	if (nnpdev->response_num_msgs > 0) {
		/*
		 * Check to prevent response buffer overrun.
		 * This should never happen since the buffer is twice
		 * the size of the HW response queue. This check is
		 * for safety and debug purposes.
		 */
		if (hw_nof_msg + nnpdev->response_num_msgs >=
		    NNP_DEVICE_RESPONSE_BUFFER_LEN) {
			dev_dbg(nnpdev->dev,
				"device response buffer would overrun: %d + %d !!\n",
				nnpdev->response_num_msgs, hw_nof_msg);
			return;
		}

		memcpy(&nnpdev->response_buf[nnpdev->response_num_msgs], hw_msg,
		       hw_nof_msg * sizeof(u64));
		msg = nnpdev->response_buf;
		nof_msg = nnpdev->response_num_msgs + hw_nof_msg;
	}

	/*
	 * loop for each message
	 */
	do {
		int op_code = FIELD_GET(NNP_C2H_OP_MASK, msg[j]);
		response_handler handler;

		/* opcodes above OP_BIOS_PROTOCOL are routed to a channel */
		if (op_code > NNP_IPC_C2H_OP_BIOS_PROTOCOL) {
			msg_size = dispatch_chan_message(nnpdev, &msg[j],
							 nof_msg - j);
			if (msg_size < 0) {
				if (msg_size != -ENOSPC)
					fatal_protocol_error = true;
				break;
			}

			j += msg_size;
			continue;
		}

		/* dispatch the message request */
		handler = resp_handlers[op_code];
		if (!handler) {
			/* Should not happen! */
			dev_dbg(nnpdev->dev,
				"Unknown response opcode received %d (0x%llx)\n",
				op_code, msg[j]);
			fatal_protocol_error = true;
			break;
		}

		msg_size = (*handler)(nnpdev, &msg[j], (nof_msg - j));

		j += msg_size;
	} while (j < nof_msg || !msg_size);

	if (fatal_protocol_error)
		nnpdev->state |= NNP_DEVICE_PROTOCOL_ERROR;

	/*
	 * If unprocessed messages left, copy it to the pending messages buffer
	 * for the next time this function will be called.
	 */
	if (j < nof_msg) {
		memcpy(&nnpdev->response_buf[0], &msg[j],
		       (nof_msg - j) * sizeof(u64));
		nnpdev->response_num_msgs = nof_msg - j;
	} else {
		nnpdev->response_num_msgs = 0;
	}
}
EXPORT_SYMBOL(nnpdev_process_messages);

static void send_sysinfo_request_to_bios(struct nnp_device *nnpdev)
{
	u64 cmd[3];

	cmd[0] = FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW0_OP_MASK,
			    NNP_IPC_H2C_OP_BIOS_PROTOCOL);
	cmd[0] |= FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW0_TYPE_MASK,
			     NNP_IPC_H2C_TYPE_SYSTEM_INFO_REQ);
	cmd[0] |= FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW0_SIZE_MASK,
			     2 * sizeof(u64));

	cmd[1] = (u64)nnpdev->bios_system_info_dma_addr;

	cmd[2] = FIELD_PREP(NNP_H2C_BIOS_SYS_INFO_REQ_QW2_SIZE_MASK,
			    NNP_PAGE_SIZE);

	nnpdev->ops->cmdq_flush(nnpdev);

	nnpdev->ops->cmdq_write_mesg(nnpdev, cmd, 3);
}

/**
 * build_bios_version_string() - builds printable string of bios version string
 * @nnpdev: pointer to device structure
 *
 * Initializes nnpdev->bios_version_str with printable string of bios version
 * from bios_system_info page.
 */
static void build_bios_version_string(struct nnp_device *nnpdev)
{
	unsigned int i;
	__le16 *v;

	if (!nnpdev->bios_system_info)
		return;

	/*
	 * The bios version string in the bios's system info page
	 * holds __le16 for each character in the version string.
	 * (see struct nnp_c2h_bios_version)
	 * Here we convert it to string of chars by taking only the
	 * LSB from each 16-bit character
	 */
	v = (__le16 *)&nnpdev->bios_system_info->bios_ver;

	/* check that bios version string is corrected null terminated */
	if (nnpdev->bios_system_info->bios_ver.null_terminator != 0)
		return;

	for (i = 0; i < NNP_BIOS_VERSION_LEN - 1 && v[i] != 0; ++i)
		nnpdev->bios_version_str[i] = v[i];

	nnpdev->bios_version_str[i] = '\0';
}

static int unload_boot_image(struct nnp_device *nnpdev)
{
	nnpdev->boot_image_loaded = false;
	return nnpdev_unload_boot_image(nnpdev);
}

/**
 * nnpdev_set_boot_state() - sets new device state.
 * @nnpdev: pointer to device structure
 * @mask: mask of device state bits defined in device.h
 *
 * This function sets new device status and handles the state machine of
 * device boot flow.
 * It is being called when various device notifications are received or
 * some error conditions are detected.
 *
 * The following flow describes the communication flow with the NNP-I card's
 * BIOS during the device boot flow, this function gets called when device
 * state changes when progressing in this flow:
 * 1) The device report its boot state through the "card doorbell" register,
 *    that signals an interrupt to the host and the "pci" layer in the driver
 *    calls the nnpdev_card_doorbell_value_changed function.
 * 2) When the device signals that it is "Ready to boot", the host driver
 *    sends it through the "command queue" an address of page in host memory.
 * 3) The card BIOS fills the page of memory with card system info and change
 *    the doorbell value to "sysinfo ready"
 * 4) The host driver then initiate the boot image loading.
 * 5) When boot image is ready in memory, the host driver send a
 *    "Boot image ready" message and the card BIOS starts booting and changes
 *    the doorbell value to indicate success or failure.
 * 6) When receiving indication about success/failure the host driver signals
 *    that the device no longer needs the boot image in memory.
 *    When all devices no longer need the image it will be removed.
 */
void nnpdev_set_boot_state(struct nnp_device *nnpdev, u32 mask)
{
	u32 state, prev_state;
	bool becomes_ready = false;
	int ret;

	/*
	 * Save previous state and modify current state
	 * with the changed state mask
	 */
	spin_lock(&nnpdev->lock);
	prev_state = nnpdev->state;
	if ((mask & NNP_DEVICE_CARD_BOOT_STATE_MASK) != 0) {
		/*
		 * When boot state changes previous boot states are reset.
		 * also, device error conditions is cleared.
		 */
		nnpdev->state &= ~(NNP_DEVICE_CARD_BOOT_STATE_MASK);
		nnpdev->state &= ~(NNP_DEVICE_ERROR_MASK);
	}
	nnpdev->state |= mask;
	state = nnpdev->state;
	spin_unlock(&nnpdev->lock);

	dev_dbg(nnpdev->dev,
		"device state change 0x%x --> 0x%x\n", prev_state, state);

	/* Unload boot image if boot started or failed */
	if (nnpdev->boot_image_loaded &&
	    (((state & NNP_DEVICE_BOOT_STARTED) &&
	      !(prev_state & NNP_DEVICE_BOOT_STARTED)) ||
	     (state & NNP_DEVICE_BOOT_FAILED))) {
		ret = unload_boot_image(nnpdev);
		/* This should never fail */
		if (ret)
			dev_dbg(nnpdev->dev,
				"Unexpected error while unloading boot image. rc=%d\n",
				ret);
	}

	/* if in error state - no need to check rest of the states */
	if (state & NNP_DEVICE_ERROR_MASK)
		return;

	if ((state & NNP_DEVICE_BOOT_BIOS_READY) &&
	    !(prev_state & NNP_DEVICE_BOOT_BIOS_READY)) {
		becomes_ready = true;
		nnpdev->is_recovery_bios = false;
	}

	if ((state & NNP_DEVICE_BOOT_RECOVERY_BIOS_READY) &&
	    !(prev_state & NNP_DEVICE_BOOT_RECOVERY_BIOS_READY)) {
		becomes_ready = true;
		nnpdev->is_recovery_bios = true;
	}

	if (becomes_ready ||
	    mask == NNP_DEVICE_BOOT_BIOS_READY ||
	    mask == NNP_DEVICE_BOOT_RECOVERY_BIOS_READY) {
		if (!becomes_ready)
			dev_dbg(nnpdev->dev, "Re-sending sysinfo page to bios!!\n");

		/* Send request to fill system_info buffer */
		send_sysinfo_request_to_bios(nnpdev);
		return;
	}

	/* Handle boot image request */
	if ((state & NNP_DEVICE_BOOT_SYSINFO_READY) &&
	    !(prev_state & NNP_DEVICE_BOOT_SYSINFO_READY) &&
	    !nnpdev->boot_image_loaded) {
		build_bios_version_string(nnpdev);
		nnpdev->bios_system_info_valid = true;
		nnpdev->boot_image_loaded = true;
		ret = nnpdev_load_boot_image(nnpdev);

		if (ret)
			dev_err(nnpdev->dev,
				"Unexpected error while loading boot image. rc=%d\n",
				ret);
	}

	/* Handle transition to active state */
	if (((state & NNP_DEVICE_CARD_DRIVER_READY) ||
	     (state & NNP_DEVICE_CARD_READY)) &&
	    !(prev_state & NNP_DEVICE_CARD_DRIVER_READY) &&
	    !(prev_state & NNP_DEVICE_CARD_READY)) {
		u32 val;

		/* set host driver state to "Driver ready" */
		val = FIELD_PREP(NNP_HOST_DRV_STATE_MASK, NNP_HOST_DRV_STATE_READY);
		nnpdev->ops->set_host_doorbell_value(nnpdev, val);
	}
}

/**
 * nnpdev_init() - initialize NNP-I device structure.
 * @nnpdev: device to be initialized
 * @dev: device structure representing the card device
 * @ops: NNP-I device driver operations
 *
 * This function is called by the device driver module when a new NNP-I device
 * is created. The function initialize NNP-I framework's device structure.
 * The device driver must call nnpdev_destroy before the underlying device is
 * removed and before the driver module get unloaded.
 * The device driver must also make sure that when nnpdev_destroy is called the
 * device is quiesced. Meaning, the physical device does no longer throw events
 * and no operations on the nnpdev will be requested.
 *
 * Return: zero on success, error value otherwise.
 */
int nnpdev_init(struct nnp_device *nnpdev, struct device *dev,
		const struct nnp_device_ops *ops)
{
	int ret;

	ret = ida_simple_get(&dev_ida, 0, NNP_MAX_DEVS, GFP_KERNEL);
	if (ret < 0)
		return ret;

	nnpdev->id = ret;
	/*
	 * It is fine to keep pointers to the underlying device and driver
	 * ops since driver must call nnpdev_destroy before the device is
	 * removed or module gets unloaded.
	 */
	nnpdev->dev = dev;
	nnpdev->ops = ops;
	nnpdev->protocol_version = 0;

	nnpdev->protocol_version = 0;

	ida_init(&nnpdev->cmd_chan_ida);
	hash_init(nnpdev->cmd_chan_hash);
	init_waitqueue_head(&nnpdev->waitq);

	nnpdev->cmdq_sched = nnp_msched_create(nnpdev);
	if (!nnpdev->cmdq_sched) {
		ret = -ENOMEM;
		goto err_ida;
	}

	nnpdev->cmdq = nnp_msched_queue_create(nnpdev->cmdq_sched);
	if (!nnpdev->cmdq) {
		ret = -ENOMEM;
		goto err_msg_sched;
	}

	nnpdev->wq = create_singlethread_workqueue("nnpdev_wq");
	if (!nnpdev->wq) {
		ret = -ENOMEM;
		goto err_cmdq;
	}

	/* setup memory for bios system info */
	nnpdev->bios_system_info =
		dma_alloc_coherent(nnpdev->dev, NNP_PAGE_SIZE,
				   &nnpdev->bios_system_info_dma_addr, GFP_KERNEL);
	if (!nnpdev->bios_system_info) {
		ret = -ENOMEM;
		goto err_wq;
	}

	/* set host driver state to "Not ready" */
	nnpdev->ops->set_host_doorbell_value(nnpdev, 0);

	spin_lock_init(&nnpdev->lock);
	nnpdev_boot_image_init(&nnpdev->boot_image);
	INIT_WORK(&nnpdev->query_version_work.work, process_query_version_reply);

	return 0;

err_wq:
	destroy_workqueue(nnpdev->wq);
err_cmdq:
	nnp_msched_queue_destroy(nnpdev->cmdq);
err_msg_sched:
	nnp_msched_destroy(nnpdev->cmdq_sched);
err_ida:
	ida_simple_remove(&dev_ida, nnpdev->id);
	return ret;
}
EXPORT_SYMBOL(nnpdev_init);

struct doorbell_work {
	struct work_struct work;
	struct nnp_device  *nnpdev;
	u32                val;
};

static void doorbell_changed_handler(struct work_struct *work)
{
	struct doorbell_work *req = container_of(work, struct doorbell_work,
						 work);
	u32 boot_state, state = 0;
	u32 error_state;
	u32 doorbell_val = req->val;
	struct nnp_device *nnpdev = req->nnpdev;
	u64 query_cmd;

	nnpdev->card_doorbell_val = doorbell_val;

	error_state = FIELD_GET(NNP_CARD_ERROR_MASK, doorbell_val);
	boot_state = FIELD_GET(NNP_CARD_BOOT_STATE_MASK, doorbell_val);

	if (error_state) {
		state = NNP_DEVICE_BOOT_FAILED;

		switch (error_state) {
		case NNP_CARD_ERROR_NOT_CAPSULE:
			state |= NNP_DEVICE_CAPSULE_EXPECTED;
			break;
		case NNP_CARD_ERROR_CORRUPTED_IMAGE:
			state |= NNP_DEVICE_CORRUPTED_BOOT_IMAGE;
			break;
		case NNP_CARD_ERROR_CAPSULE_FAILED:
			state |= NNP_DEVICE_CAPSULE_FAILED;
			break;
		default:
			break;
		}
	} else if (boot_state != nnpdev->curr_boot_state) {
		nnpdev->curr_boot_state = boot_state;
		switch (boot_state) {
		case NNP_CARD_BOOT_STATE_BIOS_READY:
			state = NNP_DEVICE_BOOT_BIOS_READY;
			break;
		case NNP_CARD_BOOT_STATE_RECOVERY_BIOS_READY:
			state = NNP_DEVICE_BOOT_RECOVERY_BIOS_READY;
			break;
		case NNP_CARD_BOOT_STATE_BIOS_SYSINFO_READY:
			state = NNP_DEVICE_BOOT_SYSINFO_READY;
			break;
		case NNP_CARD_BOOT_STATE_BOOT_STARTED:
			state = NNP_DEVICE_BOOT_STARTED;
			break;
		case NNP_CARD_BOOT_STATE_BIOS_FLASH_STARTED:
			state = NNP_DEVICE_BIOS_UPDATE_STARTED;
			break;
		case NNP_CARD_BOOT_STATE_DRV_READY:
		case NNP_CARD_BOOT_STATE_CARD_READY:
			/* card is up - send "query_version" command */
			query_cmd = FIELD_PREP(NNP_H2C_OP_MASK,
					       NNP_IPC_H2C_OP_QUERY_VERSION);
			if (nnp_msched_queue_msg(nnpdev->cmdq, query_cmd) ||
			    nnp_msched_queue_sync(nnpdev->cmdq))
				dev_err(nnpdev->dev, "Query version msg error\n");
			break;

		case NNP_CARD_BOOT_STATE_NOT_READY:
			/* card is down reset the device boot and error state */
			spin_lock(&nnpdev->lock);
			nnpdev->state = 0;
			spin_unlock(&nnpdev->lock);
			break;
		default:
			break;
		}
	}

	if (state)
		nnpdev_set_boot_state(nnpdev, state);

	kfree(req);
}

/**
 * nnpdev_card_doorbell_value_changed() - card doorbell changed notification
 * @nnpdev: The nnp device
 * @doorbell_val: The new value of the doorbell register
 *
 * This function is called from the NNP-I device driver when the card's doorbell
 * register is changed.
 */
void nnpdev_card_doorbell_value_changed(struct nnp_device *nnpdev,
					u32 doorbell_val)
{
	struct doorbell_work *req;

	dev_dbg(nnpdev->dev, "Got card doorbell value 0x%x\n", doorbell_val);

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return;

	req->nnpdev = nnpdev;
	req->val = doorbell_val;
	INIT_WORK(&req->work, doorbell_changed_handler);
	queue_work(nnpdev->wq, &req->work);
}
EXPORT_SYMBOL(nnpdev_card_doorbell_value_changed);

/**
 * nnpdev_destroy() - destroy nnp device object
 * @nnpdev: The nnp device to be destroyed.
 *
 * This function must be called by the device driver module when NNP-I device
 * is removed or the device driver get unloaded.
 */
void nnpdev_destroy(struct nnp_device *nnpdev)
{
	dev_dbg(nnpdev->dev, "Destroying NNP-I device\n");

	/*
	 * If device is removed while boot image load is in-flight,
	 * stop the image load and flag it is not needed.
	 */
	if (nnpdev->boot_image_loaded)
		unload_boot_image(nnpdev);

	destroy_workqueue(nnpdev->wq);

	disconnect_all_channels(nnpdev);
	dma_free_coherent(nnpdev->dev, NNP_PAGE_SIZE, nnpdev->bios_system_info,
			  nnpdev->bios_system_info_dma_addr);

	nnp_msched_destroy(nnpdev->cmdq_sched);
	/*
	 * nnpdev->cmd_chan_ida is empty after disconnect_all_channels,
	 * ida_destroy is not needed
	 */
	ida_simple_remove(&dev_ida, nnpdev->id);
}
EXPORT_SYMBOL(nnpdev_destroy);

static int __init nnp_init(void)
{
	return nnp_init_host_interface();
}
subsys_initcall(nnp_init);

static void __exit nnp_cleanup(void)
{
	nnp_release_host_interface();
	/* dev_ida is already empty here - no point calling ida_destroy */
}
module_exit(nnp_cleanup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel(R) NNPI Framework");
MODULE_AUTHOR("Intel Corporation");
MODULE_FIRMWARE(NNP_FIRMWARE_NAME);
