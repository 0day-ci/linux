/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifndef __LSDC_I2C__
#define __LSDC_I2C__

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

struct lsdc_i2c {
	struct drm_device *ddev;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit;
	/* pin bit mask */
	u8 sda;
	u8 scl;

	void __iomem *dir_reg;
	void __iomem *dat_reg;
};

void lsdc_destroy_i2c(struct drm_device *ddev, struct i2c_adapter *i2c);

struct i2c_adapter *lsdc_create_i2c_chan(struct drm_device *ddev,
					 unsigned int con_id);

struct i2c_adapter *lsdc_get_i2c_adapter(struct drm_device *ddev,
					 unsigned int con_id);

#endif
