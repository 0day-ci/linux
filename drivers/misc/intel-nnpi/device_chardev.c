// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/dma-map-ops.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include <uapi/misc/intel_nnpi.h>

#include "cmd_chan.h"
#include "device_chardev.h"
#include "nnp_user.h"
#include "ipc_c2h_events.h"

static dev_t       devnum;
static struct class *class;

/**
 * struct device_client - structure for opened device char device file
 * @node: list node to include this struct in a list of clients
 *        (nnpdev->cdev_clients).
 * @nnpdev: the NNP-I device associated with the opened chardev
 * @mutex: protects @nnpdev
 *
 * NOTE: @nnpdev may become NULL if the underlying NNP-I device has removed.
 *       Any ioctl request on the char device in this state will fail with
 *       -ENODEV
 */
struct device_client {
	struct list_head  node;
	struct nnp_device *nnpdev;
	struct mutex      mutex;
};

/* protects nnpdev->cdev_clients list (for all nnp devices) */
static DEFINE_MUTEX(clients_mutex);

#define NNPDRV_DEVICE_DEV_NAME "nnpi"

static inline bool is_nnp_device_file(struct file *f);

static int nnp_device_open(struct inode *inode, struct file *f)
{
	struct device_client *client;
	struct nnp_device *nnpdev;

	if (!is_nnp_device_file(f))
		return -EINVAL;

	if (!inode->i_cdev)
		return -EINVAL;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	nnpdev = container_of(inode->i_cdev, struct nnp_device, cdev);
	client->nnpdev = nnpdev;
	mutex_init(&client->mutex);
	f->private_data = client;

	mutex_lock(&clients_mutex);
	list_add_tail(&client->node, &nnpdev->cdev_clients);
	mutex_unlock(&clients_mutex);

	return 0;
}

static void disconnect_client_locked(struct device_client *client)
{
	lockdep_assert_held(&clients_mutex);

	mutex_lock(&client->mutex);
	if (!client->nnpdev) {
		mutex_unlock(&client->mutex);
		return;
	}
	client->nnpdev = NULL;
	list_del(&client->node);
	mutex_unlock(&client->mutex);
}

static int nnp_device_release(struct inode *inode, struct file *f)
{
	struct device_client *client = f->private_data;

	if (!is_nnp_device_file(f))
		return -EINVAL;

	mutex_lock(&clients_mutex);
	disconnect_client_locked(client);
	mutex_unlock(&clients_mutex);
	kfree(client);
	f->private_data = NULL;

	return 0;
}

static int event_val_to_nnp_error(enum event_val event_val)
{
	switch (event_val) {
	case NNP_IPC_NO_ERROR:
		return 0;
	case NNP_IPC_NO_MEMORY:
		return -ENOMEM;
	default:
		return -EFAULT;
	}
}

static int send_create_chan_req(struct nnp_device *nnpdev, struct nnp_chan *chan)
{
	unsigned int event_code, event_val;
	u64 cmd;
	int ret;

	cmd = FIELD_PREP(NNP_H2C_OP_MASK, NNP_IPC_H2C_OP_CHANNEL_OP);
	cmd |= FIELD_PREP(NNP_H2C_CHANNEL_OP_CHAN_ID_MASK, chan->chan_id);
	cmd |= FIELD_PREP(NNP_H2C_CHANNEL_OP_UID_MASK, 0);
	cmd |= FIELD_PREP(NNP_H2C_CHANNEL_OP_PRIV_MASK, 1);

	ret = nnp_msched_queue_msg(nnpdev->cmdq, cmd);
	if (ret < 0)
		return NNPER_DEVICE_ERROR;

	/*
	 * wait until card has respond to the create request or fatal
	 * card error has been detected.
	 */
	wait_event(nnpdev->waitq, chan->event_msg || chan_drv_fatal(chan));
	if (!chan->event_msg)
		return NNPER_DEVICE_ERROR;

	event_code = FIELD_GET(NNP_C2H_EVENT_REPORT_CODE_MASK, chan->event_msg);
	event_val = FIELD_GET(NNP_C2H_EVENT_REPORT_VAL_MASK, chan->event_msg);
	if (event_code == NNP_IPC_CREATE_CHANNEL_FAILED)
		return event_val_to_nnp_error(event_val);

	return 0;
}

