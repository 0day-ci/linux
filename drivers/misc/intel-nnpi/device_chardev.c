// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#define pr_fmt(fmt)   KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include <uapi/misc/intel_nnpi.h>

#include "cmd_chan.h"
#include "device_chardev.h"
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

