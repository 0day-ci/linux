// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/mfd/rtl8231.h>

#define RTL8231_ALL_PINS_MASK	GENMASK(RTL8231_BITS_VAL - 1, 0)
#define RTL8231_REAL_REG(reg)	(reg & GENMASK(4, 0))

/* Only specify non-volatile registers that non-zero or write-only */
static const struct reg_default rtl8231_reg_defaults[] = {
	{ .reg = RTL8231_REG_PIN_MODE1,       .def = 0xf840 },
	{ .reg = RTL8231_VREG_GPIO_DATA_OUT0, .def = 0x0000 },
	{ .reg = RTL8231_VREG_GPIO_DATA_OUT1, .def = 0x0000 },
	{ .reg = RTL8231_VREG_GPIO_DATA_OUT2, .def = 0x0000 },
};

static const struct regmap_range rtl8231_readable_ranges[] = {
	regmap_reg_range(RTL8231_REG_FUNC0, RTL8231_REG_LED_END),
	regmap_reg_range(RTL8231_REG_GPIO_DATA_IN0, RTL8231_REG_GPIO_DATA_IN2),
};

static const struct regmap_range rtl8231_non_readable_ranges[] = {
	regmap_reg_range(0x1f, 0x1f),
	regmap_reg_range(RTL8231_VREG(RTL8231_REG_FUNC0), RTL8231_VREG(RTL8231_REG_LED_END)),
	regmap_reg_range(RTL8231_VREG_GPIO_DATA_OUT0, RTL8231_VREG_GPIO_DATA_OUT2),
};

static const struct regmap_range rtl8231_writeable_ranges[] = {
	regmap_reg_range(RTL8231_REG_FUNC0, RTL8231_REG_LED_END),
	regmap_reg_range(RTL8231_VREG_GPIO_DATA_OUT0, RTL8231_VREG_GPIO_DATA_OUT2),
};

static const struct regmap_range rtl8231_non_writeable_ranges[] = {
	regmap_reg_range(0x1f, 0x1f),
	regmap_reg_range(RTL8231_VREG(RTL8231_REG_FUNC0), RTL8231_VREG(RTL8231_REG_LED_END)),
	regmap_reg_range(RTL8231_REG_GPIO_DATA_IN0, RTL8231_REG_GPIO_DATA_IN2),
};

static const struct regmap_access_table rtl8231_readable_table = {
	.yes_ranges = rtl8231_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rtl8231_readable_ranges),
	.no_ranges = rtl8231_non_readable_ranges,
	.n_no_ranges = ARRAY_SIZE(rtl8231_non_readable_ranges),
};

static const struct regmap_access_table rtl8231_writeable_table = {
	.yes_ranges = rtl8231_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rtl8231_writeable_ranges),
	.no_ranges = rtl8231_non_writeable_ranges,
	.n_no_ranges = ARRAY_SIZE(rtl8231_non_writeable_ranges),
};

static bool rtl8231_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	/* Registers with self-clearing bits, strapping pin values and inputs */
	case RTL8231_REG_FUNC0:
	case RTL8231_REG_FUNC1:
	case RTL8231_REG_PIN_HI_CFG:
	case RTL8231_REG_LED_END:
	case RTL8231_REG_GPIO_DATA_IN0:
	case RTL8231_REG_GPIO_DATA_IN1:
	case RTL8231_REG_GPIO_DATA_IN2:
		return true;
	default:
		return false;
	}
}

static int rtl8231_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mdio_device *mdio_dev = context;
	int ret;

	ret = mdiobus_read(mdio_dev->bus, mdio_dev->addr, RTL8231_REAL_REG(reg));

	if (ret < 0)
		return ret;

	*val = ret & 0xffff;

	return 0;
}

static int rtl8231_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct mdio_device *mdio_dev = context;

	return mdiobus_write(mdio_dev->bus, mdio_dev->addr, RTL8231_REAL_REG(reg), val);
}

static const struct reg_field RTL8231_FIELD_LED_START = REG_FIELD(RTL8231_REG_FUNC0, 1, 1);

static const struct mfd_cell rtl8231_cells[] = {
	{
		.name = "rtl8231-pinctrl",
	},
	{
		.name = "rtl8231-leds",
		.of_compatible = "realtek,rtl8231-leds",
	},
};

