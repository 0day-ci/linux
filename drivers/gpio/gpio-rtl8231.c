// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* RTL8231 registers for LED control */
#define RTL8231_FUNC0			0x00
#define RTL8231_FUNC1			0x01
#define RTL8231_PIN_MODE0		0x02
#define RTL8231_PIN_MODE1		0x03
#define RTL8231_PIN_HI_CFG		0x04
#define RTL8231_GPIO_DIR0		0x05
#define RTL8231_GPIO_DIR1		0x06
#define RTL8231_GPIO_INVERT0		0x07
#define RTL8231_GPIO_INVERT1		0x08
#define RTL8231_GPIO_DATA0		0x1c
#define RTL8231_GPIO_DATA1		0x1d
#define RTL8231_GPIO_DATA2		0x1e

#define RTL8231_READY_CODE_VALUE	0x37
#define RTL8231_GPIO_DIR_IN		1
#define RTL8231_GPIO_DIR_OUT		0

#define RTL8231_MAX_GPIOS		37

enum rtl8231_regfield {
	RTL8231_FIELD_LED_START,
	RTL8231_FIELD_READY_CODE,
	RTL8231_FIELD_SOFT_RESET,
	RTL8231_FIELD_PIN_MODE0,
	RTL8231_FIELD_PIN_MODE1,
	RTL8231_FIELD_PIN_MODE2,
	RTL8231_FIELD_GPIO_DIR0,
	RTL8231_FIELD_GPIO_DIR1,
	RTL8231_FIELD_GPIO_DIR2,
	RTL8231_FIELD_GPIO_DATA0,
	RTL8231_FIELD_GPIO_DATA1,
	RTL8231_FIELD_GPIO_DATA2,
	RTL8231_FIELD_MAX
};

static const struct reg_field rtl8231_fields[RTL8231_FIELD_MAX] = {
	[RTL8231_FIELD_LED_START]   = REG_FIELD(RTL8231_FUNC0, 1, 1),
	[RTL8231_FIELD_READY_CODE]  = REG_FIELD(RTL8231_FUNC1, 4, 9),
	[RTL8231_FIELD_SOFT_RESET]  = REG_FIELD(RTL8231_PIN_HI_CFG, 15, 15),
	[RTL8231_FIELD_PIN_MODE0]   = REG_FIELD(RTL8231_PIN_MODE0, 0, 15),
	[RTL8231_FIELD_PIN_MODE1]   = REG_FIELD(RTL8231_PIN_MODE1, 0, 15),
	[RTL8231_FIELD_PIN_MODE2]   = REG_FIELD(RTL8231_PIN_HI_CFG, 0, 4),
	[RTL8231_FIELD_GPIO_DIR0]   = REG_FIELD(RTL8231_GPIO_DIR0, 0, 15),
	[RTL8231_FIELD_GPIO_DIR1]   = REG_FIELD(RTL8231_GPIO_DIR1, 0, 15),
	[RTL8231_FIELD_GPIO_DIR2]   = REG_FIELD(RTL8231_PIN_HI_CFG, 5, 9),
	[RTL8231_FIELD_GPIO_DATA0]  = REG_FIELD(RTL8231_GPIO_DATA0, 0, 15),
	[RTL8231_FIELD_GPIO_DATA1]  = REG_FIELD(RTL8231_GPIO_DATA1, 0, 15),
	[RTL8231_FIELD_GPIO_DATA2]  = REG_FIELD(RTL8231_GPIO_DATA2, 0, 4),
};

/**
 * struct rtl8231_gpio_ctrl - Control data for an RTL8231 chip
 *
 * @gc: Associated gpio_chip instance
 * @dev
 * @fields
 */
struct rtl8231_gpio_ctrl {
	struct gpio_chip gc;
	struct device *dev;
	struct regmap_field *fields[RTL8231_FIELD_MAX];
};

static int rtl8231_pin_read(struct rtl8231_gpio_ctrl *ctrl, int base, int offset)
{
	int field = base + offset / 16;
	int bit = offset % 16;
	unsigned int v;
	int err;

	err = regmap_field_read(ctrl->fields[field], &v);

	if (err)
		return err;

	return !!(v & BIT(bit));
}

static int rtl8231_pin_write(struct rtl8231_gpio_ctrl *ctrl, int base, int offset, int val)
{
	int field = base + offset / 16;
	int bit = offset % 16;

	return regmap_field_update_bits(ctrl->fields[field], BIT(bit), val << bit);
}

static int rtl8231_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	return rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DIR0, offset, RTL8231_GPIO_DIR_IN);
}

static int rtl8231_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	int err;

	err = rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DIR0, offset, RTL8231_GPIO_DIR_OUT);
	if (err)
		return err;

	return rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DATA0, offset, value);
}

