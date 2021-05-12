/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef NNPDRV_CMD_CHAN_H
#define NNPDRV_CMD_CHAN_H

#include <linux/bitfield.h>
#include <linux/circ_buf.h>
#include <linux/hashtable.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "device.h"
#include "ipc_c2h_events.h"

/**
 * enum nnp_chan_state - indicate special state of a command channel
 * @NNP_CHAN_NORMAL: channel is in normal state.
 * @NNP_CHAN_DESTROYED: channel should be treated as no-longer-exist on card.
 */
enum nnp_chan_state {
	NNP_CHAN_NORMAL = 0,
	NNP_CHAN_DESTROYED,
};

/**
 * struct nnp_chan - structure object for user<->device communication
 * @ref: refcount for this object
 * @nnpdev: the device this channel is connected to. May be NULL after device
 *          disconnects (on device removal or reset).
 * @chan_id: the ipc channel id for this channel
 * @hash_node: node to include this object in list of channels
 *             hash is in (cmd_chan_hash in nnp_device).
 * @event_msg: ipc event response received from device during create channel
 * @card_critical_error_msg: last critical event report received from device
 * @get_device_events: true if device-level events received from card should
 *                     be sent over this channel to user.
 * @fd: file descriptor created for the channel (implements read/write)
 * @cmdq: message queue added to msg_scheduler, for user commands to be sent
 *        to the device.
 * @host_file: reference to opened "/dev/nnpi_host" object which defines the
 *             nnp_user object this channel connects to.
 * @nnp_user: the nnp_user this channel belongs to.
 *             the channel can reference host resources created by this
 *             nnp_user object.
 * @dev_mutex: protects @nnpdev and @state
 * @state: the current state of this channel.
 * @resp_waitq: waitqueue used for waiting for response messages be available.
 * @respq: circular buffer object that receive response messages from device.
 * @respq_lock: protects @respq
 * @respq_buf: buffer space allocated for circular response buffer.
 * @respq_size: current allocated size of circular response buffer.
 * @resp_lost: number of response messages lost due to response buffer full.
 */
struct nnp_chan {
	struct kref            ref;
	struct nnp_device      *nnpdev;
	u16                    chan_id;
	struct hlist_node      hash_node;
	u64                    event_msg;
	u64                    card_critical_error_msg;
	bool                   get_device_events;

	int fd;
	struct nnp_msched_queue    *cmdq;
	struct file                *host_file;
	struct nnp_user_info       *nnp_user;

	struct mutex      dev_mutex;
	wait_queue_head_t resp_waitq;
	enum nnp_chan_state state;

	struct circ_buf   respq;
	spinlock_t        respq_lock;
	char              *respq_buf;
	unsigned int      respq_size;
	unsigned int      resp_lost;
};

#define chan_broken(chan) FIELD_GET(NNP_C2H_EVENT_REPORT_CODE_MASK, (chan)->card_critical_error_msg)
#define chan_drv_fatal(chan) (is_card_fatal_drv_event(chan_broken(chan)))

struct nnp_chan *nnpdev_chan_create(struct nnp_device *nnpdev, int host_fd,
				    unsigned int min_id, unsigned int max_id,
				    bool get_device_events);

void nnp_chan_get(struct nnp_chan *cmd_chan);
void nnp_chan_put(struct nnp_chan *cmd_chan);
void nnp_chan_disconnect(struct nnp_chan *cmd_chan);

int nnp_chan_create_file(struct nnp_chan *cmd_chan);
int nnp_chan_send_destroy(struct nnp_chan *chan);
bool nnp_chan_set_destroyed(struct nnp_chan *chan);

int nnp_chan_add_response(struct nnp_chan *cmd_chan, u64 *hw_msg, u32 size);

#endif
