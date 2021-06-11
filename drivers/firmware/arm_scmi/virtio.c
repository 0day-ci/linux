// SPDX-License-Identifier: GPL-2.0
/*
 * Virtio Transport driver for Arm System Control and Management Interface
 * (SCMI).
 *
 * Copyright (C) 2020 OpenSynergy.
 */

/**
 * DOC: Theory of Operation
 *
 * The scmi-virtio transport implements a driver for the virtio SCMI device.
 *
 * There is one Tx channel (virtio cmdq, A2P channel) and at most one Rx
 * channel (virtio eventq, P2A channel). Each channel is implemented through a
 * virtqueue. Access to each virtqueue is protected by spinlocks.
 */

#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_ids.h>
#include <uapi/linux/virtio_scmi.h>

#include "common.h"

#define VIRTIO_SCMI_MAX_MSG_SIZE 128 /* Value may be increased. */
#define VIRTIO_SCMI_MAX_PDU_SIZE \
	(VIRTIO_SCMI_MAX_MSG_SIZE + SCMI_MSG_MAX_PROT_OVERHEAD)
#define DESCRIPTORS_PER_TX_MSG 2

/**
 * struct scmi_vio_channel - Transport channel information
 *
 * @lock: Protects access to all members except ready.
 * @ready_lock: Protects access to ready. If required, it must be taken before
 *              lock.
 * @vqueue: Associated virtqueue
 * @cinfo: SCMI Tx or Rx channel
 * @free_list: List of unused scmi_vio_msg, maintained for Tx channels only
 * @is_rx: Whether channel is an Rx channel
 * @ready: Whether transport user is ready to hear about channel
 */
struct scmi_vio_channel {
	spinlock_t lock;
	spinlock_t ready_lock;
	struct virtqueue *vqueue;
	struct scmi_chan_info *cinfo;
	struct list_head free_list;
	u8 is_rx;
	u8 ready;
};

/**
 * struct scmi_vio_msg - Transport PDU information
 *
 * @request: SDU used for commands
 * @input: SDU used for (delayed) responses and notifications
 * @list: List which scmi_vio_msg may be part of
 * @rx_len: Input SDU size in bytes, once input has been received
 */
struct scmi_vio_msg {
	struct scmi_msg_payld *request;
	struct scmi_msg_payld *input;
	struct list_head list;
	unsigned int rx_len;
};

static bool scmi_vio_have_vq_rx(struct virtio_device *vdev)
{
	return virtio_has_feature(vdev, VIRTIO_SCMI_F_P2A_CHANNELS);
}

static int scmi_vio_feed_vq_rx(struct scmi_vio_channel *vioch,
			       struct scmi_vio_msg *msg)
{
	struct scatterlist sg_in;
	int rc;
	unsigned long flags;

	sg_init_one(&sg_in, msg->input, VIRTIO_SCMI_MAX_PDU_SIZE);

	spin_lock_irqsave(&vioch->lock, flags);

	rc = virtqueue_add_inbuf(vioch->vqueue, &sg_in, 1, msg, GFP_ATOMIC);
	if (rc)
		dev_err_once(vioch->cinfo->dev,
			     "%s() failed to add to virtqueue (%d)\n", __func__,
			     rc);
	else
		virtqueue_kick(vioch->vqueue);

	spin_unlock_irqrestore(&vioch->lock, flags);

	return rc;
}