static long create_channel(struct device_client *cinfo, void __user *arg,
			   unsigned int size)
{
	struct nnp_device *nnpdev = cinfo->nnpdev;
	struct ioctl_nnpi_create_channel req;
	unsigned int io_size = sizeof(req);
	struct nnp_chan *chan;
	long ret = 0;
	u32 error_mask;

	/* only single size structure is currently supported */
	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&req, arg, io_size))
		return -EFAULT;

	/* o_errno must be cleared on entry */
	if (req.o_errno)
		return -EINVAL;

	if (req.i_max_id < req.i_min_id ||
	    req.i_max_id > NNP_MAX_CHANNEL_ID)
		return -EINVAL;

	/*
	 * Do not allow create command channel if device is in
	 * error state.
	 * However allow new non infer context channels in case
	 * of fatal ICE error in order to allow retrieve debug
	 * information.
	 */
	error_mask = NNP_DEVICE_ERROR_MASK;
	if (req.i_max_id > NNP_MAX_INF_CONTEXT_CHANNEL_ID)
		error_mask &= ~(NNP_DEVICE_FATAL_ICE_ERROR);

	if ((nnpdev->state & error_mask) ||
	    !(nnpdev->state & NNP_DEVICE_CARD_DRIVER_READY) ||
	    (req.i_max_id <= NNP_MAX_INF_CONTEXT_CHANNEL_ID &&
	     (nnpdev->state & NNP_DEVICE_ACTIVE_MASK) !=
	     NNP_DEVICE_ACTIVE_MASK)) {
		req.o_errno = NNPER_DEVICE_NOT_READY;
		goto done;
	}

	/* Validate channel protocol version */
	if (NNP_VERSION_MAJOR(req.i_protocol_version) !=
	    NNP_VERSION_MAJOR(nnpdev->chan_protocol_version) ||
	    NNP_VERSION_MINOR(req.i_protocol_version) !=
	    NNP_VERSION_MINOR(nnpdev->chan_protocol_version)) {
		req.o_errno = NNPER_VERSIONS_MISMATCH;
		goto done;
	}

	/* create the channel object */
	chan = nnpdev_chan_create(nnpdev, req.i_host_fd, req.i_min_id, req.i_max_id,
				  req.i_get_device_events);
	if (IS_ERR(chan)) {
		ret = PTR_ERR(chan);
		goto done;
	}

	/* create the channel on card */
	req.o_errno = send_create_chan_req(nnpdev, chan);
	if (req.o_errno)
		goto err_destroy;

	req.o_channel_id = chan->chan_id;

	/* Attach file descriptor to the channel object */
	req.o_fd = nnp_chan_create_file(chan);

	/* remove channel object if failed */
	if (req.o_fd < 0) {
		/* the channel already created on card - send a destroy request */
		nnp_chan_send_destroy(chan);
		ret = req.o_fd;
	}

	goto done;

err_destroy:
	/* the channel was not created on card - destroy it now */
	if (!nnp_chan_set_destroyed(chan))
		nnp_chan_put(chan);
done:
	if (!ret && copy_to_user(arg, &req, io_size))
		return -EFAULT;

	return ret;
}

/**
 * send_rb_op() - sends CHANNEL_RB_OP command and wait for reply
 * @chan: the command channel
 * @rb_op_cmd: the command to send
 * @o_errno: returns zero or error code from device
 *
 * The function sends a "ring buffer operation" command to the device
 * to either create or destroy a ring buffer object.
 * This is a synchronous operation, the function will wait until a response
 * from the device has arrived.
 * If some other synchronous ring buffer operation is already in progress on
 * the same channel, the function will fail.
 *
 * Return:
 * * -EBUSY: Ring-buffer create/destroy operation is already in-flight.
 * * -EPIPE: The channel is in critical error state or sending the command
 *           has failed.
 * * 0: The command has sent successfully, the operation status is updated
 *      in o_errno, if o_errno is zero, then the create/destoy operation has
 *      succeeded, otherwise it indicates an error code received from
 *      device.
 */
