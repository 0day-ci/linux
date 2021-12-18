/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2021 Innovative Advantage Inc.
 */

#include <linux/regmap.h>

struct ocelot_mfd_config {
	struct device *dev;
	struct regmap *(*get_regmap)(struct ocelot_mfd_config *config,
				     const struct resource *res,
				     const char *name);
	int (*init_bus)(struct ocelot_mfd_config *config);
};

void ocelot_mfd_get_resource_name(char *name, const struct resource *res,
				  int size);
int ocelot_mfd_init(struct ocelot_mfd_config *config);
int ocelot_mfd_remove(struct ocelot_mfd_config *config);
