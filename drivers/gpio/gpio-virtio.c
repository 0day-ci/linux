// SPDX-License-Identifier: GPL-2.0+

/*
 * GPIO driver for virtio-based virtual GPIOs
 *
 * Copyright (C) 2018 metux IT consult
 * Author: Enrico Weigelt, metux IT consult <info@metux.net>
 *
 */

#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_ids.h>
#include <uapi/linux/virtio_gpio.h>

#define CONFIG_VIRTIO_GPIO_MAX_IRQ		256

#define MSG_BUF_SZ	(sizeof(struct virtio_gpio_msg))

struct virtio_gpio_priv {
	struct gpio_chip gc;
	spinlock_t vq_lock;
	struct virtio_device *vdev;
	int num_gpios;
	char *name;
	struct virtqueue *vq_rx;
	struct virtqueue *vq_tx;
	struct virtio_gpio_msg last;
	wait_queue_head_t waitq;
	unsigned long reply_wait;
	struct irq_chip irq_chip;
	DECLARE_BITMAP(irq_mask, CONFIG_VIRTIO_GPIO_MAX_IRQ);
	unsigned int irq_parents;
	struct mutex rpc_mutex;
};

static int virtio_gpio_prepare_inbuf(struct virtio_gpio_priv *priv)
{
	struct scatterlist rcv_sg;
	struct virtio_gpio_msg *buf;

	buf = devm_kzalloc(&priv->vdev->dev, MSG_BUF_SZ, GFP_KERNEL);
	if (!buf) {
		dev_err(&priv->vdev->dev, "failed to allocate input buffer\n");
		return -ENOMEM;
	}

	sg_init_one(&rcv_sg, buf, sizeof(struct virtio_gpio_priv));
	virtqueue_add_inbuf(priv->vq_rx, &rcv_sg, 1, buf, GFP_KERNEL);
	virtqueue_kick(priv->vq_rx);

	return 0;
}

static int virtio_gpio_xmit(struct virtio_gpio_priv *priv, int type,
			    int pin, int value, struct virtio_gpio_msg *ev)
{
	struct scatterlist xmit_sg;
	int ret;
	unsigned long flags;

	ev->type = cpu_to_le16(type);
	ev->pin = cpu_to_le16(pin);
	ev->value = cpu_to_le32(value);

	sg_init_one(&xmit_sg, ev, MSG_BUF_SZ);
	spin_lock_irqsave(&priv->vq_lock, flags);
	ret = virtqueue_add_outbuf(priv->vq_tx, &xmit_sg, 1, priv, GFP_KERNEL);
	if (ret < 0) {
		dev_err(&priv->vdev->dev,
			"virtqueue_add_outbuf() failed: %d\n", ret);
		goto out;
	}
	virtqueue_kick(priv->vq_tx);

out:
	spin_unlock_irqrestore(&priv->vq_lock, flags);
	return ret;
}

static inline void wakeup_event(struct virtio_gpio_priv *priv, int id)
{
	set_bit(id, &priv->reply_wait);
}

static inline int check_event(struct virtio_gpio_priv *priv, int id)
{
	return test_bit(id, &priv->reply_wait);
}

static inline void clear_event(struct virtio_gpio_priv *priv, int id)
{
	clear_bit(id, &priv->reply_wait);
}

static int virtio_gpio_rpc(struct virtio_gpio_priv *priv, int type,
			   int pin, int value)
{
	int ret;
	struct virtio_gpio_msg *buf = kzalloc(MSG_BUF_SZ, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->rpc_mutex);
	virtio_gpio_prepare_inbuf(priv);
	clear_event(priv, type);

	ret = virtio_gpio_xmit(priv, type, pin, value, buf);
	if (ret)
		goto out;

	wait_event_interruptible(priv->waitq, check_event(priv, type));
	ret = priv->last.value;

out:
	mutex_unlock(&priv->rpc_mutex);
	kfree(buf);
	return ret;
}

static int virtio_gpio_direction_input(struct gpio_chip *gc,
				       unsigned int pin)
{
	return virtio_gpio_rpc(gpiochip_get_data(gc),
			       VIRTIO_GPIO_MSG_CPU_DIRECTION_INPUT,
			       pin, 0);
}