static int send_rb_op(struct nnp_chan *chan, u64 rb_op_cmd, __u32 *o_errno)
{
	struct nnp_device *nnpdev = chan->nnpdev;
	unsigned int event_code, event_val;
	int ret = -EPIPE;

	*o_errno = 0;

	mutex_lock(&chan->dev_mutex);
	if (chan->state == NNP_CHAN_RB_OP_IN_FLIGHT) {
		mutex_unlock(&chan->dev_mutex);
		return -EBUSY;
	} else if (chan->state == NNP_CHAN_DESTROYED) {
		mutex_unlock(&chan->dev_mutex);
		*o_errno = NNPER_DEVICE_ERROR;
		return 0;
	}
	chan->state = NNP_CHAN_RB_OP_IN_FLIGHT;
	mutex_unlock(&chan->dev_mutex);

	chan->event_msg = 0;

	/* send the command to card */
	if (!chan_drv_fatal(chan))
		ret = nnp_msched_queue_msg(nnpdev->cmdq, rb_op_cmd);

	if (ret < 0)
		goto done;

	/* wait until card respond or card critical error is detected */
	wait_event(nnpdev->waitq, chan->event_msg || chan_drv_fatal(chan));
	if (!chan->event_msg) {
		*o_errno = NNPER_DEVICE_ERROR;
		ret = 0;
		goto done;
	}

	event_code = FIELD_GET(NNP_C2H_EVENT_REPORT_CODE_MASK, chan->event_msg);
	if (event_code == NNP_IPC_CHANNEL_SET_RB_FAILED) {
		event_val = FIELD_GET(NNP_C2H_EVENT_REPORT_VAL_MASK, chan->event_msg);
		*o_errno = event_val_to_nnp_error(event_val);
		ret = 0;
	}

done:
	mutex_lock(&chan->dev_mutex);
	if (chan->state == NNP_CHAN_RB_OP_IN_FLIGHT)
		chan->state = NNP_CHAN_NORMAL;
	mutex_unlock(&chan->dev_mutex);
	return ret;
}

static long create_channel_data_ringbuf(struct device_client *cinfo,
					void __user *arg, unsigned int size)
{
	struct nnp_device *nnpdev = cinfo->nnpdev;
	struct ioctl_nnpi_create_channel_data_ringbuf req;
	struct user_hostres *hostres_entry = NULL;
	struct nnp_user_info *nnp_user = NULL;
	struct nnpdev_mapping *hostres_map;
	unsigned int io_size = sizeof(req);
	struct host_resource *hostres;
	struct nnp_chan *chan = NULL;
	unsigned long dma_pfn;
	dma_addr_t page_list;
	u64 rb_op_cmd;
	int ret = 0;

	/* only single size structure is currently supported */
	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&req, arg, io_size))
		return -EFAULT;

	if (req.i_id > NNP_IPC_MAX_CHANNEL_RB - 1)
		return -EINVAL;

	/* o_errno must be cleared on entry */
	if (req.o_errno)
		return -EINVAL;

	chan = nnpdev_find_channel(nnpdev, req.i_channel_id);
	if (!chan) {
		req.o_errno = NNPER_NO_SUCH_CHANNEL;
		goto done;
	}

	nnp_user = chan->nnp_user;
	mutex_lock(&nnp_user->mutex);
	hostres_entry = idr_find(&nnp_user->idr, req.i_hostres_handle);
	if (!hostres_entry) {
		req.o_errno = NNPER_NO_SUCH_RESOURCE;
		goto unlock_user;
	}

	hostres = hostres_entry->hostres;

	/* check the resource fit the direction */
	if ((req.i_h2c && !nnp_hostres_is_input(hostres)) ||
	    (!req.i_h2c && !nnp_hostres_is_output(hostres))) {
		req.o_errno = NNPER_INCOMPATIBLE_RESOURCES;
		goto unlock_user;
	}

	hostres_map = nnp_hostres_map_device(hostres, nnpdev, false, &page_list, NULL);
	if (IS_ERR(hostres_map)) {
		ret = -EFAULT;
		goto unlock_user;
	}

	/*
	 * Its OK to release the mutex here and let other
	 * thread destroy the hostres handle as we already
	 * mapped it (which ref counted)
	 */
	mutex_unlock(&nnp_user->mutex);

	dma_pfn = NNP_IPC_DMA_ADDR_TO_PFN(page_list);
	rb_op_cmd = FIELD_PREP(NNP_H2C_OP_MASK, NNP_IPC_H2C_OP_CHANNEL_RB_OP);
	rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_CHAN_ID_MASK, chan->chan_id);
	rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_ID_MASK, req.i_id);
	rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_HOST_PFN_MASK, dma_pfn);
	if (req.i_h2c)
		rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_H2C_MASK, 1);

	ret = send_rb_op(chan, rb_op_cmd, &req.o_errno);
	if (!ret && !req.o_errno)
		ret = nnp_chan_set_ringbuf(chan, req.i_h2c, req.i_id, hostres_map);

	if (ret || req.o_errno)
		nnp_hostres_unmap_device(hostres_map);

	goto put_chan;

