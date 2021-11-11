// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include "sp_mdio.h"

u32 mdio_read(struct sp_mac *mac, u32 phy_id, u16 regnum)
{
	int ret;

	ret = hal_mdio_access(mac, MDIO_READ_CMD, phy_id, regnum, 0);
	if (ret < 0)
		return -EOPNOTSUPP;

	return ret;
}

u32 mdio_write(struct sp_mac *mac, u32 phy_id, u32 regnum, u16 val)
{
	int ret;

	ret = hal_mdio_access(mac, MDIO_WRITE_CMD, phy_id, regnum, val);
	if (ret < 0)
		return -EOPNOTSUPP;

	return 0;
}

static int mii_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct sp_mac *mac = bus->priv;

	return mdio_read(mac, phy_id, regnum);
}

static int mii_write(struct mii_bus *bus, int phy_id, int regnum, u16 val)
{
	struct sp_mac *mac = bus->priv;

	return mdio_write(mac, phy_id, regnum, val);
}

u32 mdio_init(struct platform_device *pdev, struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);
	struct mii_bus *mii_bus;
	struct device_node *mdio_node;
	int ret;

	mii_bus = mdiobus_alloc();
	if (!mii_bus) {
		netdev_err(ndev, "Failed to allocate mdio_bus memory!\n");
		return -ENOMEM;
	}

	mii_bus->name = "sunplus_mii_bus";
	mii_bus->parent = &pdev->dev;
	mii_bus->priv = mac;
	mii_bus->read = mii_read;
	mii_bus->write = mii_write;
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(&pdev->dev));

	mdio_node = of_get_parent(mac->phy_node);
	if (!mdio_node) {
		netdev_err(ndev, "Failed to get mdio_node!\n");
		return -ENODATA;
	}

	ret = of_mdiobus_register(mii_bus, mdio_node);
	if (ret) {
		netdev_err(ndev, "Failed to register mii bus!\n");
		mdiobus_free(mii_bus);
		return ret;
	}

	mac->comm->mii_bus = mii_bus;
	return ret;
}

void mdio_remove(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);

	if (mac->comm->mii_bus) {
		mdiobus_unregister(mac->comm->mii_bus);
		mdiobus_free(mac->comm->mii_bus);
		mac->comm->mii_bus = NULL;
	}
}
