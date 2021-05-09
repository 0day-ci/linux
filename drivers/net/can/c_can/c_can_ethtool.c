// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021, Dario Binacchi <dariobin@libero.it>
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>

#include "c_can.h"

static void c_can_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct c_can_priv *priv = netdev_priv(netdev);
	struct platform_device	*pdev = to_platform_device(priv->device);

	strscpy(info->driver, "c_can", sizeof(info->driver));
	strscpy(info->version, "1.0", sizeof(info->version));
	strscpy(info->bus_info, pdev->name, sizeof(info->bus_info));
}

static void c_can_get_channels(struct net_device *netdev,
			       struct ethtool_channels *ch)
{
	struct c_can_priv *priv = netdev_priv(netdev);

	ch->max_rx = priv->msg_obj_num;
	ch->max_tx = priv->msg_obj_num;
	ch->max_combined = priv->msg_obj_num;
	ch->rx_count = priv->msg_obj_rx_num;
	ch->tx_count = priv->msg_obj_tx_num;
	ch->combined_count = priv->msg_obj_rx_num + priv->msg_obj_tx_num;
}

static const struct ethtool_ops c_can_ethtool_ops = {
	.get_drvinfo = c_can_get_drvinfo,
	.get_channels = c_can_get_channels,
};

void c_can_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &c_can_ethtool_ops;
}