static int rtl8231_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	return rtl8231_pin_read(ctrl, RTL8231_FIELD_GPIO_DIR0, offset);
}

static int rtl8231_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	return rtl8231_pin_read(ctrl, RTL8231_FIELD_GPIO_DATA0, offset);
}

static void rtl8231_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DATA0, offset, value);
}

static int rtl8231_gpio_get_multiple(struct gpio_chip *gc,
	unsigned long *mask, unsigned long *bits)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	int read, field;
	int offset, shift;
	int sub_mask;
	int value, err;

	err = 0;
	read = 0;
	field = 0;

	while (read < gc->ngpio) {
		shift = read % (8 * sizeof(*bits));
		offset = read / (8 * sizeof(*bits));
		sub_mask = (mask[offset] >> shift) & 0xffff;
		if (sub_mask) {
			err = regmap_field_read(ctrl->fields[RTL8231_FIELD_GPIO_DATA0 + field],
				&value);
			if (err)
				return err;
			value = (sub_mask & value) << shift;
			sub_mask <<= shift;
			bits[offset] = (bits[offset] & ~sub_mask) | value;
		}

		field += 1;
		read += 16;
	}

	return err;
}

static void rtl8231_gpio_set_multiple(struct gpio_chip *gc,
	unsigned long *mask, unsigned long *bits)
{
	struct rtl8231_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	int read, field;
	int offset, shift;
	int sub_mask;
	int value;

	read = 0;
	field = 0;

	while (read < gc->ngpio) {
		shift = read % (8 * sizeof(*bits));
		offset = read / (8 * sizeof(*bits));
		sub_mask = (mask[offset] >> shift) & 0xffff;
		if (sub_mask) {
			value = bits[offset] >> shift;
			regmap_field_update_bits(ctrl->fields[RTL8231_FIELD_GPIO_DATA0 + field],
				sub_mask, value);
		}

		field += 1;
		read += 16;
	}
}

static int rtl8231_init(struct rtl8231_gpio_ctrl *ctrl)
{
	unsigned int v;
	int err;

	err = regmap_field_read(ctrl->fields[RTL8231_FIELD_READY_CODE], &v);
	if (err) {
		dev_err(ctrl->dev, "failed to read READY_CODE\n");
		return -ENODEV;
	} else if (v != RTL8231_READY_CODE_VALUE) {
		dev_err(ctrl->dev, "RTL8231 not present or ready 0x%x != 0x%x\n",
			v, RTL8231_READY_CODE_VALUE);
		return -ENODEV;
	}

	dev_info(ctrl->dev, "RTL8231 found\n");

	/* If the device was already configured, just leave it alone */
	err = regmap_field_read(ctrl->fields[RTL8231_FIELD_LED_START], &v);
	if (err)
		return err;
	else if (v)
		return 0;

	regmap_field_write(ctrl->fields[RTL8231_FIELD_SOFT_RESET], 1);
	regmap_field_write(ctrl->fields[RTL8231_FIELD_LED_START], 1);

	/* Select GPIO functionality for all pins and set to input */
	regmap_field_write(ctrl->fields[RTL8231_FIELD_PIN_MODE0], 0xffff);
	regmap_field_write(ctrl->fields[RTL8231_FIELD_GPIO_DIR0], 0xffff);
	regmap_field_write(ctrl->fields[RTL8231_FIELD_PIN_MODE1], 0xffff);
	regmap_field_write(ctrl->fields[RTL8231_FIELD_GPIO_DIR1], 0xffff);
	regmap_field_write(ctrl->fields[RTL8231_FIELD_PIN_MODE2], 0x1f);
	regmap_field_write(ctrl->fields[RTL8231_FIELD_GPIO_DIR2], 0x1f);

	return 0;
}

#define OF_COMPATIBLE_RTL8231_MDIO	"realtek,rtl8231-mdio"
#define OF_COMPATIBLE_RTL8231_I2C	"realtek,rtl8231-i2c"

static const struct of_device_id rtl8231_gpio_of_match[] = {
	{ .compatible = OF_COMPATIBLE_RTL8231_MDIO },
	{ .compatible = OF_COMPATIBLE_RTL8231_I2C },
	{},
};

MODULE_DEVICE_TABLE(of, rtl8231_gpio_of_match);

static struct regmap *rtl8231_gpio_regmap_mdio(struct device *dev, struct regmap_config *cfg)
{
	struct device_node *np = dev->of_node;
	struct device_node *expander_np = NULL;
	struct mdio_device *mdiodev;

	expander_np = of_parse_phandle(np, "dev-handle", 0);
	if (!expander_np) {
		dev_err(dev, "missing dev-handle node\n");
		return ERR_PTR(-EINVAL);
	}

	mdiodev = of_mdio_find_device(expander_np);
	of_node_put(expander_np);

