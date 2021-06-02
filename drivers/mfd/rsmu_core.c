// SPDX-License-Identifier: GPL-2.0+
/*
 * Core driver for the Renesas ClockMatrix(TM) and 82P33xxx families of
 * timing and synchronization devices.
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rsmu.h>
#include "rsmu_private.h"

enum {
	RSMU_PHC = 0,
	RSMU_CDEV = 1,
	RSMU_N_DEVS = 2,
};

/* clockmatrix devices */
static struct mfd_cell rsmu_cm_devs[] = {
	[RSMU_PHC] = {
		.name = "idtcm-phc",
		.pdata_size = sizeof(struct rsmu_pdata),
	},
	[RSMU_CDEV] = {
		.name = "idtcm-cdev",
		.pdata_size = sizeof(struct rsmu_pdata),
	},
};

/* sabre devices */
static struct mfd_cell rsmu_sabre_devs[] = {
	[RSMU_PHC] = {
		.name = "idt82p33-phc",
		.pdata_size = sizeof(struct rsmu_pdata),
	},
	[RSMU_CDEV] = {
		.name = "idt82p33-cdev",
		.pdata_size = sizeof(struct rsmu_pdata),
	},
};

int rsmu_read(struct device *dev, u16 reg, u8 *buf, u16 size)
{
	struct rsmu_dev *rsmu = dev_get_drvdata(dev);

	return regmap_bulk_read(rsmu->regmap, reg, buf, size);
}
EXPORT_SYMBOL_GPL(rsmu_read);

int rsmu_write(struct device *dev, u16 reg, u8 *buf, u16 size)
{
	struct rsmu_dev *rsmu = dev_get_drvdata(dev);

	return regmap_bulk_write(rsmu->regmap, reg, buf, size);
}
EXPORT_SYMBOL_GPL(rsmu_write);

int rsmu_device_init(struct rsmu_dev *rsmu)
{
	struct rsmu_pdata *pdata;
	struct mfd_cell *cells;
	int ret;

	pdata = devm_kzalloc(rsmu->dev, sizeof(struct rsmu_pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	switch (rsmu->type) {
	case RSMU_CM:
		cells = rsmu_cm_devs;
		break;
	case RSMU_SABRE:
		cells = rsmu_sabre_devs;
		break;
	default:
		dev_err(rsmu->dev, "Invalid rsmu device type: %d\n", rsmu->type);
		return -ENODEV;
	}

	cells[RSMU_PHC].platform_data = pdata;
	cells[RSMU_CDEV].platform_data = pdata;
	mutex_init(&rsmu->lock);
	pdata->lock = &rsmu->lock;

	ret = devm_mfd_add_devices(rsmu->dev, PLATFORM_DEVID_AUTO, cells,
				   RSMU_N_DEVS, NULL, 0, NULL);
	if (ret < 0)
		dev_err(rsmu->dev, "Add mfd devices failed: %d\n", ret);

	return ret;
}

void rsmu_device_exit(struct rsmu_dev *rsmu)
{
	mutex_destroy(&rsmu->lock);
}

MODULE_DESCRIPTION("Core driver for Renesas Synchronization Management Unit");
MODULE_LICENSE("GPL");