unlock_user:
	mutex_unlock(&nnp_user->mutex);
put_chan:
	nnp_chan_put(chan);
done:
	if (!ret && copy_to_user(arg, &req, io_size))
		return -EFAULT;

	return ret;
}

static long destroy_channel_data_ringbuf(struct device_client *cinfo,
					 void __user *arg, unsigned int size)
{
	struct ioctl_nnpi_destroy_channel_data_ringbuf req;
	struct nnp_device *nnpdev = cinfo->nnpdev;
	unsigned int io_size = sizeof(req);
	struct nnp_chan *chan;
	u64 rb_op_cmd;
	int ret = 0;

	/* only single size structure is currently supported */
	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&req, arg, io_size))
		return -EFAULT;

	/* we have one bit in ipc protocol for ringbuf id for each direction */
	if (req.i_id > 1)
		return -EINVAL;

	/* o_errno must be cleared on entry */
	if (req.o_errno)
		return -EINVAL;

	chan = nnpdev_find_channel(nnpdev, req.i_channel_id);
	if (!chan) {
		req.o_errno = NNPER_NO_SUCH_CHANNEL;
		goto done;
	}

	rb_op_cmd = FIELD_PREP(NNP_H2C_OP_MASK, NNP_IPC_H2C_OP_CHANNEL_RB_OP);
	rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_CHAN_ID_MASK, chan->chan_id);
	rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_ID_MASK, req.i_id);
	rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_DESTROY_MASK, 1);
	if (req.i_h2c)
		rb_op_cmd |= FIELD_PREP(NNP_H2C_CHANNEL_RB_OP_H2C_MASK, 1);

	ret = send_rb_op(chan, rb_op_cmd, &req.o_errno);
	if (ret || req.o_errno)
		goto put_chan;

	ret = nnp_chan_set_ringbuf(chan, req.i_h2c, req.i_id, NULL);

put_chan:
	nnp_chan_put(chan);
done:
	if (!ret && copy_to_user(arg, &req, io_size))
		return -EFAULT;

	return ret;
}

static int send_map_hostres_req(struct nnp_device *nnpdev, struct nnp_chan *chan,
				struct chan_hostres_map *hostres_map, dma_addr_t page_list)
{
	unsigned int event_code, event_val;
	unsigned long dma_pfn;
	u64 cmd[2];

	dma_pfn = NNP_IPC_DMA_ADDR_TO_PFN(page_list);
	cmd[0] = FIELD_PREP(NNP_H2C_OP_MASK, NNP_IPC_H2C_OP_CHANNEL_HOSTRES_OP);
	cmd[0] |= FIELD_PREP(NNP_H2C_CHANNEL_HOSTRES_QW0_CHAN_ID_MASK,
			     chan->chan_id);
	cmd[0] |= FIELD_PREP(NNP_H2C_CHANNEL_HOSTRES_QW0_ID_MASK, hostres_map->id);
	cmd[1] = FIELD_PREP(NNP_H2C_CHANNEL_HOSTRES_QW1_HOST_PFN_MASK, dma_pfn);

	/* do not send the map command if the device in a fatal error state */
	if (chan_drv_fatal(chan))
		return NNPER_DEVICE_ERROR;

	/* send the hostres map command to card */
	if (nnp_msched_queue_msg(chan->cmdq, cmd) < 0)
		return NNPER_DEVICE_ERROR;

	/* wait until card respond or card critical error is detected */
	wait_event(nnpdev->waitq, hostres_map->event_msg || chan_drv_fatal(chan));

	if (!hostres_map->event_msg)
		return NNPER_DEVICE_ERROR;

	event_code = FIELD_GET(NNP_C2H_EVENT_REPORT_CODE_MASK, hostres_map->event_msg);
	if (event_code == NNP_IPC_CHANNEL_MAP_HOSTRES_FAILED) {
		event_val = FIELD_GET(NNP_C2H_EVENT_REPORT_VAL_MASK, hostres_map->event_msg);
		return event_val_to_nnp_error(event_val);
	}

	return 0;
}

