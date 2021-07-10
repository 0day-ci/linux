// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Distributed Switch Architecture VSC9953 driver
 * Copyright (C) 2020, Maxim Kochetkov <fido_max@inbox.ru>
 */
#include <linux/types.h>
#include <soc/mscc/ocelot.h>
#include <linux/dsa/ocelot.h>
#include <linux/iopoll.h>
#include "felix.h"

#define MSCC_MIIM_CMD_OPR_WRITE			BIT(1)
#define MSCC_MIIM_CMD_OPR_READ			BIT(2)
#define MSCC_MIIM_CMD_WRDATA_SHIFT		4
#define MSCC_MIIM_CMD_REGAD_SHIFT		20
#define MSCC_MIIM_CMD_PHYAD_SHIFT		25
#define MSCC_MIIM_CMD_VLD			BIT(31)

#define FELIX_MDIO_MII_TIMEOUT			10000
#define FELIX_MDIO_MII_RETRY			10

static int felix_gcb_miim_pending_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_MIIM_MII_STATUS_PENDING, &val);

	return val;
}

static int felix_gcb_miim_busy_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_MIIM_MII_STATUS_BUSY, &val);

	return val;
}

static int felix_mdio_write(struct mii_bus *bus, int phy_id, int regnum,
			    u16 value)
{
	struct ocelot *ocelot = bus->priv;
	int err, cmd, val;

	/* Wait while MIIM controller becomes idle */
	err = readx_poll_timeout(felix_gcb_miim_pending_status, ocelot, val,
				 !val, FELIX_MDIO_MII_RETRY,
				 FELIX_MDIO_MII_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "MDIO write: pending timeout\n");
		goto out;
	}

	cmd = MSCC_MIIM_CMD_VLD | (phy_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
	      (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) |
	      (value << MSCC_MIIM_CMD_WRDATA_SHIFT) |
	      MSCC_MIIM_CMD_OPR_WRITE;

	ocelot_write(ocelot, cmd, GCB_MIIM_MII_CMD);

out:
	return err;
}

static int felix_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct ocelot *ocelot = bus->priv;
	int err, cmd, val;

	/* Wait until MIIM controller becomes idle */
	err = readx_poll_timeout(felix_gcb_miim_pending_status, ocelot, val,
				 !val, FELIX_MDIO_MII_RETRY,
				 FELIX_MDIO_MII_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "MDIO read: pending timeout\n");
		goto out;
	}

	/* Write the MIIM COMMAND register */
	cmd = MSCC_MIIM_CMD_VLD | (phy_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
	      (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) | MSCC_MIIM_CMD_OPR_READ;

	ocelot_write(ocelot, cmd, GCB_MIIM_MII_CMD);

	/* Wait while read operation via the MIIM controller is in progress */
	err = readx_poll_timeout(felix_gcb_miim_busy_status, ocelot, val, !val,
				 FELIX_MDIO_MII_RETRY, FELIX_MDIO_MII_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "MDIO read: busy timeout\n");
		goto out;
	}

	val = ocelot_read(ocelot, GCB_MIIM_MII_DATA);

	err = val & 0xFFFF;
out:
	return err;
}

int felix_mdio_register(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct device *dev = ocelot->dev;
	int rc;

	/* Needed in order to initialize the bus mutex lock */
	rc = mdiobus_register(felix->imdio);
	if (rc < 0) {
		dev_err(dev, "failed to register MDIO bus\n");
		felix->imdio = NULL;
	}

	return rc;
}

int felix_mdio_bus_alloc(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct device *dev = ocelot->dev;
	struct mii_bus *bus;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "Felix internal MDIO bus";
	bus->read = felix_mdio_read;
	bus->write = felix_mdio_write;
	bus->parent = dev;
	bus->priv = ocelot;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-imdio", dev_name(dev));

	felix->imdio = bus;

	return 0;
}

void felix_mdio_bus_free(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->imdio)
		mdiobus_unregister(felix->imdio);
}

