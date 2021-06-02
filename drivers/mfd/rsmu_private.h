/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Multi-function driver for the IDT ClockMatrix(TM) and 82p33xxx families of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */

#ifndef __RSMU_MFD_PRIVATE_H
#define __RSMU_MFD_PRIVATE_H

#include <linux/mfd/rsmu.h>

struct rsmu_dev {
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;
	enum rsmu_type type;
	u16 page;
};

int rsmu_device_init(struct rsmu_dev *rsmu);
void rsmu_device_exit(struct rsmu_dev *rsmu);
#endif /*  __LINUX_MFD_RSMU_H */
