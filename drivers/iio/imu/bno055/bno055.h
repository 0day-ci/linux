/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __BNO055_H__
#define __BNO055_H__

#include <linux/device.h>
#include <linux/regmap.h>

int bno055_probe(struct device *dev, struct regmap *regmap, int irq,
		 int xfer_burst_break_thr);
extern const struct regmap_config bno055_regmap_config;

#endif
