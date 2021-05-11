// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rtl8231.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static const struct reg_field RTL8231_FIELD_LED_START = REG_FIELD(RTL8231_REG_FUNC0, 1, 1);
static const struct reg_field RTL8231_FIELD_READY_CODE = REG_FIELD(RTL8231_REG_FUNC1, 4, 9);
static const struct reg_field RTL8231_FIELD_SOFT_RESET = REG_FIELD(RTL8231_REG_PIN_HI_CFG, 15, 15);

static const struct mfd_cell rtl8231_cells[] = {
	{
		.name = "rtl8231-pinctrl",
		.of_compatible = "realtek,rtl8231-pinctrl",
	},
	{
		.name = "rtl8231-leds",
		.of_compatible = "realtek,rtl8231-leds",
	},
};

static int rtl8231_init(struct device *dev, struct regmap *map)
{
	struct regmap_field *field_ready_code;
	struct regmap_field *field_soft_reset;
	unsigned int v;
	int err = 0;

	field_ready_code = regmap_field_alloc(map, RTL8231_FIELD_READY_CODE);
	if (IS_ERR(field_ready_code))
		return PTR_ERR(field_ready_code);

	field_soft_reset = regmap_field_alloc(map, RTL8231_FIELD_SOFT_RESET);
	if (IS_ERR(field_soft_reset)) {
		err = PTR_ERR(field_soft_reset);
		goto init_out;
	}

	err = regmap_field_read(field_ready_code, &v);

	if (err) {
		dev_err(dev, "failed to read READY_CODE\n");
		goto init_out;
	} else if (v != RTL8231_FUNC1_READY_CODE_VALUE) {
		dev_err(dev, "RTL8231 not present or ready 0x%x != 0x%x\n",
			v, RTL8231_FUNC1_READY_CODE_VALUE);
		err = -ENODEV;
		goto init_out;
	}

	// TODO Implement reset-gpios
	regmap_field_write(field_soft_reset, 1);
	usleep_range(1000, 10000);

	/* Do not write LED_START before configuring pins */
	/* Select GPIO functionality for all pins and set to input */
	regmap_write(map, RTL8231_REG_PIN_MODE0, 0xffff);
	regmap_write(map, RTL8231_REG_GPIO_DIR0, 0xffff);
	regmap_write(map, RTL8231_REG_PIN_MODE1, 0xffff);
	regmap_write(map, RTL8231_REG_GPIO_DIR1, 0xffff);
	regmap_write(map, RTL8231_REG_PIN_HI_CFG, GENMASK(4, 0) | GENMASK(9, 5));

init_out:
	regmap_field_free(field_ready_code);
	regmap_field_free(field_soft_reset);
	return err;
}

static int rtl8231_mdio_reg_read(void *ctx, unsigned int reg, unsigned int *val)
{
	struct mdio_device *mdiodev = ctx;
	int ret;

	ret = mdiobus_read(mdiodev->bus, mdiodev->addr, reg);
	if (ret < 0)
		return ret;

	*val = ret & 0xffff;
	return 0;
}

static int rtl8231_mdio_reg_write(void *ctx, unsigned int reg, unsigned int val)
{
	struct mdio_device *mdiodev = ctx;

	return mdiobus_write(mdiodev->bus, mdiodev->addr, reg, val);
}

static const struct regmap_config rtl8231_regmap_config = {
	.val_bits = 16,
	.reg_bits = 5,
	.max_register = RTL8231_REG_COUNT - 1,
	.use_single_read = true,
	.use_single_write = true,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = rtl8231_mdio_reg_read,
	.reg_write = rtl8231_mdio_reg_write,
};

static int rtl8231_mdio_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct regmap_field *led_start;
	struct regmap *map;
	int err;

	map = devm_regmap_init(dev, NULL, mdiodev, &rtl8231_regmap_config);

	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap\n");
		return PTR_ERR(map);
	}

	led_start = devm_regmap_field_alloc(dev, map, RTL8231_FIELD_LED_START);
	if (IS_ERR(led_start))
		return PTR_ERR(led_start);

	dev_set_drvdata(dev, led_start);

	err = rtl8231_init(dev, map);
	if (err)
		return err;

	/* LED_START enables power to output pins, and starts the LED engine */
	regmap_field_write(led_start, 1);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, rtl8231_cells,
		ARRAY_SIZE(rtl8231_cells), NULL, 0, NULL);
}

static void rtl8231_mdio_remove(struct mdio_device *mdiodev)
{
	struct regmap_field *led_start;

	led_start = dev_get_drvdata(&mdiodev->dev);
	regmap_field_write(led_start, 0);
}

static const struct of_device_id rtl8231_of_match[] = {
	{ .compatible = "realtek,rtl8231" },
	{},
};
MODULE_DEVICE_TABLE(of, rtl8231_of_match);

static struct mdio_driver rtl8231_mdio_driver = {
	.mdiodrv.driver = {
		.name = "rtl8231-expander",
		.of_match_table	= rtl8231_of_match,
	},
	.probe = rtl8231_mdio_probe,
	.remove = rtl8231_mdio_remove,
};
mdio_module_driver(rtl8231_mdio_driver);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Realtek RTL8231 GPIO and LED expander");
MODULE_LICENSE("GPL v2");
