/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __LOONGSON_I2C_H__
#define __LOONGSON_I2C_H__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/gpio/driver.h>
#include <drm/drm_edid.h>

#define DC_I2C_TON 5
#define DC_I2C_NAME "ls_dc_i2c"
#define LS_MAX_I2C_BUS 2

/* Loongson 7A display controller proprietary GPIOs */
#define LS7A_DC_GPIO_BASE 73
#define DC_GPIO_0 (73)
#define DC_GPIO_1 (74)
#define DC_GPIO_2 (75)
#define DC_GPIO_3 (76)
#define LS7A_DC_GPIO_CFG_OFFSET (0x1660)
#define LS7A_DC_GPIO_IN_OFFSET (0x1650)
#define LS7A_DC_GPIO_OUT_OFFSET (0x1650)

struct loongson_device;
struct loongson_i2c {
	struct loongson_device *ldev;
	struct i2c_adapter *adapter;
	u32 data, clock;
	bool use;
	u32 i2c_id;
};

int loongson_i2c_init(struct loongson_device *ldev);

#endif /* __LOONGSON_I2C_H__ */
