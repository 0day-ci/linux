/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Sartura Ltd
 */

#ifndef __TN48M_H__
#define __TN48M_H__

#include <linux/device.h>
#include <linux/regmap.h>

#define HARDWARE_VERSION_ID	0x0
#define HARDWARE_VERSION_MASK	GENMASK(3, 0)
#define HARDWARE_VERSION_EVT1	0
#define HARDWARE_VERSION_EVT2	1
#define HARDWARE_VERSION_DVT	2
#define HARDWARE_VERSION_PVT	3
#define BOARD_ID		0x1
#define BOARD_ID_TN48M		0xa
#define BOARD_ID_TN48M_P	0xb
#define CPLD_CODE_VERSION	0x2
#define SFP_TX_DISABLE		0x31
#define SFP_PRESENT		0x3a
#define SFP_LOS			0x40
#define PSU_STATUS		0xa

struct tn48m_data {
	struct device *dev;
	struct regmap *regmap;
	struct i2c_client *client;
	struct dentry *debugfs_dir;
};

#endif
