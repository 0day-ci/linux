/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Shared code for indirect MDIO access for Felix drivers
 *
 * Author: Colin Foster <colin.foster@in-advantage.com>
 * Copyright (C) 2021 Innovative Advantage
 */
#include <linux/of.h>
#include <linux/types.h>
#include <soc/mscc/ocelot.h>

int felix_mdio_bus_alloc(struct ocelot *ocelot);
int felix_of_mdio_register(struct ocelot *ocelot, struct device_node *np);
void felix_mdio_bus_free(struct ocelot *ocelot);
