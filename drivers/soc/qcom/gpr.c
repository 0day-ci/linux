// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/soc/qcom/gpr.h>
#include <linux/delay.h>
#include <linux/rpmsg.h>
#include <linux/of.h>

/* Some random values tbh which does not collide with static modules */
#define GPR_DYNAMIC_PORT_START	0x10000000
#define GPR_DYNAMIC_PORT_END	0x20000000

struct gpr_rx_buf {
	struct list_head node;
	int len;
	uint8_t buf[];
};

struct gpr {
	struct rpmsg_endpoint *ch;
	struct device *dev;
	spinlock_t ports_lock;
	spinlock_t rx_lock;
	struct idr ports_idr;
	int dest_domain_id;
	struct workqueue_struct *rxwq;
	struct work_struct rx_work;
	struct list_head rx_list;
};

struct gpr_pkt *gpr_alloc_pkt(struct gpr_port *port, int payload_size,
			      uint32_t opcode, uint32_t token,
			      uint32_t dest_port)
{
	int pkt_size = GPR_HDR_SIZE + payload_size;
	struct gpr *gpr = port->gpr;
	struct gpr_pkt *pkt;
	void *p;

	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	pkt = p;
	pkt->hdr.version = GPR_PKT_VER;
	pkt->hdr.hdr_size = GPR_PKT_HEADER_WORD_SIZE;
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.dest_port = dest_port;
	pkt->hdr.src_port = port->id;
	pkt->hdr.dest_domain = gpr->dest_domain_id;
	pkt->hdr.src_domain = GPR_DOMAIN_ID_APPS;
	pkt->hdr.token = token;
	pkt->hdr.opcode = opcode;

	return pkt;
}
EXPORT_SYMBOL_GPL(gpr_alloc_pkt);

void gpr_free_pkt(struct gpr_port *port, struct gpr_pkt *pkt)
{
	kfree(pkt);
}
EXPORT_SYMBOL_GPL(gpr_free_pkt);

int gpr_send_port_pkt(struct gpr_port *port, struct gpr_pkt *pkt)
{
	struct gpr *gpr = port->gpr;
	struct gpr_hdr *hdr;
	unsigned long flags;
	int ret;

	hdr = &pkt->hdr;

	spin_lock_irqsave(&port->lock, flags);
	ret = rpmsg_trysend(gpr->ch, pkt, hdr->pkt_size);
	spin_unlock_irqrestore(&port->lock, flags);

	return ret ? ret : hdr->pkt_size;
}
EXPORT_SYMBOL_GPL(gpr_send_port_pkt);

static void gpr_dev_release(struct device *dev)
{
	struct gpr_device *gdev = to_gpr_device(dev);

	kfree(gdev);
}

static int gpr_callback(struct rpmsg_device *rpdev, void *buf,
			int len, void *priv, u32 addr)
{
	struct gpr *gpr = dev_get_drvdata(&rpdev->dev);
	struct gpr_rx_buf *abuf;
	unsigned long flags;

	abuf = kzalloc(sizeof(*abuf) + len, GFP_ATOMIC);
	if (!abuf)
		return -ENOMEM;

	abuf->len = len;
	memcpy(abuf->buf, buf, len);

	spin_lock_irqsave(&gpr->rx_lock, flags);
	list_add_tail(&abuf->node, &gpr->rx_list);
	spin_unlock_irqrestore(&gpr->rx_lock, flags);

	queue_work(gpr->rxwq, &gpr->rx_work);

	return 0;
}

static int gpr_do_rx_callback(struct gpr *gpr, struct gpr_rx_buf *abuf)
{
	uint16_t hdr_size, ver;
	struct gpr_port *port = NULL;
	struct gpr_resp_pkt resp;
	struct gpr_hdr *hdr;
	unsigned long flags;
	void *buf = abuf->buf;
	int len = abuf->len;

	hdr = buf;
	ver = hdr->version;
	if (ver > GPR_PKT_VER + 1)
		return -EINVAL;

	hdr_size = hdr->hdr_size;
	if (hdr_size < GPR_PKT_HEADER_WORD_SIZE) {
		dev_err(gpr->dev, "GPR: Wrong hdr size:%d\n", hdr_size);
		return -EINVAL;
	}

	if (hdr->pkt_size < GPR_PKT_HEADER_BYTE_SIZE || hdr->pkt_size != len) {
		dev_err(gpr->dev, "GPR: Wrong packet size\n");
		return -EINVAL;
	}

	resp.hdr = *hdr;
	resp.payload_size = hdr->pkt_size - (hdr_size * 4);

	/*
	 * NOTE: hdr_size is not same as GPR_HDR_SIZE as remote can include
	 * optional headers in to gpr_hdr which should be ignored
	 */
	if (resp.payload_size > 0)
		resp.payload = buf + (hdr_size *  4);


	spin_lock_irqsave(&gpr->ports_lock, flags);
	port = idr_find(&gpr->ports_idr, hdr->dest_port);
	spin_unlock_irqrestore(&gpr->ports_lock, flags);

	if (!port) {
		dev_err(gpr->dev, "GPR: Port(%x) is not registered\n",
			hdr->dest_port);
		return -EINVAL;
	}

	if (port->callback)
		port->callback(&resp, port->priv, 0);

	return 0;
}

