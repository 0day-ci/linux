// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/anon_inodes.h>
#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/file.h>
#include <linux/minmax.h>
#include <linux/poll.h>
#include <linux/slab.h>

#include "cmd_chan.h"
#include "host_chardev.h"
#include "ipc_c2h_events.h"
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

static inline void respq_unpop(struct nnp_chan *chan, int count)
{
	chan->respq.tail = (chan->respq.tail - count) & (chan->respq_size - 1);
}

enum respq_state {
	RESPQ_EMPTY = 0,
	RESPQ_MSG_AVAIL,
	RESPQ_DISCONNECTED
};

/**
 * respq_state() - check if a response message is available to be popped
 * @chan: the cmd_chan object
 *
 * Checks if new response message is available or channel has been destroyed.
 *
 * Return:
 *  * RESPQ_EMPTY        - response queue is empty
 *  * RESPQ_MSG_AVAIL    - a response message is available in the queue
 *  * RESPQ_DISCONNECTED - the channel in a destroyed state
 */
static enum respq_state respq_state(struct nnp_chan *chan)
{
	bool ret;

	mutex_lock(&chan->dev_mutex);
	if (chan->state == NNP_CHAN_DESTROYED) {
		mutex_unlock(&chan->dev_mutex);
		return RESPQ_DISCONNECTED;
	}

	spin_lock(&chan->respq_lock);
	/*
	 * response messages are pushed into the respq ring-buffer by pushing
	 * the size of the message (as u32) followed by message content.
	 * So an entire message is available only if more than sizeof(u32)
	 * bytes are available (there is no message with zero size).
	 */
	if (CIRC_CNT(chan->respq.head, chan->respq.tail, chan->respq_size) >
	    sizeof(u32))
		ret = RESPQ_MSG_AVAIL;
	else
		ret = RESPQ_EMPTY;
	spin_unlock(&chan->respq_lock);

	mutex_unlock(&chan->dev_mutex);

	return ret;
}

static inline int is_cmd_chan_file(struct file *f);

static int cmd_chan_file_release(struct inode *inode, struct file *f)
{
	struct nnp_chan *chan = f->private_data;
	struct file *host_file;

	if (!is_cmd_chan_file(f))
		return -EINVAL;

	nnp_chan_send_destroy(chan);

	host_file = chan->host_file;
	nnp_chan_put(chan);
	fput(host_file);

	return 0;
}

/**
 * cmd_chan_file_read() - reads a single response message arrived from device
 * @f: cmd_chan file descriptor
 * @buf: buffer to receive the message
 * @size: size of buf, must be at least 16 qwords (16 * sizeof(u64))
 * @off: ignored.
 *
 * This function will block and wait until interrupted or a response
 * message from device is available.
 * When message(s) are available, it reads a single message, copy it to
 * @buf and returns the message size.
 * The given @buf and @size must be large enough to receive the largest
 * possible response which is 16 qwords, otherwise -EINVAL is returned.
 * The function returns the size of the received message, a return value
 * of zero means that corrupted message has been detected and no more reads
 * can be made from this channel.
 *
 * Return: if positive, the size in bytes of the read message.
 *         zero, if a corrupted message has been detected.
 *         error code otherwise
 */
static ssize_t cmd_chan_file_read(struct file *f, char __user *buf, size_t size,
				  loff_t *off)
{
	struct nnp_chan *chan = f->private_data;
	u64 msg[NNP_DEVICE_RESPONSE_FIFO_LEN];
	enum respq_state state;
	u32 msg_size;
	int ret;

	if (!is_cmd_chan_file(f))
		return -EINVAL;

	if (size < sizeof(msg))
		return -EINVAL;

	/*
	 * wait for response message to be available, interrupted or channel
	 * has been destroyed on us.
	 */
	ret = wait_event_interruptible(chan->resp_waitq,
				       (state = respq_state(chan)) != RESPQ_EMPTY);
	if (ret < 0)
		return ret;

	if (state == RESPQ_DISCONNECTED)
		return -EPIPE;

	spin_lock(&chan->respq_lock);
	respq_pop(chan, &msg_size, sizeof(msg_size));
	/*
	 * Check msg_size does not overrun msg size.
	 * This will never happen unless the response ring buffer got
	 * corrupted in some way.
	 * We detect it here for safety and return zero
	 */
	if (msg_size > sizeof(msg)) {
		/*
		 * unpop the bad size to let subsequent read attempts
		 * to fail as well.
		 */
		respq_unpop(chan, sizeof(msg_size));
		spin_unlock(&chan->respq_lock);
		return 0;
	}
	respq_pop(chan, msg, msg_size);
	spin_unlock(&chan->respq_lock);

	if (copy_to_user(buf, msg, msg_size))
		return -EFAULT;

	return (ssize_t)msg_size;
}