static int rtl8231_init(struct device *dev, struct regmap *map)
{
	unsigned int val;
	int err;

	err = regmap_read(map, RTL8231_REG_FUNC1, &val);
	if (err) {
		dev_err(dev, "failed to read READY_CODE\n");
		return err;
	}

	val = FIELD_GET(RTL8231_FUNC1_READY_CODE_MASK, val);
	if (val != RTL8231_FUNC1_READY_CODE_VALUE) {
		dev_err(dev, "RTL8231 not present or ready 0x%x != 0x%x\n",
			val, RTL8231_FUNC1_READY_CODE_VALUE);
		return -ENODEV;
	}

	/* SOFT_RESET bit self-clears when done */
	regmap_write_bits(map, RTL8231_REG_PIN_HI_CFG,
		RTL8231_PIN_HI_CFG_SOFT_RESET, RTL8231_PIN_HI_CFG_SOFT_RESET);
	err = regmap_read_poll_timeout(map, RTL8231_REG_PIN_HI_CFG, val,
		!(val & RTL8231_PIN_HI_CFG_SOFT_RESET), 50, 1000);
	if (err)
		return err;

	/*
	 * Chip reset results in a pin configuration that is a mix of LED and GPIO outputs.
	 * Select GPI functionality for all pins before enabling pin outputs.
	 */
	regmap_write(map, RTL8231_REG_PIN_MODE0, RTL8231_ALL_PINS_MASK);
	regmap_write(map, RTL8231_REG_GPIO_DIR0, RTL8231_ALL_PINS_MASK);
	regmap_write(map, RTL8231_REG_PIN_MODE1, RTL8231_ALL_PINS_MASK);
	regmap_write(map, RTL8231_REG_GPIO_DIR1, RTL8231_ALL_PINS_MASK);
	regmap_write(map, RTL8231_REG_PIN_HI_CFG,
		RTL8231_PIN_HI_CFG_MODE_MASK | RTL8231_PIN_HI_CFG_DIR_MASK);

	return 0;
}

static const struct regmap_config rtl8231_mdio_regmap_config = {
	.val_bits = RTL8231_BITS_VAL,
	.reg_bits = RTL8231_BITS_REG,
	.volatile_reg = rtl8231_volatile_reg,
	.rd_table = &rtl8231_readable_table,
	.wr_table = &rtl8231_writeable_table,
	.max_register = RTL8231_VREG(RTL8231_REG_COUNT - 1),
	.use_single_read = true,
	.use_single_write = true,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.reg_read = rtl8231_reg_read,
	.reg_write = rtl8231_reg_write,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = rtl8231_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rtl8231_reg_defaults),
};

static int rtl8231_mdio_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct regmap_field *led_start;
	struct regmap *map;
	int err;

	map = devm_regmap_init_mdio(mdiodev, &rtl8231_mdio_regmap_config);
	if (IS_ERR(map)) {
		dev_err(dev, "failed to init regmap\n");
		return PTR_ERR(map);
	}

	led_start = devm_regmap_field_alloc(dev, map, RTL8231_FIELD_LED_START);
	if (IS_ERR(led_start))
		return PTR_ERR(led_start);

	dev_set_drvdata(dev, led_start);

	mdiodev->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	device_property_read_u32(dev, "reset-assert-delay", &mdiodev->reset_assert_delay);
	device_property_read_u32(dev, "reset-deassert-delay", &mdiodev->reset_deassert_delay);

	err = rtl8231_init(dev, map);
	if (err)
		return err;

	/* LED_START enables power to output pins, and starts the LED engine */
	regmap_field_force_write(led_start, 1);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, rtl8231_cells,
		ARRAY_SIZE(rtl8231_cells), NULL, 0, NULL);
}

__maybe_unused static int rtl8231_suspend(struct device *dev)
{
	struct regmap_field *led_start = dev_get_drvdata(dev);

	return regmap_field_force_write(led_start, 0);
}

__maybe_unused static int rtl8231_resume(struct device *dev)
{
	struct regmap_field *led_start = dev_get_drvdata(dev);

	return regmap_field_force_write(led_start, 1);
}

static SIMPLE_DEV_PM_OPS(rtl8231_pm_ops, rtl8231_suspend, rtl8231_resume);

static const struct of_device_id rtl8231_of_match[] = {
	{ .compatible = "realtek,rtl8231" },
	{}
};
MODULE_DEVICE_TABLE(of, rtl8231_of_match);

static struct mdio_driver rtl8231_mdio_driver = {
	.mdiodrv.driver = {
		.name = "rtl8231-expander",
		.of_match_table	= rtl8231_of_match,
		.pm = pm_ptr(&rtl8231_pm_ops),
	},
	.probe = rtl8231_mdio_probe,
};
mdio_module_driver(rtl8231_mdio_driver);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Realtek RTL8231 GPIO and LED expander");
MODULE_LICENSE("GPL v2");