static int do_map_hostres(struct nnp_device *nnpdev, struct nnp_chan   *chan,
			  unsigned long     hostres_handle)
{
	struct chan_hostres_map *hostres_map = NULL;
	struct user_hostres *hostres_entry = NULL;
	struct nnp_user_info *nnp_user;
	struct host_resource *hostres;
	dma_addr_t page_list;
	int map_id;
	int err;

	nnp_user = chan->nnp_user;
	mutex_lock(&nnp_user->mutex);
	hostres_entry = idr_find(&nnp_user->idr, hostres_handle);
	if (!hostres_entry) {
		err = -NNPER_NO_SUCH_RESOURCE;
		goto unlock_user;
	}
	hostres = hostres_entry->hostres;

	hostres_map = kzalloc(sizeof(*hostres_map), GFP_KERNEL);
	if (!hostres_map) {
		err = -ENOMEM;
		goto unlock_user;
	}

	mutex_lock(&chan->dev_mutex);
	map_id = ida_simple_get(&chan->hostres_map_ida, 0, U16_MAX, GFP_KERNEL);
	if (map_id < 0) {
		err = -ENOMEM;
		goto err_map;
	}

	hostres_map->hostres_map = nnp_hostres_map_device(hostres, nnpdev,
							  false, &page_list, NULL);
	if (IS_ERR(hostres_map->hostres_map)) {
		err = -EFAULT;
		goto err_ida;
	}

	hostres_map->event_msg = 0;
	hostres_map->id = map_id;

	hash_add(chan->hostres_hash, &hostres_map->hash_node, hostres_map->id);
	mutex_unlock(&chan->dev_mutex);
	mutex_unlock(&nnp_user->mutex);

	err = send_map_hostres_req(nnpdev, chan, hostres_map, page_list);
	if (err) {
		nnp_chan_unmap_hostres(chan, hostres_map->id);
		return err;
	}

	return map_id;

err_ida:
	ida_simple_remove(&chan->hostres_map_ida, map_id);
err_map:
	mutex_unlock(&chan->dev_mutex);
	kfree(hostres_map);
unlock_user:
	mutex_unlock(&nnp_user->mutex);
	return err;
}

static long map_hostres(struct device_client *cinfo, void __user *arg,
			unsigned int size)
{
	struct nnp_device *nnpdev = cinfo->nnpdev;
	struct ioctl_nnpi_channel_map_hostres req;
	unsigned int io_size = sizeof(req);
	const struct dma_map_ops *ops;
	struct nnp_chan *chan = NULL;
	int ret;

	/* only single size structure is currently supported */
	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&req, arg, io_size))
		return -EFAULT;

	chan = nnpdev_find_channel(nnpdev, req.i_channel_id);
	if (!chan) {
		req.o_errno = NNPER_NO_SUCH_CHANNEL;
		goto done;
	}

	ret = do_map_hostres(nnpdev, chan, req.i_hostres_handle);
	if (ret < 0) {
		req.o_errno = -ret;
		goto put_chan;
	}

	req.o_errno = 0;
	req.o_map_id = ret;

	ops = get_dma_ops(nnpdev->dev);
	if (ops)
		req.o_sync_needed = ops->sync_sg_for_cpu ? 1 : 0;
	else
		req.o_sync_needed =
			!dev_is_dma_coherent(nnpdev->dev);

	goto put_chan;

put_chan:
	nnp_chan_put(chan);
done:
	if (copy_to_user(arg, &req, io_size))
		return -EFAULT;

	return 0;
}

static long unmap_hostres(struct device_client *cinfo, void __user *arg,
			  unsigned int size)
{
	struct ioctl_nnpi_channel_unmap_hostres req;
	struct nnp_device *nnpdev = cinfo->nnpdev;
	struct chan_hostres_map *hostres_map;
	unsigned int io_size = sizeof(req);
	struct nnp_chan *chan = NULL;
	u64 cmd[2];
	long ret = 0;

	/* only single size structure is currently supported */
	if (size != io_size)
		return -EINVAL;

	if (copy_from_user(&req, arg, io_size))
		return -EFAULT;

	/* o_errno must be cleared on entry */
	if (req.o_errno)
		return -EINVAL;

	chan = nnpdev_find_channel(nnpdev, req.i_channel_id);
	if (!chan) {
		req.o_errno = NNPER_NO_SUCH_CHANNEL;
		goto done;
	}

	hostres_map = nnp_chan_find_map(chan, req.i_map_id);
	if (!hostres_map) {
		req.o_errno = NNPER_NO_SUCH_HOSTRES_MAP;
		goto put_chan;
	}

	cmd[0] = FIELD_PREP(NNP_H2C_OP_MASK, NNP_IPC_H2C_OP_CHANNEL_HOSTRES_OP);
	cmd[0] |= FIELD_PREP(NNP_H2C_CHANNEL_HOSTRES_QW0_CHAN_ID_MASK,
			     chan->chan_id);
	cmd[0] |= FIELD_PREP(NNP_H2C_CHANNEL_HOSTRES_QW0_ID_MASK, req.i_map_id);
	cmd[0] |= FIELD_PREP(NNP_H2C_CHANNEL_HOSTRES_QW0_UNMAP_MASK, 1);
	cmd[1] = 0;

	ret = nnp_msched_queue_msg(chan->cmdq, cmd);

put_chan:
	nnp_chan_put(chan);
done:
	if (!ret && copy_to_user(arg, &req, io_size))
		return -EFAULT;

	return ret;
}