/**
 * cmd_chan_file_write() - schedule a command message to be sent to the device.
 * @f: a cmd_chan file descriptor
 * @buf: the command message content
 * @size: size in bytes of the message, must be multiple of 8 and not larger
 *        than 3 qwords.
 * @off: ignored
 *
 * This function reads a command message from buffer and puts it in the
 * channel's message queue to schedule it to be delivered to the device.
 * The function returns when the message is copied to the message scheduler
 * queue without waiting for it to be sent out.
 * A valid command message size must be qword aligned and not larger than
 * the maximum size the message scheduler support, which is 3 qwords.
 *
 * The function also validate the command content and fail if the chan_id
 * field of the command header does not belong to the same channel of this
 * file descriptor, or the command opcode is out of range, or the command
 * size does not fit the size of this opcode.
 *
 * Return: the size of the message written or error code.
 */
static ssize_t cmd_chan_file_write(struct file *f, const char __user *buf,
				   size_t size, loff_t *off)
{
	struct nnp_chan *chan = f->private_data;
	u64 msg[MSG_SCHED_MAX_MSG_SIZE];
	unsigned int chan_id, opcode;
	unsigned int op;
	int rc = 0;

	if (!is_cmd_chan_file(f))
		return -EINVAL;

	/*
	 * size must be positive, multiple of 8 bytes and
	 * cannot exceed maximum message size
	 */
	if (!size || size > sizeof(msg) || (size &  0x7) != 0)
		return -EINVAL;

	if (copy_from_user(msg, buf, size))
		return -EFAULT;

	/*
	 * Check chan_id, opcode and message size are valid
	 */
	opcode = FIELD_GET(NNP_H2C_CHAN_MSG_OP_MASK, msg[0]);
	chan_id = FIELD_GET(NNP_H2C_CHAN_MSG_CHAN_ID_MASK, msg[0]);
	if (chan_id != chan->chan_id)
		return -EINVAL;
	if (opcode < NNP_IPC_MIN_USER_OP)
		return -EINVAL;
	op = opcode - NNP_IPC_MIN_USER_OP;

	mutex_lock(&chan->dev_mutex);
	if (!chan->nnpdev) {
		/* The device was removed */
		mutex_unlock(&chan->dev_mutex);
		return -EPIPE;
	}
	if (size != chan->nnpdev->ipc_chan_cmd_op_size[op] * 8) {
		mutex_unlock(&chan->dev_mutex);
		return -EINVAL;
	}

	if (!is_card_fatal_drv_event(chan_broken(chan)))
		rc  = nnp_msched_queue_add_msg(chan->cmdq, msg, size / 8);
	mutex_unlock(&chan->dev_mutex);

	if (rc < 0)
		return rc;

	return size;
}

static unsigned int cmd_chan_file_poll(struct file *f, struct poll_table_struct *pt)
{
	struct nnp_chan *chan = f->private_data;
	unsigned int mask = POLLOUT | POLLWRNORM;
	enum respq_state state;

	if (!is_cmd_chan_file(f))
		return 0;

	poll_wait(f, &chan->resp_waitq, pt);
	state = respq_state(chan);
	if (state != RESPQ_EMPTY)
		mask |= POLLIN | POLLRDNORM;
	if (state == RESPQ_DISCONNECTED)
		mask |= POLLHUP;

	return mask;
}

static const struct file_operations nnp_chan_fops = {
	.owner = THIS_MODULE,
	.release = cmd_chan_file_release,
	.read = cmd_chan_file_read,
	.write = cmd_chan_file_write,
	.poll = cmd_chan_file_poll,
};

