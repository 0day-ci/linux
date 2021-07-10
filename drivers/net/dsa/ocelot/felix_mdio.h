// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Distributed Switch Architecture VSC9953 driver
 * Copyright (C) 2020, Maxim Kochetkov <fido_max@inbox.ru>
 */
#include <linux/types.h>
#include <soc/mscc/ocelot.h>

int felix_mdio_bus_alloc(struct ocelot *ocelot);
int felix_mdio_register(struct ocelot *ocelot);
void felix_mdio_bus_free(struct ocelot *ocelot);