static void gpr_rxwq(struct work_struct *work)
{
	struct gpr *gpr = container_of(work, struct gpr, rx_work);
	struct gpr_rx_buf *abuf, *b;
	unsigned long flags;

	if (!list_empty(&gpr->rx_list)) {
		list_for_each_entry_safe(abuf, b, &gpr->rx_list, node) {
			gpr_do_rx_callback(gpr, abuf);
			spin_lock_irqsave(&gpr->rx_lock, flags);
			list_del(&abuf->node);
			spin_unlock_irqrestore(&gpr->rx_lock, flags);
			kfree(abuf);
		}
	}
}

static int gpr_device_match(struct device *dev, struct device_driver *drv)
{
	/* Attempt an OF style match first */
	if (of_driver_match_device(dev, drv))
		return 1;

	return 0;
}

static int gpr_device_probe(struct device *dev)
{
	struct gpr_device *gdev = to_gpr_device(dev);
	struct gpr_driver *adrv = to_gpr_driver(dev->driver);
	int ret;

	ret = adrv->probe(gdev);
	if (!ret)
		gdev->port.callback = adrv->callback;

	return ret;
}

static int gpr_device_remove(struct device *dev)
{
	struct gpr_device *gdev = to_gpr_device(dev);
	struct gpr_driver *adrv;
	struct gpr *gpr = dev_get_drvdata(gdev->dev.parent);

	if (dev->driver) {
		adrv = to_gpr_driver(dev->driver);
		if (adrv->remove)
			adrv->remove(gdev);
		spin_lock(&gpr->ports_lock);
		idr_remove(&gpr->ports_idr, gdev->port_id);
		spin_unlock(&gpr->ports_lock);
	}

	return 0;
}

static int gpr_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gpr_device *gdev = to_gpr_device(dev);
	int ret;

	ret = of_device_uevent_modalias(dev, env);
	if (ret != -ENODEV)
		return ret;

	return add_uevent_var(env, "MODALIAS=gpr:%s", gdev->name);
}

struct bus_type gprbus = {
	.name		= "gprbus",
	.match		= gpr_device_match,
	.probe		= gpr_device_probe,
	.uevent		= gpr_uevent,
	.remove		= gpr_device_remove,
};
EXPORT_SYMBOL_GPL(gprbus);

void gpr_free_port(struct gpr_port *port)
{
	struct gpr *gpr = port->gpr;
	unsigned long flags;

	spin_lock_irqsave(&gpr->ports_lock, flags);
	idr_remove(&gpr->ports_idr, port->id);
	spin_unlock_irqrestore(&gpr->ports_lock, flags);

	kfree(port);
}
EXPORT_SYMBOL_GPL(gpr_free_port);

struct gpr_port *gpr_alloc_port(struct gpr_device* gdev, struct device *dev,
				gpr_port_cb cb,	void *priv)
{
	struct gpr *gpr = dev_get_drvdata(gdev->dev.parent);
	struct gpr_port *port;
	int id;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->callback = cb;
	port->gpr = gpr;
	port->priv = priv;
	port->dev = dev;
	spin_lock_init(&port->lock);

	spin_lock(&gpr->ports_lock);
	id = idr_alloc_cyclic(&gpr->ports_idr, port, GPR_DYNAMIC_PORT_START,
			      GPR_DYNAMIC_PORT_END, GFP_ATOMIC);
	if (id < 0) {
		dev_err(dev, "Unable to allocate dynamic GPR src port\n");
		kfree(port);
		spin_unlock(&gpr->ports_lock);
		return ERR_PTR(-ENOMEM);
	}
	port->id = id;
	spin_unlock(&gpr->ports_lock);

	dev_info(dev, "Adding GPR src port (%x)\n", port->id);

	return port;
}
EXPORT_SYMBOL_GPL(gpr_alloc_port);

