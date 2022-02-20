// SPDX-License-Identifier: GPL-2.0
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#include <linux/string.h>
#include <linux/i2c.h>

#include "lsdc_drv.h"
#include "lsdc_regs.h"
#include "lsdc_i2c.h"

/*
 * ls7a_gpio_i2c_set - set the state of a gpio pin indicated by mask
 * @mask: gpio pin mask
 */
static void ls7a_gpio_i2c_set(struct lsdc_i2c * const i2c, int mask, int state)
{
	struct lsdc_device *ldev = to_lsdc(i2c->ddev);
	u8 val;
	unsigned long flags;

	spin_lock_irqsave(&ldev->reglock, flags);

	if (state) {
		val = readb(i2c->dir_reg);
		val |= mask;
		writeb(val, i2c->dir_reg);
	} else {
		val = readb(i2c->dir_reg);
		val &= ~mask;
		writeb(val, i2c->dir_reg);

		val = readb(i2c->dat_reg);
		if (state)
			val |= mask;
		else
			val &= ~mask;
		writeb(val, i2c->dat_reg);
	}

	spin_unlock_irqrestore(&ldev->reglock, flags);
}

/*
 * ls7a_gpio_i2c_get - read value back from gpio pin
 * @mask: gpio pin mask
 */
static int ls7a_gpio_i2c_get(struct lsdc_i2c * const i2c, int mask)
{
	struct lsdc_device *ldev = to_lsdc(i2c->ddev);
	u8 val;
	unsigned long flags;

	spin_lock_irqsave(&ldev->reglock, flags);

	/* first set this pin as input */
	val = readb(i2c->dir_reg);
	val |= mask;
	writeb(val, i2c->dir_reg);

	/* then get level state from this pin */
	val = readb(i2c->dat_reg);

	spin_unlock_irqrestore(&ldev->reglock, flags);

	return (val & mask) ? 1 : 0;
}

/* set the state on the i2c->sda pin */
static void ls7a_i2c_set_sda(void *i2c, int state)
{
	struct lsdc_i2c * const li2c = (struct lsdc_i2c *)i2c;

	return ls7a_gpio_i2c_set(li2c, li2c->sda, state);
}

/* set the state on the i2c->scl pin */
static void ls7a_i2c_set_scl(void *i2c, int state)
{
	struct lsdc_i2c * const li2c = (struct lsdc_i2c *)i2c;

	return ls7a_gpio_i2c_set(li2c, li2c->scl, state);
}

/* read the value from the i2c->sda pin */
static int ls7a_i2c_get_sda(void *i2c)
{
	struct lsdc_i2c * const li2c = (struct lsdc_i2c *)i2c;

	return ls7a_gpio_i2c_get(li2c, li2c->sda);
}

/* read the value from the i2c->scl pin */
static int ls7a_i2c_get_scl(void *i2c)
{
	struct lsdc_i2c * const li2c = (struct lsdc_i2c *)i2c;

	return ls7a_gpio_i2c_get(li2c, li2c->scl);
}

/*
 * Get i2c id from connector id
 *
 * TODO: get it from dtb
 */
static int lsdc_get_i2c_id(struct drm_device *ddev, unsigned int index)
{
	return index;
}

/*
 * mainly for dc in ls7a1000 which have builtin gpio emulated i2c
 *
 * @index : output channel index, 0 for DVO0, 1 for DVO1
 */
struct i2c_adapter *lsdc_create_i2c_chan(struct drm_device *ddev,
					 unsigned int index)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct i2c_adapter *adapter;
	struct lsdc_i2c *li2c;
	int ret;

	li2c = devm_kzalloc(ddev->dev, sizeof(*li2c), GFP_KERNEL);
	if (!li2c)
		return ERR_PTR(-ENOMEM);

	li2c->ddev = ddev;

	if (index == 0) {
		li2c->sda = 0x01;
		li2c->scl = 0x02;
	} else if (index == 1) {
		li2c->sda = 0x04;
		li2c->scl = 0x08;
	}

	li2c->dir_reg = ldev->reg_base + LS7A_DC_GPIO_DIR_REG;
	li2c->dat_reg = ldev->reg_base + LS7A_DC_GPIO_DAT_REG;

	li2c->bit.setsda = ls7a_i2c_set_sda;
	li2c->bit.setscl = ls7a_i2c_set_scl;
	li2c->bit.getsda = ls7a_i2c_get_sda;
	li2c->bit.getscl = ls7a_i2c_get_scl;
	li2c->bit.udelay = 5;
	li2c->bit.timeout = usecs_to_jiffies(2200);
	li2c->bit.data = li2c;

	adapter = &li2c->adapter;

	adapter->algo_data = &li2c->bit;
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_DDC;
	adapter->dev.parent = ddev->dev;
	adapter->nr = -1;

	snprintf(adapter->name, sizeof(adapter->name), "%s-%d", "lsdc_gpio_i2c", index);

	i2c_set_adapdata(adapter, li2c);

	ret = i2c_bit_add_numbered_bus(adapter);
	if (ret) {
		devm_kfree(ddev->dev, li2c);
		return ERR_PTR(ret);
	}

	return adapter;
}

/*
 * lsdc_get_i2c_adapter - get a i2c adapter from i2c susystem.
 *
 * @index : output channel index, 0 for DVO0, 1 for DVO1
 */
struct i2c_adapter *lsdc_get_i2c_adapter(struct drm_device *ddev,
					 unsigned int index)
{
	unsigned int i2c_id;

	/* find mapping between i2c id and connector id */
	i2c_id = lsdc_get_i2c_id(ddev, index);

	return i2c_get_adapter(i2c_id);
}

void lsdc_destroy_i2c(struct drm_device *ddev, struct i2c_adapter *adapter)
{
	i2c_put_adapter(adapter);
}
