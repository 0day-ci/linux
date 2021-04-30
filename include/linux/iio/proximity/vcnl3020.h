/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file describe API for VCNL3020 proximity sensor.
 */

#ifndef VCNL3020_PROXIMITY_H
#define VCNL3020_PROXIMITY_H

#define VCNL3020_DRV_HWMON	"vcnl3020-hwmon"
#define VCNL3020_DRV		"vcnl3020"

/**
 * struct vcnl3020_data - vcnl3020 specific data.
 * @regmap:	device register map.
 * @dev:	vcnl3020 device.
 * @rev:	revision id.
 */
struct vcnl3020_data {
	struct regmap *regmap;
	struct device *dev;
	u8 rev;
};

bool vcnl3020_is_thr_triggered(struct vcnl3020_data *data);

#endif