static int virtio_gpio_direction_output(struct gpio_chip *gc,
					unsigned int pin, int value)
{
	return virtio_gpio_rpc(gpiochip_get_data(gc),
			       VIRTIO_GPIO_MSG_CPU_DIRECTION_OUTPUT,
			       pin, value);
}

static int virtio_gpio_get_direction(struct gpio_chip *gc, unsigned int pin)
{
	return virtio_gpio_rpc(gpiochip_get_data(gc),
			       VIRTIO_GPIO_MSG_CPU_GET_DIRECTION,
			       pin, 0);
}

static void virtio_gpio_set(struct gpio_chip *gc,
			    unsigned int pin, int value)
{
	virtio_gpio_rpc(gpiochip_get_data(gc),
			VIRTIO_GPIO_MSG_CPU_SET_LEVEL, pin, value);
}

static int virtio_gpio_get(struct gpio_chip *gc,
			   unsigned int pin)
{
	return virtio_gpio_rpc(gpiochip_get_data(gc),
			       VIRTIO_GPIO_MSG_CPU_GET_LEVEL, pin, 0);
}

static int virtio_gpio_request(struct gpio_chip *gc,
			       unsigned int pin)
{
	return virtio_gpio_rpc(gpiochip_get_data(gc),
			       VIRTIO_GPIO_MSG_CPU_REQUEST, pin, 0);
}

static void virtio_gpio_signal(struct virtio_gpio_priv *priv, int event,
			       int pin, int value)
{
	int mapped_irq = irq_find_mapping(priv->gc.irq.domain, pin);

	if ((pin < priv->num_gpios) && test_bit(pin, priv->irq_mask))
		generic_handle_irq(mapped_irq);
}

static void virtio_gpio_data_rx(struct virtqueue *vq)
{
	struct virtio_gpio_priv *priv = vq->vdev->priv;
	void *data;
	unsigned int len;
	struct virtio_gpio_msg *ev;

	data = virtqueue_get_buf(priv->vq_rx, &len);
	if (!data || !len) {
		dev_warn(&vq->vdev->dev, "RX received no data ! %d\n", len);
		return;
	}

	ev = data;

	memcpy(&priv->last, data, MSG_BUF_SZ);

	ev->type  = le16_to_cpu(ev->type);
	ev->pin   = le16_to_cpu(ev->pin);
	ev->value = le32_to_cpu(ev->value);

	switch (ev->type) {
	case VIRTIO_GPIO_MSG_DEVICE_LEVEL:
		virtio_gpio_prepare_inbuf(priv);
		virtio_gpio_signal(priv, ev->type, ev->pin, ev->value);
		break;
	default:
		wakeup_event(priv, ev->type & ~VIRTIO_GPIO_MSG_REPLY);
		break;
	}

	wake_up_all(&priv->waitq);

	devm_kfree(&priv->vdev->dev, data);
}

static int virtio_gpio_alloc_vq(struct virtio_gpio_priv *priv)
{
	struct virtqueue *vqs[2];
	vq_callback_t *cbs[] = {
		NULL,
		virtio_gpio_data_rx,
	};
	static const char * const names[] = { "in", "out", };
	int ret;

	ret = virtio_find_vqs(priv->vdev, 2, vqs, cbs, names, NULL);
	if (ret) {
		dev_err(&priv->vdev->dev, "failed to alloc vqs: %d\n", ret);
		return ret;
	}

	priv->vq_rx = vqs[0];
	priv->vq_tx = vqs[1];

	ret = virtio_gpio_prepare_inbuf(priv);
	if (ret) {
		dev_err(&priv->vdev->dev, "preparing inbuf failed\n");
		return ret;
	}

	virtqueue_enable_cb(priv->vq_rx);
	virtio_device_ready(priv->vdev);

	return 0;
}

static void virtio_gpio_irq_unmask(struct irq_data *irq)
{
	int hwirq = irqd_to_hwirq(irq);
	struct virtio_gpio_priv *priv
		= gpiochip_get_data(irq_data_get_irq_chip_data(irq));
	if (hwirq < CONFIG_VIRTIO_GPIO_MAX_IRQ)
		set_bit(hwirq, priv->irq_mask);
}

static void virtio_gpio_irq_mask(struct irq_data *irq)
{
	int hwirq = irqd_to_hwirq(irq);
	struct virtio_gpio_priv *priv
		= gpiochip_get_data(irq_data_get_irq_chip_data(irq));
	if (hwirq < CONFIG_VIRTIO_GPIO_MAX_IRQ)
		clear_bit(hwirq, priv->irq_mask);
}