static int gpr_add_device(struct device *dev, struct device_node *np,
			  u32 port_id, u32 domain_id)
{
	struct gpr *gpr = dev_get_drvdata(dev);
	struct gpr_device *gdev = NULL;
	int ret;

	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	gdev->port_id = port_id;
	gdev->domain_id = domain_id;
	if (np)
		snprintf(gdev->name, GPR_NAME_SIZE, "%pOFn", np);

	dev_set_name(&gdev->dev, "gprport:%s:%x:%x", gdev->name,
		     domain_id, port_id);

	gdev->dev.bus = &gprbus;
	gdev->dev.parent = dev;
	gdev->dev.of_node = np;
	gdev->dev.release = gpr_dev_release;
	gdev->dev.driver = NULL;

	gdev->port.gpr = gpr;
	gdev->port.priv = gdev;
	gdev->port.id = port_id;
	spin_lock_init(&gdev->port.lock);

	spin_lock(&gpr->ports_lock);
	idr_alloc(&gpr->ports_idr, &gdev->port, port_id,
		  port_id + 1, GFP_ATOMIC);
	spin_unlock(&gpr->ports_lock);

	dev_info(dev, "Adding GPR dev: %s\n", dev_name(&gdev->dev));

	ret = device_register(&gdev->dev);
	if (ret) {
		dev_err(dev, "device_register failed: %d\n", ret);
		put_device(&gdev->dev);
	}

	return ret;
}

static void of_register_gpr_devices(struct device *dev)
{
	struct gpr *gpr = dev_get_drvdata(dev);
	struct device_node *node;

	for_each_child_of_node(dev->of_node, node) {
		u32 port_id;
		u32 domain_id;

		if (of_property_read_u32(node, "reg", &port_id))
			continue;

		domain_id = gpr->dest_domain_id;

		if (gpr_add_device(dev, node, port_id, domain_id))
			dev_err(dev, "Failed to add gpr %d port\n", port_id);
	}
}

static int gpr_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct gpr *gpr;
	int ret;

	gpr = devm_kzalloc(dev, sizeof(*gpr), GFP_KERNEL);
	if (!gpr)
		return -ENOMEM;

	ret = of_property_read_u32(dev->of_node, "qcom,gpr-domain",
				   &gpr->dest_domain_id);
	if (ret) {
		dev_err(dev, "GPR Domain ID not specified in DT\n");
		return ret;
	}

	dev_set_drvdata(dev, gpr);
	gpr->ch = rpdev->ept;
	gpr->dev = dev;
	gpr->rxwq = create_singlethread_workqueue("qcom_gpr_rx");
	if (!gpr->rxwq) {
		dev_err(gpr->dev, "Failed to start Rx WQ\n");
		return -ENOMEM;
	}
	INIT_WORK(&gpr->rx_work, gpr_rxwq);

	INIT_LIST_HEAD(&gpr->rx_list);
	spin_lock_init(&gpr->rx_lock);
	spin_lock_init(&gpr->ports_lock);
	idr_init(&gpr->ports_idr);

	of_register_gpr_devices(dev);

	return 0;
}

static int gpr_remove_device(struct device *dev, void *null)
{
	struct gpr_device *gdev = to_gpr_device(dev);

	device_unregister(&gdev->dev);

	return 0;
}

static void gpr_remove(struct rpmsg_device *rpdev)
{
	struct gpr *gpr = dev_get_drvdata(&rpdev->dev);

	device_for_each_child(&rpdev->dev, NULL, gpr_remove_device);
	flush_workqueue(gpr->rxwq);
	destroy_workqueue(gpr->rxwq);
}

/*
 * __gpr_driver_register() - Client driver registration with gprbus
 *
 * @drv:Client driver to be associated with client-device.
 * @owner: owning module/driver
 *
 * This API will register the client driver with the gprbus
 * It is called from the driver's module-init function.
 */
int __gpr_driver_register(struct gpr_driver *drv, struct module *owner)
{
	drv->driver.bus = &gprbus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__gpr_driver_register);

/*
 * gpr_driver_unregister() - Undo effect of gpr_driver_register
 *
 * @drv: Client driver to be unregistered
 */
void gpr_driver_unregister(struct gpr_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(gpr_driver_unregister);

static const struct of_device_id gpr_of_match[] = {
	{ .compatible = "qcom,gpr", },
	{}
};
MODULE_DEVICE_TABLE(of, gpr_of_match);

static struct rpmsg_driver gpr_driver = {
	.probe = gpr_probe,
	.remove = gpr_remove,
	.callback = gpr_callback,
	.drv = {
		.name = "qcom,gpr",
		.of_match_table = gpr_of_match,
	},
};

static int __init gpr_init(void)
{
	int ret;

	ret = bus_register(&gprbus);
	if (!ret)
		ret = register_rpmsg_driver(&gpr_driver);
	else
		bus_unregister(&gprbus);

	return ret;
}

static void __exit gpr_exit(void)
{
	bus_unregister(&gprbus);
	unregister_rpmsg_driver(&gpr_driver);
}

subsys_initcall(gpr_init);
module_exit(gpr_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm GPR Bus");