	if (!mdiodev) {
		dev_err(dev, "failed to find MDIO device\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	cfg->reg_bits = 5;
	return devm_regmap_init_miim(mdiodev, cfg);
}

static struct regmap *rtl8231_gpio_regmap_i2c(struct device *dev, struct regmap_config *cfg)
{
	struct device_node *np = dev->of_node;
	struct i2c_client *i2cdev;
	struct regmap *map;
	u32 reg_width;

	// TODO untested
	i2cdev = of_find_i2c_device_by_node(np);
	if (IS_ERR(i2cdev)) {
		dev_err(dev, "failed to find I2C device\n");
		return ERR_PTR(-ENODEV);
	}

	/* Complete 7-bit I2C address is [1 0 1 0 A2 A1 A0] */
	if ((i2cdev->addr & ~(0x7)) != 0x50) {
		dev_err(dev, "invalid address\n");
		map = ERR_PTR(-EINVAL);
		goto regmap_i2c_out;
	}

	if (of_property_read_u32(np, "realtek,regnum-width", &reg_width)
		|| reg_width != 1 || reg_width != 2) {
		dev_err(dev, "invalid realtek,regnum-width\n");
		map = ERR_PTR(-EINVAL);
		goto regmap_i2c_out;
	}

	cfg->reg_bits = 8*reg_width;
	map = devm_regmap_init_i2c(i2cdev, cfg);

regmap_i2c_out:
	put_device(&i2cdev->dev);
	return map;
}

static int rtl8231_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regmap *map;
	struct regmap_config regmap_cfg = {};
	struct rtl8231_gpio_ctrl *ctrl;
	int field, err;
	u32 ngpios;

	if (!np) {
		dev_err(dev, "no DT node found\n");
		return -EINVAL;
	}

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ngpios = RTL8231_MAX_GPIOS;
	of_property_read_u32(np, "ngpios", &ngpios);
	if (ngpios > RTL8231_MAX_GPIOS) {
		dev_err(dev, "ngpios can be at most %d\n", RTL8231_MAX_GPIOS);
		return -EINVAL;
	}

	regmap_cfg.val_bits = 16;
	regmap_cfg.max_register = 30;
	regmap_cfg.cache_type = REGCACHE_NONE;
	regmap_cfg.num_ranges = 0;
	regmap_cfg.use_single_read = true;
	regmap_cfg.use_single_write = true;
	regmap_cfg.reg_format_endian = REGMAP_ENDIAN_BIG;
	regmap_cfg.val_format_endian = REGMAP_ENDIAN_BIG;

	if (of_device_is_compatible(np, OF_COMPATIBLE_RTL8231_MDIO)) {
		map = rtl8231_gpio_regmap_mdio(dev, &regmap_cfg);
	} else if (of_device_is_compatible(np, OF_COMPATIBLE_RTL8231_I2C)) {
		map = rtl8231_gpio_regmap_i2c(dev, &regmap_cfg);
	} else {
		dev_err(dev, "invalid bus type\n");
		return -ENOTSUPP;
	}

	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap\n");
		return PTR_ERR(map);
	}

	for (field = 0; field < RTL8231_FIELD_MAX; field++) {
		ctrl->fields[field] = devm_regmap_field_alloc(dev, map, rtl8231_fields[field]);
		if (IS_ERR(ctrl->fields[field])) {
			dev_err(dev, "unable to allocate regmap field\n");
			return PTR_ERR(ctrl->fields[field]);
		}
	}

	ctrl->dev = dev;
	err = rtl8231_init(ctrl);
	if (err < 0)
		return err;

	ctrl->gc.base = -1;
	ctrl->gc.ngpio = ngpios;
	ctrl->gc.label = "rtl8231-gpio";
	ctrl->gc.parent = dev;
	ctrl->gc.owner = THIS_MODULE;
	ctrl->gc.can_sleep = true;

	ctrl->gc.set = rtl8231_gpio_set;
	ctrl->gc.set_multiple = rtl8231_gpio_set_multiple;
	ctrl->gc.get = rtl8231_gpio_get;
	ctrl->gc.get_multiple = rtl8231_gpio_get_multiple;
	ctrl->gc.direction_input = rtl8231_direction_input;
	ctrl->gc.direction_output = rtl8231_direction_output;
	ctrl->gc.get_direction = rtl8231_get_direction;

	return devm_gpiochip_add_data(dev, &ctrl->gc, ctrl);
}

static struct platform_driver rtl8231_gpio_driver = {
	.driver = {
		.name = "rtl8231-expander",
		.of_match_table	= rtl8231_gpio_of_match,
	},
	.probe = rtl8231_gpio_probe,
};
module_platform_driver(rtl8231_gpio_driver);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Realtek RTL8231 GPIO and LED expander support");
MODULE_LICENSE("GPL v2");