static inline int is_cmd_chan_file(struct file *f)
{
	return f->f_op == &nnp_chan_fops;
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
	cmd_chan->fd = -1;
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

	/*
	 * If a chan file was created (through nnp_chan_create_file),
	 * the host_file was already put when the file has released, otherwise
	 * we put it here.
	 * This is because we want to release host_file once the channel file
	 * has been closed even though the channel object may continue to exist
	 * until the card will send a respond that it was destroyed.
	 */
	if (cmd_chan->fd < 0)
		fput(cmd_chan->host_file);

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

int nnp_chan_create_file(struct nnp_chan *cmd_chan)
{
	/*
	 * get refcount to the channel that will drop when
	 * the file is released.
	 */
	nnp_chan_get(cmd_chan);

	cmd_chan->fd = anon_inode_getfd("nnpi_chan", &nnp_chan_fops, cmd_chan,
					O_RDWR | O_CLOEXEC);
	if (cmd_chan->fd < 0)
		nnp_chan_put(cmd_chan);

	return cmd_chan->fd;
}

/**
 * nnp_chan_set_destroyed() - atomically mark the channel "destroyed"
 * @chan: the cmd_chan
 *
 * This function sets the command channel state to "destroyed" and returns
 * the previous destroyed state.
 * This function should be called once the channel has been destructed on the
 * device and a "channel destroyed" response message arrived.
 *
 * Return: true if the channel was already marked destroyed.
 */
bool nnp_chan_set_destroyed(struct nnp_chan *chan)
{
	bool ret;

	mutex_lock(&chan->dev_mutex);
	ret = (chan->state == NNP_CHAN_DESTROYED);
	chan->state = NNP_CHAN_DESTROYED;
	mutex_unlock(&chan->dev_mutex);

	wake_up_all(&chan->resp_waitq);

	return ret;
}

/**
 * nnp_chan_send_destroy() - sends a "destroy channel" command to device
 * @chan: the cmd_chan to destroy.
 *
 * This function sends a command to the device to destroy a command channel,
 * The channel object remains to exist, it will be dropped only when the device
 * send back a "channel destroyed" response message.
 * In case the device is in critical error state, we treat it as not
 * functional, and the function will immediately drop the channel object without
 * sending any command and will return with success.
 *
 * Return: 0 on success, error value otherwise.
 */
int nnp_chan_send_destroy(struct nnp_chan *chan)
{
	u64 cmd;
	int ret = 0;
	bool do_put = false;

	mutex_lock(&chan->dev_mutex);
	if (chan->state == NNP_CHAN_DESTROYED || !chan->nnpdev)
		goto done;

	cmd = FIELD_PREP(NNP_H2C_OP_MASK, NNP_IPC_H2C_OP_CHANNEL_OP);
	cmd |= FIELD_PREP(NNP_H2C_CHANNEL_OP_CHAN_ID_MASK, chan->chan_id);
	cmd |= FIELD_PREP(NNP_H2C_CHANNEL_OP_DESTROY_MASK, 1);

	chan->event_msg = 0;

	/*
	 * If card is in critical state (or was during the channel lifetime)
	 * we destroy the channel.
	 * otherwise, we send a destroy command to card and will destroy when
	 * the destroy reply arrives.
	 */
	if (is_card_fatal_drv_event(chan_broken(chan))) {
		chan->state = NNP_CHAN_DESTROYED;
		do_put = true;
		goto done;
	}

	ret = nnp_msched_queue_msg(chan->cmdq, cmd);

done:
	mutex_unlock(&chan->dev_mutex);
	if (do_put) {
		wake_up_all(&chan->resp_waitq);
		nnp_chan_put(chan);
	}
	return ret;
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
	cmd_chan->state = NNP_CHAN_DESTROYED;
	mutex_unlock(&cmd_chan->dev_mutex);

	/*
	 * If the channel is not in critical state,
	 * put it in critical state and wake any user
	 * which might wait for the device.
	 */
	if (!chan_drv_fatal(cmd_chan)) {
		cmd_chan->card_critical_error_msg = FIELD_PREP(NNP_C2H_EVENT_REPORT_CODE_MASK,
							       NNP_IPC_ERROR_CHANNEL_KILLED);
		wake_up_all(&nnpdev->waitq);
	}

	wake_up_all(&cmd_chan->resp_waitq);
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