static int virtio_gpio_probe(struct virtio_device *vdev)
{
	struct virtio_gpio_priv *priv;
	struct virtio_gpio_config cf = {};
	char *name_buffer;
	const char **gpio_names = NULL;
	struct device *dev = &vdev->dev;
	struct gpio_irq_chip *girq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->name = devm_kzalloc(dev, sizeof(cf.name)+1, GFP_KERNEL);
	if (!priv->name)
		return -ENOMEM;

	spin_lock_init(&priv->vq_lock);
	mutex_init(&priv->rpc_mutex);

	virtio_cread_le(vdev, struct virtio_gpio_config, version, &cf.version);
	virtio_cread_le(vdev, struct virtio_gpio_config, num_gpios,
			&cf.num_gpios);
	virtio_cread_le(vdev, struct virtio_gpio_config, names_size,
			&cf.names_size);
	virtio_cread_bytes(vdev, offsetof(struct virtio_gpio_config, name),
			   priv->name, sizeof(cf.name));

	if (cf.version != 1) {
		dev_err(dev, "unsupported interface version %d\n", cf.version);
		return -EINVAL;
	}

	priv->num_gpios = cf.num_gpios;

	if (cf.names_size) {
		char *bufwalk;
		int idx = 0;

		name_buffer = devm_kzalloc(&vdev->dev, cf.names_size,
					   GFP_KERNEL)+1;
		virtio_cread_bytes(vdev, sizeof(struct virtio_gpio_config),
				   name_buffer, cf.names_size);
		name_buffer[cf.names_size] = 0;

		gpio_names = devm_kcalloc(dev, priv->num_gpios, sizeof(char *),
					  GFP_KERNEL);
		bufwalk = name_buffer;

		while (idx < priv->num_gpios &&
		       bufwalk < (name_buffer+cf.names_size)) {
			gpio_names[idx] = (strlen(bufwalk) ? bufwalk : NULL);
			bufwalk += strlen(bufwalk)+1;
			idx++;
		}
	}

	priv->vdev			= vdev;
	vdev->priv = priv;

	priv->gc.owner			= THIS_MODULE;
	priv->gc.parent			= dev;
	priv->gc.label			= (priv->name[0] ? priv->name
							 : dev_name(dev));
	priv->gc.ngpio			= priv->num_gpios;
	priv->gc.names			= gpio_names;
	priv->gc.base			= -1;
	priv->gc.request		= virtio_gpio_request;
	priv->gc.direction_input	= virtio_gpio_direction_input;
	priv->gc.direction_output	= virtio_gpio_direction_output;
	priv->gc.get_direction		= virtio_gpio_get_direction;
	priv->gc.get			= virtio_gpio_get;
	priv->gc.set			= virtio_gpio_set;
	priv->gc.can_sleep		= true;

	priv->irq_chip.name		= "virtio-gpio-irq";
	priv->irq_chip.irq_mask		= virtio_gpio_irq_mask;
	priv->irq_chip.irq_unmask	= virtio_gpio_irq_unmask;

	girq = &priv->gc.irq;

	priv->gc.irq.chip		= &priv->irq_chip;
	priv->gc.irq.num_parents	= 1;
	priv->gc.irq.default_type	= IRQ_TYPE_NONE;
	priv->gc.irq.handler		= handle_simple_irq;
	priv->gc.irq.parents		= &priv->irq_parents;
	priv->irq_parents		= 0;

	init_waitqueue_head(&priv->waitq);

	priv->reply_wait = 0;

	virtio_gpio_alloc_vq(priv);

	return devm_gpiochip_add_data(dev, &priv->gc, priv);
}

static void virtio_gpio_remove(struct virtio_device *vdev)
{
	/* just dummy, virtio subsys can't cope w/ NULL vector */
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPIO, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_gpio_driver = {
	.driver.name	= KBUILD_MODNAME,
	.driver.owner	= THIS_MODULE,
	.id_table	= id_table,
	.probe		= virtio_gpio_probe,
	.remove		= virtio_gpio_remove,
};

module_virtio_driver(virtio_gpio_driver);

MODULE_AUTHOR("Enrico Weigelt, metux IT consult <info@metux.net>");
MODULE_DESCRIPTION("VirtIO GPIO driver");
MODULE_LICENSE("GPL");