static void scmi_vio_complete_cb(struct virtqueue *vqueue)
{
	unsigned long ready_flags;
	unsigned long flags;
	unsigned int length;
	struct scmi_vio_channel *vioch;
	struct scmi_vio_msg *msg;
	bool cb_enabled = true;

	if (WARN_ON_ONCE(!vqueue->vdev->priv))
		return;
	vioch = &((struct scmi_vio_channel *)vqueue->vdev->priv)[vqueue->index];

	for (;;) {
		spin_lock_irqsave(&vioch->ready_lock, ready_flags);

		if (!vioch->ready) {
			if (!cb_enabled)
				(void)virtqueue_enable_cb(vqueue);
			goto unlock_ready_out;
		}

		spin_lock_irqsave(&vioch->lock, flags);
		if (cb_enabled) {
			virtqueue_disable_cb(vqueue);
			cb_enabled = false;
		}
		msg = virtqueue_get_buf(vqueue, &length);
		if (!msg) {
			if (virtqueue_enable_cb(vqueue))
				goto unlock_out;
			else
				cb_enabled = true;
		}
		spin_unlock_irqrestore(&vioch->lock, flags);

		if (msg) {
			msg->rx_len = length;

			/*
			 * Hold the ready_lock during the callback to avoid
			 * races when the arm-scmi driver is unbinding while
			 * the virtio device is not quiesced yet.
			 */
			scmi_rx_callback(vioch->cinfo,
					 msg_read_header(msg->input), msg);
		}
		spin_unlock_irqrestore(&vioch->ready_lock, ready_flags);
	}

unlock_out:
	spin_unlock_irqrestore(&vioch->lock, flags);
unlock_ready_out:
	spin_unlock_irqrestore(&vioch->ready_lock, ready_flags);
}

static const char *const scmi_vio_vqueue_names[] = { "tx", "rx" };

static vq_callback_t *scmi_vio_complete_callbacks[] = {
	scmi_vio_complete_cb,
	scmi_vio_complete_cb
};

static unsigned int virtio_get_max_msg(bool tx,
				       struct scmi_chan_info *base_cinfo)
{
	struct scmi_vio_channel *vioch = base_cinfo->transport_info;
	unsigned int ret;

	ret = virtqueue_get_vring_size(vioch->vqueue);

	/* Tx messages need multiple descriptors. */
	if (tx)
		ret /= DESCRIPTORS_PER_TX_MSG;

	if (ret > MSG_TOKEN_MAX) {
		dev_info_once(
			base_cinfo->dev,
			"Only %ld messages can be pending simultaneously, while the %s virtqueue could hold %d\n",
			MSG_TOKEN_MAX, tx ? "tx" : "rx", ret);
		ret = MSG_TOKEN_MAX;
	}

	return ret;
}

static int scmi_vio_match_any_dev(struct device *dev, const void *data)
{
	return 1;
}

static struct virtio_driver virtio_scmi_driver; /* Forward declaration */

static int virtio_link_supplier(struct device *dev)
{
	struct device *vdev = driver_find_device(
		&virtio_scmi_driver.driver, NULL, NULL, scmi_vio_match_any_dev);

	if (!vdev) {
		dev_notice_once(
			dev,
			"Deferring probe after not finding a bound scmi-virtio device\n");
		return -EPROBE_DEFER;
	}

	/* Add device link for remove order and sysfs link. */
	if (!device_link_add(dev, vdev, DL_FLAG_AUTOREMOVE_CONSUMER)) {
		put_device(vdev);
		dev_err(dev, "Adding link to supplier virtio device failed\n");
		return -ECANCELED;
	}

	put_device(vdev);
	return scmi_set_transport_info(dev, dev_to_virtio(vdev));
}

static bool virtio_chan_available(struct device *dev, int idx)
{
	struct virtio_device *vdev;

	/* scmi-virtio doesn't support per-protocol channels */
	if (is_scmi_protocol_device(dev))
		return false;

	vdev = scmi_get_transport_info(dev);
	if (!vdev)
		return false;

	switch (idx) {
	case VIRTIO_SCMI_VQ_TX:
		return true;
	case VIRTIO_SCMI_VQ_RX:
		return scmi_vio_have_vq_rx(vdev);
	default:
		return false;
	}
}

