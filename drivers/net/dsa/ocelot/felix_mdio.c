// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Distributed Switch Architecture VSC9953 driver
 * Copyright (C) 2020, Maxim Kochetkov <fido_max@inbox.ru>
 * Copyright (C) 2021 Innovative Advantage
 */
#include <linux/of_mdio.h>
#include <linux/types.h>
#include <soc/mscc/ocelot.h>
#include <linux/dsa/ocelot.h>
#include <linux/mdio/mdio-mscc-miim.h>
#include "felix.h"
#include "felix_mdio.h"

int felix_of_mdio_register(struct ocelot *ocelot, struct device_node *np)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct device *dev = ocelot->dev;
	int rc;

	/* Needed in order to initialize the bus mutex lock */
	rc = of_mdiobus_register(felix->imdio, np);
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
	int err;

	err = mscc_miim_setup(dev, &bus, ocelot->targets[GCB],
			      ocelot->map[GCB][GCB_MIIM_MII_STATUS & REG_MASK],
			      ocelot->targets[GCB],
			      ocelot->map[GCB][GCB_PHY_PHY_CFG & REG_MASK]);

	if (!err)
		felix->imdio = bus;

	return err;
}

void felix_mdio_bus_free(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->imdio)
		mdiobus_unregister(felix->imdio);
}