static long nnp_device_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct device_client *client = f->private_data;
	unsigned int ioc_nr, size;
	long ret;

	if (!is_nnp_device_file(f))
		return -ENOTTY;

	if (_IOC_TYPE(cmd) != 'D')
		return -EINVAL;

	mutex_lock(&client->mutex);
	if (!client->nnpdev) {
		mutex_unlock(&client->mutex);
		return -ENODEV;
	}

	ioc_nr = _IOC_NR(cmd);
	size = _IOC_SIZE(cmd);

	switch (ioc_nr) {
	case _IOC_NR(IOCTL_NNPI_DEVICE_CREATE_CHANNEL):
		ret = create_channel(client, (void __user *)arg, size);
		break;
	case _IOC_NR(IOCTL_NNPI_DEVICE_CREATE_CHANNEL_RB):
		ret = create_channel_data_ringbuf(client, (void __user *)arg,
						  size);
		break;
	case _IOC_NR(IOCTL_NNPI_DEVICE_DESTROY_CHANNEL_RB):
		ret = destroy_channel_data_ringbuf(client, (void __user *)arg,
						   size);
		break;
	case _IOC_NR(IOCTL_NNPI_DEVICE_CHANNEL_MAP_HOSTRES):
		ret = map_hostres(client, (void __user *)arg, size);
		break;
	case _IOC_NR(IOCTL_NNPI_DEVICE_CHANNEL_UNMAP_HOSTRES):
		ret = unmap_hostres(client, (void __user *)arg, size);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&client->mutex);

	return ret;
}

static const struct file_operations nnp_device_fops = {
	.owner = THIS_MODULE,
	.open = nnp_device_open,
	.release = nnp_device_release,
	.unlocked_ioctl = nnp_device_ioctl,
	.compat_ioctl = nnp_device_ioctl,
};

static inline bool is_nnp_device_file(struct file *f)
{
	return f->f_op == &nnp_device_fops;
}

int nnpdev_cdev_create(struct nnp_device *nnpdev)
{
	int ret;

	INIT_LIST_HEAD(&nnpdev->cdev_clients);

	cdev_init(&nnpdev->cdev, &nnp_device_fops);
	nnpdev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&nnpdev->cdev, MKDEV(MAJOR(devnum), nnpdev->id), 1);
	if (ret)
		return ret;

	nnpdev->chardev = device_create(class, NULL, MKDEV(MAJOR(devnum), nnpdev->id),
					nnpdev, NNPI_DEVICE_DEV_FMT, nnpdev->id);
	if (IS_ERR(nnpdev->chardev)) {
		cdev_del(&nnpdev->cdev);
		return PTR_ERR(nnpdev->chardev);
	}

	return 0;
}

void nnpdev_cdev_destroy(struct nnp_device *nnpdev)
{
	struct device_client *client, *tmp;

	device_destroy(class, MKDEV(MAJOR(devnum), nnpdev->id));

	/* disconnect all chardev clients from the device */
	mutex_lock(&clients_mutex);
	list_for_each_entry_safe(client, tmp, &nnpdev->cdev_clients, node)
		disconnect_client_locked(client);
	mutex_unlock(&clients_mutex);

	cdev_del(&nnpdev->cdev);
}

int nnpdev_cdev_class_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&devnum, 0, NNP_MAX_DEVS,
				  NNPDRV_DEVICE_DEV_NAME);
	if (ret < 0)
		return ret;

	class = class_create(THIS_MODULE, NNPDRV_DEVICE_DEV_NAME);
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		unregister_chrdev_region(devnum, NNP_MAX_DEVS);
		return ret;
	}

	return 0;
}

void nnpdev_cdev_class_cleanup(void)
{
	class_destroy(class);
	unregister_chrdev_region(devnum, NNP_MAX_DEVS);
}

