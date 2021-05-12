// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/anon_inodes.h>
#include <linux/dev_printk.h>
#include <linux/file.h>
#include <linux/minmax.h>
#include <linux/slab.h>

#include "cmd_chan.h"
#include "host_chardev.h"
#include "ipc_protocol.h"
#include "nnp_user.h"

#define RESPQ_INIT_BUF_SIZE    SZ_2K   /* must be power of 2 */
#define RESPQ_MAX_BUF_SIZE     SZ_1M   /* must be power of 2 */

static inline int respq_free_bytes(struct nnp_chan *chan)
{
	return CIRC_SPACE(chan->respq.head, chan->respq.tail, chan->respq_size);
}

static inline void respq_push(struct nnp_chan *chan, void *buf, int count)
{
	char *dst = chan->respq.buf + chan->respq.head;
	int t = CIRC_SPACE_TO_END(chan->respq.head, chan->respq.tail,
				  chan->respq_size);

	if (t >= count) {
		memcpy(dst, buf, count);
	} else {
		memcpy(dst, buf, t);
		memcpy(chan->respq.buf, (u8 *)buf + t, count - t);
	}
	chan->respq.head = (chan->respq.head + count) & (chan->respq_size - 1);
}

static inline void respq_pop(struct nnp_chan *chan, void *buf, int count)
{
	char *src = chan->respq.buf + chan->respq.tail;
	int t = CIRC_CNT_TO_END(chan->respq.head, chan->respq.tail,
				chan->respq_size);

	if (t >= count) {
		memcpy(buf, src, count);
	} else {
		memcpy(buf, src, t);
		memcpy((u8 *)buf + t, chan->respq.buf, count - t);
	}
	chan->respq.tail = (chan->respq.tail + count) & (chan->respq_size - 1);
}

/**
 * nnpdev_chan_create() - creates a command channel object
 * @nnpdev: the device
 * @host_fd: opened file descriptor to "/dev/nnpi_host"
 * @min_id: minimum range for allocating ipc channel id for that channel
 * @max_id: maximum range for allocating ipc channel id for that channel
 * @get_device_events: true if this channel needs to receive device-level
 *                     responses (not originated to specific channel).
 *
 * This function create a "command channel" and assign it a unique id within
 * the range [@min_id..@max_id]. channels in id range [0, 255] are assumed to be
 * used for inference related operations and have slightly special semantics.
 *
 * Return: pointer to created channel or error.
 */
struct nnp_chan *nnpdev_chan_create(struct nnp_device *nnpdev, int host_fd,
				    unsigned int min_id, unsigned int max_id,
				    bool get_device_events)
{
	struct nnp_chan *cmd_chan;
	int chan_id;
	int ret;
	unsigned int max_proto_id = BIT(NNP_IPC_CHANNEL_BITS) - 1;

	if (min_id > max_proto_id)
		return ERR_PTR(-EINVAL);
	if (max_id > max_proto_id)
		max_id = max_proto_id;
	if (max_id < min_id)
		return ERR_PTR(-EINVAL);

	ret = ida_simple_get(&nnpdev->cmd_chan_ida, min_id, max_id + 1,
			     GFP_KERNEL);
	if (ret < 0)
		return ERR_PTR(ret);
	chan_id = ret;

	cmd_chan = kzalloc(sizeof(*cmd_chan), GFP_KERNEL);
	if (!cmd_chan) {
		ret = -ENOMEM;
		goto err_ida;
	}

	cmd_chan->respq_buf = kmalloc(RESPQ_INIT_BUF_SIZE, GFP_KERNEL);
	if (!cmd_chan->respq_buf) {
		ret = -ENOMEM;
		goto err_alloc;
	}
	cmd_chan->respq_size = RESPQ_INIT_BUF_SIZE;
	cmd_chan->respq.buf = cmd_chan->respq_buf;
	spin_lock_init(&cmd_chan->respq_lock);

	cmd_chan->host_file = nnp_host_file_get(host_fd);
	if (!cmd_chan->host_file) {
		ret = -EINVAL;
		goto err_respq;
	}

	cmd_chan->cmdq = nnp_msched_queue_create(nnpdev->cmdq_sched);
	if (!cmd_chan->cmdq) {
		ret = -ENOMEM;
		goto err_file_get;
	}

	kref_init(&cmd_chan->ref);
	cmd_chan->chan_id = chan_id;
	cmd_chan->nnpdev = nnpdev;
	cmd_chan->get_device_events = get_device_events;

	cmd_chan->nnp_user = cmd_chan->host_file->private_data;
	nnp_user_get(cmd_chan->nnp_user);

	init_waitqueue_head(&cmd_chan->resp_waitq);
	mutex_init(&cmd_chan->dev_mutex);

	/*
	 * Add channel to the channel hash
	 */
	spin_lock(&nnpdev->lock);
	hash_add(nnpdev->cmd_chan_hash, &cmd_chan->hash_node, cmd_chan->chan_id);

	spin_unlock(&nnpdev->lock);

	return cmd_chan;

err_file_get:
	fput(cmd_chan->host_file);
err_respq:
	kfree(cmd_chan->respq_buf);
err_alloc:
	kfree(cmd_chan);
err_ida:
	ida_simple_remove(&nnpdev->cmd_chan_ida, chan_id);
	return ERR_PTR(ret);
}

