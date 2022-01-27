// SPDX-License-Identifier: GPL-2.0

#include <linux/usb/usbnet.h>

static struct usbnet *usbnet_from_devlink(struct devlink *devlink)
{
	struct usbnet_devlink_priv *priv = devlink_priv(devlink);

	return priv->usbnet;
}

static int usbnet_usb_health_report(struct devlink_health_reporter *reporter,
				    struct usbnet_devlink_priv *dl_priv,
				    char *string, int err)
{
	char buf[50];

	snprintf(buf, sizeof(buf), "%s %pe", string, ERR_PTR(err));

	return devlink_health_report(reporter, buf, dl_priv);
}

int usbnet_usb_tx_health_report(struct usbnet *usbnet, char *string, int err)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(usbnet->devlink);

	return usbnet_usb_health_report(dl_priv->usb_tx_fault_reporter,
					dl_priv, string, err);
}

int usbnet_usb_rx_health_report(struct usbnet *usbnet, char *string, int err)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(usbnet->devlink);

	return usbnet_usb_health_report(dl_priv->usb_rx_fault_reporter,
					dl_priv, string, err);
}

int usbnet_usb_ctrl_health_report(struct usbnet *usbnet, char *string, int err)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(usbnet->devlink);

	return usbnet_usb_health_report(dl_priv->usb_ctrl_fault_reporter,
					dl_priv, string, err);
}

int usbnet_usb_intr_health_report(struct usbnet *usbnet, char *string, int err)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(usbnet->devlink);

	return usbnet_usb_health_report(dl_priv->usb_intr_fault_reporter,
					dl_priv, string, err);
}

static const struct
devlink_health_reporter_ops usbnet_usb_ctrl_fault_reporter_ops = {
	.name = "usb_ctrl",
};

static const struct
devlink_health_reporter_ops usbnet_usb_intr_fault_reporter_ops = {
	.name = "usb_intr",
};

static const struct
devlink_health_reporter_ops usbnet_usb_tx_fault_reporter_ops = {
	.name = "usb_tx",
};

static const struct
devlink_health_reporter_ops usbnet_usb_rx_fault_reporter_ops = {
	.name = "usb_rx",
};

static int usbnet_health_reporters_create(struct devlink *devlink)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(devlink);
	struct usbnet *usbnet = usbnet_from_devlink(devlink);
	int ret;

	dl_priv->usb_rx_fault_reporter =
		devlink_health_reporter_create(devlink,
					       &usbnet_usb_rx_fault_reporter_ops,
					       0, dl_priv);
	if (IS_ERR(dl_priv->usb_rx_fault_reporter)) {
		ret = PTR_ERR(dl_priv->usb_rx_fault_reporter);
		goto create_error;
	}

	dl_priv->usb_tx_fault_reporter =
		devlink_health_reporter_create(devlink,
					       &usbnet_usb_tx_fault_reporter_ops,
					       0, dl_priv);
	if (IS_ERR(dl_priv->usb_tx_fault_reporter)) {
		ret = PTR_ERR(dl_priv->usb_tx_fault_reporter);
		goto destroy_usb_rx;
	}

	dl_priv->usb_ctrl_fault_reporter =
		devlink_health_reporter_create(devlink,
					       &usbnet_usb_ctrl_fault_reporter_ops,
					       0, dl_priv);
	if (IS_ERR(dl_priv->usb_ctrl_fault_reporter)) {
		ret = PTR_ERR(dl_priv->usb_ctrl_fault_reporter);
		goto destroy_usb_tx;
	}

	dl_priv->usb_intr_fault_reporter =
		devlink_health_reporter_create(devlink,
					       &usbnet_usb_intr_fault_reporter_ops,
					       0, dl_priv);
	if (IS_ERR(dl_priv->usb_intr_fault_reporter)) {
		ret = PTR_ERR(dl_priv->usb_tx_fault_reporter);
		goto destroy_usb_ctrl;
	}

	return 0;

destroy_usb_ctrl:
	devlink_health_reporter_destroy(dl_priv->usb_ctrl_fault_reporter);
destroy_usb_tx:
	devlink_health_reporter_destroy(dl_priv->usb_tx_fault_reporter);
destroy_usb_rx:
	devlink_health_reporter_destroy(dl_priv->usb_rx_fault_reporter);
create_error:
	netif_err(usbnet, probe, usbnet->net,
		  "Failed to register health reporters. %pe\n", ERR_PTR(ret));

	return ret;
}

static int usbnet_devlink_info_get(struct devlink *devlink,
				 struct devlink_info_req *req,
				 struct netlink_ext_ack *extack)
{
	struct usbnet *usbnet = usbnet_from_devlink(devlink);
	char buf[10];
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	scnprintf(buf, 10, "%d.%d", 100, 200);
	err = devlink_info_version_running_put(req, usbnet->driver_name, buf);
	if (err)
		return err;

	return 0;
}

static const struct devlink_ops usbnet_devlink_ops = {
	.info_get = usbnet_devlink_info_get,
};

static int usbnet_devlink_port_add(struct devlink *devlink)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(devlink);
	struct usbnet *usbnet = usbnet_from_devlink(devlink);
	struct devlink_port *devlink_port = &dl_priv->devlink_port;
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	devlink_port_attrs_set(devlink_port, &attrs);

	err = devlink_port_register(usbnet->devlink, devlink_port, 0);
	if (err)
		return err;

	devlink_port_type_eth_set(devlink_port, usbnet->net);

	return 0;
}

int usbnet_devlink_alloc(struct usbnet *usbnet)
{
	struct net_device *net = usbnet->net;
	struct device *dev = net->dev.parent;
	struct usbnet_devlink_priv *dl_priv;
	int ret;

	usbnet->devlink =
		devlink_alloc(&usbnet_devlink_ops, sizeof(*dl_priv), dev);
	if (!usbnet->devlink) {
		netif_err(usbnet, probe, usbnet->net, "devlink_alloc failed\n");
		return -ENOMEM;
	}
	dl_priv = devlink_priv(usbnet->devlink);
	dl_priv->usbnet = usbnet;

	ret = usbnet_devlink_port_add(usbnet->devlink);
	if (ret)
		goto free_devlink;

	ret = usbnet_health_reporters_create(usbnet->devlink);
	if (ret)
		goto free_port;

	return 0;

free_port:
	devlink_port_type_clear(&dl_priv->devlink_port);
	devlink_port_unregister(&dl_priv->devlink_port);
free_devlink:
	devlink_free(usbnet->devlink);

	return ret;
}

void usbnet_devlink_free(struct usbnet *usbnet)
{
	struct usbnet_devlink_priv *dl_priv = devlink_priv(usbnet->devlink);
	struct devlink_port *devlink_port = &dl_priv->devlink_port;

	devlink_health_reporter_destroy(dl_priv->usb_rx_fault_reporter);
	devlink_health_reporter_destroy(dl_priv->usb_tx_fault_reporter);
	devlink_health_reporter_destroy(dl_priv->usb_ctrl_fault_reporter);
	devlink_health_reporter_destroy(dl_priv->usb_intr_fault_reporter);

	devlink_port_type_clear(devlink_port);
	devlink_port_unregister(devlink_port);

	devlink_free(usbnet->devlink);
}

void usbnet_devlink_register(struct usbnet *usbnet)
{
	devlink_register(usbnet->devlink);
}

void usbnet_devlink_unregister(struct usbnet *usbnet)
{
	devlink_unregister(usbnet->devlink);
}
