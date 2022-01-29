/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2021 Innovative Advantage Inc.
 */

#include <linux/kconfig.h>
#include <linux/regmap.h>

struct ocelot_core {
	struct device *dev;
	struct regmap *gcb_regmap;
};

void ocelot_get_resource_name(char *name, const struct resource *res,
			      int size);
int ocelot_core_init(struct ocelot_core *core);
int ocelot_remove(struct ocelot_core *core);

#if IS_ENABLED(CONFIG_MFD_OCELOT_SPI)
struct regmap *ocelot_spi_devm_get_regmap(struct ocelot_core *core,
					  struct device *dev,
					  const struct resource *res);
int ocelot_spi_initialize(struct ocelot_core *core);
#else
static inline struct regmap *ocelot_spi_devm_get_regmap(
		struct ocelot_core *core, struct device *dev,
		const struct resource *res)
{
	return NULL;
}

static inline int ocelot_spi_initialize(struct ocelot_core *core)
{
	return -EOPNOTSUPP;
}
#endif
