// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Amarula Solutions
 *
 * Author: Dario Binacchi <dario.binacchi@amarulasolutions.com>
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/can/dev.h>

#include "flexcan.h"

static void flexcan_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *info)
{
	strscpy(info->driver, "flexcan", sizeof(info->driver));
}

static const struct ethtool_ops flexcan_ethtool_ops = {
	.get_drvinfo = flexcan_get_drvinfo,
};

void flexcan_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &flexcan_ethtool_ops;
}
