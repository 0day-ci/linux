// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include "sp_phy.h"
#include "sp_mdio.h"

static void mii_linkchange(struct net_device *netdev)
{
}

int sp_phy_probe(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);
	struct phy_device *phydev;
	int i;

	phydev = of_phy_connect(ndev, mac->phy_node, mii_linkchange,
				0, mac->phy_mode);
	if (!phydev) {
		netdev_err(ndev, "\"%s\" failed to connect to phy!\n", ndev->name);
		return -ENODEV;
	}

	for (i = 0; i < sizeof(phydev->supported) / sizeof(long); i++)
		phydev->advertising[i] = phydev->supported[i];

	phydev->irq = PHY_MAC_INTERRUPT;
	mac->phy_dev = phydev;

	// Bug workaround:
	// Flow-control of phy should be enabled. MAC flow-control will refer
	// to the bit to decide to enable or disable flow-control.
	mdio_write(mac, mac->phy_addr, 4, mdio_read(mac, mac->phy_addr, 4) | (1 << 10));

	return 0;
}

void sp_phy_start(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);

	if (mac->phy_dev)
		phy_start(mac->phy_dev);
}

void sp_phy_stop(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);

	if (mac->phy_dev)
		phy_stop(mac->phy_dev);
}

void sp_phy_remove(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);

	if (mac->phy_dev) {
		phy_disconnect(mac->phy_dev);
		mac->phy_dev = NULL;
	}
}