static void nnp_chan_release(struct kref *kref)
{
	struct nnp_chan *cmd_chan = container_of(kref, struct nnp_chan, ref);

	nnp_chan_disconnect(cmd_chan);

	nnp_user_put(cmd_chan->nnp_user);

	kfree(cmd_chan->respq_buf);
	kfree(cmd_chan);
}

void nnp_chan_get(struct nnp_chan *cmd_chan)
{
	kref_get(&cmd_chan->ref);
}

void nnp_chan_put(struct nnp_chan *cmd_chan)
{
	kref_put(&cmd_chan->ref, nnp_chan_release);
}

/**
 * nnp_chan_disconnect() - disconnect the channel from the NNP-I device object
 * @cmd_chan: the command channel object
 *
 * This function is called when the channel is released or the NNP-I device is
 * being removed. It disconnect the channel from the nnp_device object.
 * A disconnected channel can no longer become connected again and cannot
 * be used to communicate with any device.
 */
void nnp_chan_disconnect(struct nnp_chan *cmd_chan)
{
	struct nnp_device *nnpdev;

	mutex_lock(&cmd_chan->dev_mutex);
	if (!cmd_chan->nnpdev) {
		mutex_unlock(&cmd_chan->dev_mutex);
		return;
	}

	nnpdev = cmd_chan->nnpdev;
	cmd_chan->nnpdev = NULL;
	spin_lock(&nnpdev->lock);
	hash_del(&cmd_chan->hash_node);
	spin_unlock(&nnpdev->lock);
	mutex_unlock(&cmd_chan->dev_mutex);

	nnp_msched_queue_sync(cmd_chan->cmdq);
	nnp_msched_queue_destroy(cmd_chan->cmdq);

	ida_simple_remove(&nnpdev->cmd_chan_ida, cmd_chan->chan_id);
}

static int resize_respq(struct nnp_chan *cmd_chan)
{
	unsigned int avail_size;
	unsigned int new_size;
	char         *new_buf;

	new_size = min_t(unsigned int, cmd_chan->respq_size * 2, RESPQ_MAX_BUF_SIZE);

	/* do not try to resize if already in maximum size */
	if (new_size == cmd_chan->respq_size)
		return -ENOMEM;

	new_buf = kmalloc(new_size, GFP_KERNEL);
	if (!new_buf)
		return -ENOMEM;

	/* copy data from old to new ring buffer */
	spin_lock(&cmd_chan->respq_lock);
	avail_size = CIRC_CNT(cmd_chan->respq.head, cmd_chan->respq.tail,
			      cmd_chan->respq_size);
	if (avail_size > 0)
		respq_pop(cmd_chan, new_buf, avail_size);
	kfree(cmd_chan->respq_buf);
	cmd_chan->respq_buf = new_buf;
	cmd_chan->respq_size = new_size;
	cmd_chan->respq.buf = cmd_chan->respq_buf;
	cmd_chan->respq.tail = 0;
	cmd_chan->respq.head = avail_size;
	spin_unlock(&cmd_chan->respq_lock);
	dev_dbg(cmd_chan->nnpdev->dev, "channel respq resized to %d\n", new_size);

	return 0;
}

/**
 * try_add_response() - adds a response message to respq if enough space exist
 * @cmd_chan: the command channel object
 * @hw_msg: response message arrived from device
 * @size: size in bytes of the response
 *
 * Return: zero on success, -ENOSPC if message does not fit
 */
static int try_add_response(struct nnp_chan *cmd_chan, u64 *hw_msg, u32 size)
{
	spin_lock(&cmd_chan->respq_lock);

	/* Would the response fit in the buffer? */
	if (respq_free_bytes(cmd_chan) < size + sizeof(size)) {
		spin_unlock(&cmd_chan->respq_lock);
		return -ENOSPC;
	}

	/* push the response message to the ring buffer */
	respq_push(cmd_chan, &size, sizeof(size));
	respq_push(cmd_chan, hw_msg, size);

	spin_unlock(&cmd_chan->respq_lock);

	wake_up_all(&cmd_chan->resp_waitq);

	return 0;
}

/**
 * nnp_chan_add_response() - adds a response message targeting this channel
 * @cmd_chan: the command channel object
 * @hw_msg: response message arrived from device
 * @size: size in bytes of the response
 *
 * This function is being called when a response arrived from the NNP-I card
 * which targets to a specific command channel object.
 * The function puts the response message in a ring buffer and will later be
 * consumed by user space through a call to read(2) on the channel' file
 * descriptor.
 *
 * Return: error code or zero on success.
 */
int nnp_chan_add_response(struct nnp_chan *cmd_chan, u64 *hw_msg, u32 size)
{
	int ret;

retry:
	ret = try_add_response(cmd_chan, hw_msg, size);
	if (ret == -ENOSPC) {
		/*
		 * This should *rarely* happen in normal system
		 * operation since the ring-buffer is big enough.
		 * We will get here only if the user application sleeps
		 * for a *very* long time without draining the responses.
		 * Try to resize the response buffer when it does
		 * happen, but up to a maximum value.
		 * If resize failed, we have no choice but to lose the
		 * response. Only the application that uses that channel
		 * will get affected.
		 */
		ret = resize_respq(cmd_chan);
		if (!ret)
			goto retry;
	}

	if (ret) {
		if (!cmd_chan->resp_lost)
			dev_err_ratelimited(cmd_chan->nnpdev->dev,
					    "Response queue full for channel %d losing response!\n",
					    cmd_chan->chan_id);
		cmd_chan->resp_lost++;
	}

	return ret;
}