static int virtio_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			     bool tx)
{
	unsigned long flags;
	struct virtio_device *vdev;
	struct scmi_vio_channel *vioch;
	int index = tx ? VIRTIO_SCMI_VQ_TX : VIRTIO_SCMI_VQ_RX;
	int max_msg;
	int i;

	if (!virtio_chan_available(dev, index))
		return -ENODEV;

	vdev = scmi_get_transport_info(dev);
	vioch = &((struct scmi_vio_channel *)vdev->priv)[index];

	spin_lock_irqsave(&vioch->lock, flags);
	cinfo->transport_info = vioch;
	vioch->cinfo = cinfo;
	spin_unlock_irqrestore(&vioch->lock, flags);

	max_msg = virtio_get_max_msg(tx, cinfo);

	for (i = 0; i < max_msg; i++) {
		struct scmi_vio_msg *msg;

		msg = devm_kzalloc(cinfo->dev, sizeof(*msg), GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		if (tx) {
			msg->request = devm_kzalloc(cinfo->dev,
						    VIRTIO_SCMI_MAX_PDU_SIZE,
						    GFP_KERNEL);
			if (!msg->request)
				return -ENOMEM;
		}

		msg->input = devm_kzalloc(cinfo->dev, VIRTIO_SCMI_MAX_PDU_SIZE,
					  GFP_KERNEL);
		if (!msg->input)
			return -ENOMEM;

		if (tx) {
			spin_lock_irqsave(&vioch->lock, flags);
			list_add_tail(&msg->list, &vioch->free_list);
			spin_unlock_irqrestore(&vioch->lock, flags);
		} else {
			scmi_vio_feed_vq_rx(vioch, msg);
		}
	}

	spin_lock_irqsave(&vioch->ready_lock, flags);
	vioch->ready = true;
	spin_unlock_irqrestore(&vioch->ready_lock, flags);

	return 0;
}

static int virtio_chan_free(int id, void *p, void *data)
{
	unsigned long flags;
	struct scmi_chan_info *cinfo = p;
	struct scmi_vio_channel *vioch = cinfo->transport_info;

	spin_lock_irqsave(&vioch->ready_lock, flags);
	vioch->ready = false;
	spin_unlock_irqrestore(&vioch->ready_lock, flags);

	scmi_free_channel(cinfo, data, id);
	return 0;
}

static int virtio_send_message(struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer)
{
	struct scmi_vio_channel *vioch = cinfo->transport_info;
	struct scatterlist sg_out;
	struct scatterlist sg_in;
	struct scatterlist *sgs[DESCRIPTORS_PER_TX_MSG] = { &sg_out, &sg_in };
	unsigned long flags;
	int rc;
	struct scmi_vio_msg *msg;

	/*
	 * TODO: For now, we don't support polling. But it should not be
	 * difficult to add support.
	 */
	if (xfer->hdr.poll_completion)
		return -EINVAL;

	spin_lock_irqsave(&vioch->lock, flags);

	if (list_empty(&vioch->free_list)) {
		spin_unlock_irqrestore(&vioch->lock, flags);
		return -EBUSY;
	}

	msg = list_first_entry(&vioch->free_list, typeof(*msg), list);
	list_del(&msg->list);

	msg_tx_prepare(msg->request, xfer);

	sg_init_one(&sg_out, msg->request, msg_command_size(xfer));
	sg_init_one(&sg_in, msg->input, msg_response_size(xfer));

	rc = virtqueue_add_sgs(vioch->vqueue, sgs, 1, 1, msg, GFP_ATOMIC);
	if (rc) {
		list_add(&msg->list, &vioch->free_list);
		dev_err_once(vioch->cinfo->dev,
			     "%s() failed to add to virtqueue (%d)\n", __func__,
			     rc);
	} else {
		virtqueue_kick(vioch->vqueue);
	}

	spin_unlock_irqrestore(&vioch->lock, flags);

	return rc;
}

static void virtio_fetch_response(struct scmi_chan_info *cinfo,
				  struct scmi_xfer *xfer, void *msg_handle)
{
	struct scmi_vio_msg *msg = msg_handle;
	struct scmi_vio_channel *vioch = cinfo->transport_info;

	if (!msg) {
		dev_dbg_once(&vioch->vqueue->vdev->dev,
			     "Ignoring %s() call with NULL msg_handle\n",
			     __func__);
		return;
	}

	msg_fetch_response(msg->input, msg->rx_len, xfer);
}

static void virtio_fetch_notification(struct scmi_chan_info *cinfo,
				      size_t max_len, struct scmi_xfer *xfer,
				      void *msg_handle)
{
	struct scmi_vio_msg *msg = msg_handle;
	struct scmi_vio_channel *vioch = cinfo->transport_info;

	if (!msg) {
		dev_dbg_once(&vioch->vqueue->vdev->dev,
			     "Ignoring %s() call with NULL msg_handle\n",
			     __func__);
		return;
	}

	msg_fetch_notification(msg->input, msg->rx_len, max_len, xfer);
}

static void dummy_clear_channel(struct scmi_chan_info *cinfo)
{
}

static bool dummy_poll_done(struct scmi_chan_info *cinfo,
			    struct scmi_xfer *xfer)
{
	return false;
}

static void virtio_drop_message(struct scmi_chan_info *cinfo, void *msg_handle)
{
	unsigned long flags;
	struct scmi_vio_channel *vioch = cinfo->transport_info;
	struct scmi_vio_msg *msg = msg_handle;

	if (!msg) {
		dev_dbg_once(&vioch->vqueue->vdev->dev,
			     "Ignoring %s() call with NULL msg_handle\n",
			     __func__);
		return;
	}

	if (vioch->is_rx) {
		scmi_vio_feed_vq_rx(vioch, msg);
	} else {
		spin_lock_irqsave(&vioch->lock, flags);
		list_add(&msg->list, &vioch->free_list);
		spin_unlock_irqrestore(&vioch->lock, flags);
	}
}

static const struct scmi_transport_ops scmi_virtio_ops = {
	.link_supplier = virtio_link_supplier,
	.chan_available = virtio_chan_available,
	.chan_setup = virtio_chan_setup,
	.chan_free = virtio_chan_free,
	.get_max_msg = virtio_get_max_msg,
	.send_message = virtio_send_message,
	.fetch_response = virtio_fetch_response,
	.fetch_notification = virtio_fetch_notification,
	.clear_channel = dummy_clear_channel,
	.poll_done = dummy_poll_done,
	.drop_message = virtio_drop_message,
};

static int scmi_vio_probe(struct virtio_device *vdev)
{
	struct device *dev = &vdev->dev;
	struct scmi_vio_channel *channels;
	bool have_vq_rx;
	int vq_cnt;
	int i;
	int ret;
	struct virtqueue *vqs[VIRTIO_SCMI_VQ_MAX_CNT];

	have_vq_rx = scmi_vio_have_vq_rx(vdev);
	vq_cnt = have_vq_rx ? VIRTIO_SCMI_VQ_MAX_CNT : 1;

	channels = devm_kcalloc(dev, vq_cnt, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	if (have_vq_rx)
		channels[VIRTIO_SCMI_VQ_RX].is_rx = true;

	ret = virtio_find_vqs(vdev, vq_cnt, vqs, scmi_vio_complete_callbacks,
			      scmi_vio_vqueue_names, NULL);
	if (ret) {
		dev_err(dev, "Failed to get %d virtqueue(s)\n", vq_cnt);
		return ret;
	}
	dev_info(dev, "Found %d virtqueue(s)\n", vq_cnt);

	for (i = 0; i < vq_cnt; i++) {
		spin_lock_init(&channels[i].lock);
		spin_lock_init(&channels[i].ready_lock);
		INIT_LIST_HEAD(&channels[i].free_list);
		channels[i].vqueue = vqs[i];
	}

	vdev->priv = channels;

	return 0;
}

static void scmi_vio_remove(struct virtio_device *vdev)
{
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

static unsigned int features[] = {
	VIRTIO_SCMI_F_P2A_CHANNELS,
};

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SCMI, VIRTIO_DEV_ANY_ID },
	{ 0 }
};

static struct virtio_driver virtio_scmi_driver = {
	.driver.name = "scmi-virtio",
	.driver.owner = THIS_MODULE,
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.id_table = id_table,
	.probe = scmi_vio_probe,
	.remove = scmi_vio_remove,
};

static int __init virtio_scmi_init(void)
{
	return register_virtio_driver(&virtio_scmi_driver);
}

static void __exit virtio_scmi_exit(void)
{
	unregister_virtio_driver(&virtio_scmi_driver);
}

const struct scmi_desc scmi_virtio_desc = {
	.init = virtio_scmi_init,
	.exit = virtio_scmi_exit,
	.ops = &scmi_virtio_ops,
	.max_rx_timeout_ms = 60000, /* for non-realtime virtio devices */
	.max_msg = 0, /* overridden by virtio_get_max_msg() */
	.max_msg_size = VIRTIO_SCMI_MAX_MSG_SIZE,
};
