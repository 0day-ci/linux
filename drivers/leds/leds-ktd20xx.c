// SPDX-License-Identifier: GPL-2.0-only
/*
 *  LEDs driver for the Kinetic KDT20xx device
 *
 *  Copyright (C) 2021 TDT AG Florian Eckert <fe@dev.tdt.de>
 *
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>

/* I2C Register Map */
#define KTD20XX_ID		0x00
#define KTD20XX_MONITOR		0x01
#define KTD20XX_CONTROL		0x02

/* Color0 Configuration Registers */
#define KTD20XX_IRED0		0x03
#define KTD20XX_IGRN0		0x04
#define KTD20XX_IBLU0		0x05

/* Color1 Configuration Registers */
#define KTD20XX_IRED1		0x06
#define KTD20XX_IGRN1		0x07
#define KTD20XX_IBLU1		0x08

/* Selection Configuration Register */
#define KTD20XX_ISELA12		0x09
#define KTD20XX_ISELA34		0x0A
#define KTD20XX_ISELB12		0x0B
#define KTD20XX_ISELB34		0x0C
#define KTD20XX_ISELC12		0x0D
#define KTD20XX_ISELC34		0x0E

#define KTD20XX_MAX_LEDS	12
#define KTD20XX_LED_CHANNELS	3

enum ktd20xx_led_number {
	/* ISELA12 */
	RGB_A1,
	RGB_A2,
	/* ISELA34 */
	RGB_A3,
	RGB_A4,
	/* ISELB12 */
	RGB_B1,
	RGB_B2,
	/* ISELB34 */
	RGB_B3,
	RGB_B4,
	/* ISELC12 */
	RGB_C1,
	RGB_C2,
	/* ISELC34 */
	RGB_C3,
	RGB_C4,
};

enum ktd20xx_control_mode {
	CONTROL_MODE_OFF,
	CONTROL_MODE_NIGHT,
	CONTROL_MODE_NORMAL,
	CONTROL_MODE_RESET,
};

static const struct reg_default ktd20xx_reg_defs[] = {
	/* Color0 Configuration Registers */
	{KTD20XX_IRED0, 0x28},
	{KTD20XX_IGRN0, 0x28},
	{KTD20XX_IBLU0, 0x28},
	/* Color1 Configuration Registers */
	{KTD20XX_IRED1, 0x60},
	{KTD20XX_IGRN1, 0x60},
	{KTD20XX_IBLU1, 0x60},
	/* Selection Configuration Register */
	{KTD20XX_ISELA12, 0x00},
	{KTD20XX_ISELA34, 0x00},
	{KTD20XX_ISELB12, 0x00},
	{KTD20XX_ISELB34, 0x00},
	{KTD20XX_ISELC12, 0x00},
	{KTD20XX_ISELC34, 0x00}
};

/* Chip values */
static const struct reg_field kt20xx_control_mode = REG_FIELD(KTD20XX_CONTROL, 6, 7);
static const struct reg_field kt20xx_faderate = REG_FIELD(KTD20XX_CONTROL, 0, 2);
static const struct reg_field kt20xx_vendor = REG_FIELD(KTD20XX_ID, 5, 7);
static const struct reg_field kt20xx_chip_id = REG_FIELD(KTD20XX_ID, 0, 4);
static const struct reg_field kt20xx_chip_rev = REG_FIELD(KTD20XX_MONITOR, 4, 7);

/* ISELA1 and ISELA2 */
static const struct reg_field kt20xx_a1_select = REG_FIELD(KTD20XX_ISELA12, 4, 6);
static const struct reg_field kt20xx_a1_enable = REG_FIELD(KTD20XX_ISELA12, 7, 7);
static const struct reg_field kt20xx_a2_select = REG_FIELD(KTD20XX_ISELA12, 0, 2);
static const struct reg_field kt20xx_a2_enable = REG_FIELD(KTD20XX_ISELA12, 3, 3);

/* ISELA3 and ISELA4 */
static const struct reg_field kt20xx_a3_select = REG_FIELD(KTD20XX_ISELA34, 4, 6);
static const struct reg_field kt20xx_a3_enable = REG_FIELD(KTD20XX_ISELA34, 7, 7);
static const struct reg_field kt20xx_a4_select = REG_FIELD(KTD20XX_ISELA34, 0, 2);
static const struct reg_field kt20xx_a4_enable = REG_FIELD(KTD20XX_ISELA34, 3, 3);

/* ISELB1 and ISELB2 */
static const struct reg_field kt20xx_b1_select = REG_FIELD(KTD20XX_ISELB12, 4, 6);
static const struct reg_field kt20xx_b1_enable = REG_FIELD(KTD20XX_ISELB12, 7, 7);
static const struct reg_field kt20xx_b2_select = REG_FIELD(KTD20XX_ISELB12, 0, 2);
static const struct reg_field kt20xx_b2_enable = REG_FIELD(KTD20XX_ISELB12, 3, 3);

/* ISELB3 and ISELB4 */
static const struct reg_field kt20xx_b3_select = REG_FIELD(KTD20XX_ISELB34, 4, 6);
static const struct reg_field kt20xx_b3_enable = REG_FIELD(KTD20XX_ISELB34, 7, 7);
static const struct reg_field kt20xx_b4_select = REG_FIELD(KTD20XX_ISELB34, 0, 2);
static const struct reg_field kt20xx_b4_enable = REG_FIELD(KTD20XX_ISELB34, 3, 3);

/* ISELC1 and ISELC2 */
static const struct reg_field kt20xx_c1_select = REG_FIELD(KTD20XX_ISELC12, 4, 6);
static const struct reg_field kt20xx_c1_enable = REG_FIELD(KTD20XX_ISELC12, 7, 7);
static const struct reg_field kt20xx_c2_select = REG_FIELD(KTD20XX_ISELC12, 0, 2);
static const struct reg_field kt20xx_c2_enable = REG_FIELD(KTD20XX_ISELC12, 3, 3);

/* ISELC3 and ISELC4 */
static const struct reg_field kt20xx_c3_select = REG_FIELD(KTD20XX_ISELC34, 4, 6);
static const struct reg_field kt20xx_c3_enable = REG_FIELD(KTD20XX_ISELC34, 7, 7);
static const struct reg_field kt20xx_c4_select = REG_FIELD(KTD20XX_ISELC34, 0, 2);
static const struct reg_field kt20xx_c4_enable = REG_FIELD(KTD20XX_ISELC34, 3, 3);

static const struct regmap_range ktd20xx_volatile_ranges = {
	.range_min = KTD20XX_ID,
	.range_max = KTD20XX_CONTROL,
};

static const struct regmap_access_table ktd20xx_volatile_table = {
	.yes_ranges = &ktd20xx_volatile_ranges,
	.n_yes_ranges = 1,
};

static const struct regmap_range ktd20xx_readable_ranges = {
	.range_min = KTD20XX_ID,
	.range_max = KTD20XX_MONITOR,
};

static const struct regmap_access_table ktd20xx_readable_table = {
	.yes_ranges = &ktd20xx_readable_ranges,
	.n_yes_ranges = 1,
};

static const struct regmap_config ktd20xx_regmap_config = {
	.name = "ktd20xx_regmap",
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = KTD20XX_ISELC34,

	.volatile_table = &ktd20xx_volatile_table,
	.rd_table = &ktd20xx_readable_table,

	.reg_defaults = ktd20xx_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(ktd20xx_reg_defs),
	.cache_type = REGCACHE_FLAT,
};

struct ktd20xx_led {
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled_info[KTD20XX_LED_CHANNELS];
	int index;
	struct regmap_field *enable;
	struct regmap_field *select;
	struct ktd20xx *chip;
};

struct ktd20xx {
	struct mutex lock;
	struct i2c_client *client;
	struct regmap *regmap;
	struct regmap_field *control_mode;
	struct regmap_field *faderate;
	struct regmap_field *vendor;
	struct regmap_field *chip_id;
	struct regmap_field *chip_rev;
	struct ktd20xx_led leds[KTD20XX_MAX_LEDS];
};

static int ktd20xx_hwinit(struct ktd20xx *chip)
{
	struct device *dev = &chip->client->dev;
	int ret;
	unsigned int value;

	/*
	 * If the device tree property 'kinetc,led-current' is found
	 * then set this value into the color0 register as the max current
	 * for all color channel LEDs. If this poperty is not set then
	 * use the default value 0x28 set by the chip after a hardware reset.
	 * The hardware default value 0x28 corresponds to 5mA.
	 */
	/* Set color1 register current value to 0x00 and therefor 0mA */
	regmap_write(chip->regmap, KTD20XX_IRED1, 0);
	regmap_write(chip->regmap, KTD20XX_IGRN1, 0);
	regmap_write(chip->regmap, KTD20XX_IBLU1, 0);

	ret = device_property_read_u32(dev, "kinetic,led-current", &value);
	if (ret) {
		dev_warn(dev, "property 'kinetic,led-current' not found. Using default hardware value 0x28 (5mA).\n");
	} else {
		dev_dbg(dev, "property 'kinetic,led-current' found. Using value 0x%02x.\n",
				value);
		regmap_write(chip->regmap, KTD20XX_IRED0, value);
		regmap_write(chip->regmap, KTD20XX_IGRN0, value);
		regmap_write(chip->regmap, KTD20XX_IBLU0, value);
	}

	/* Enable chip to run in 'normal mode' */
	regmap_field_write(chip->control_mode, CONTROL_MODE_NORMAL);

	return 0;
}

static struct ktd20xx_led *mcled_cdev_to_led(struct led_classdev_mc *mc_cdev)
{
	return container_of(mc_cdev, struct ktd20xx_led, mc_cdev);
}

static int ktd20xx_brightness_set(struct led_classdev *cdev,
		enum led_brightness brightness)
{
	struct led_classdev_mc *mc_dev = lcdev_to_mccdev(cdev);
	struct ktd20xx_led *led = mcled_cdev_to_led(mc_dev);
	struct device *dev = &led->chip->client->dev;
	int ret;
	int i;
	unsigned long rgb = 0;

	mutex_lock(&led->chip->lock);
	ret = regmap_field_write(led->enable, brightness ? 1 : 0);
	if (ret) {
		dev_err(dev, "Cannot set enable flag of LED %d error: %d\n",
				led->index, ret);
		goto unlock_out;
	}

	for (i = 0; i < led->mc_cdev.num_colors; i++) {
		unsigned int intensity = mc_dev->subled_info[i].intensity;
		unsigned int channel = mc_dev->subled_info[i].channel;

		if (intensity > 0)
			set_bit(channel, &rgb);
	}

	/*
	 * To use the color0 registers default value after an hadware reset,
	 * if the device tree property 'kinetc,led-current' is not set,
	 * we have to 'invert' the rgb channel!
	 */
	ret = regmap_field_write(led->select, ~rgb);
	if (ret) {
		dev_err(dev, "Can not set RGB for LED %d error: %d\n",
				led->index, ret);
		goto unlock_out;
	}

unlock_out:
	mutex_unlock(&led->chip->lock);
	return ret;
}

static int ktd20xx_probe_dt(struct ktd20xx *chip)
{
	struct fwnode_handle *child = NULL;
	struct led_init_data init_data = {};
	struct led_classdev *led_cdev;
	struct ktd20xx_led *led;
	struct device *dev = &chip->client->dev;
	int color;
	int ret;
	int i = 0;

	device_for_each_child_node(dev, child) {
		led = &chip->leds[i];

		ret = fwnode_property_read_u32(child, "reg", &led->index);
		if (ret) {
			dev_err(dev, "missing property 'reg'\n");
			goto child_out;
		}
		if (led->index >= KTD20XX_MAX_LEDS) {
			dev_warn(dev, "property 'reg' is greater then '%i'\n",
					KTD20XX_MAX_LEDS);
			ret = -EINVAL;
			goto child_out;
		}

		ret = fwnode_property_read_u32(child, "color", &color);
		if (ret) {
			dev_err(dev, "missing property 'color'\n");
			goto child_out;
		}
		if (color != LED_COLOR_ID_MULTI) {
			dev_warn(dev, "property 'color' is not equal to the value 'LED_COLOR_ID_MULTI'\n");
			ret = -EINVAL;
			goto child_out;
		}

		led->subled_info[0].color_index = LED_COLOR_ID_RED;
		led->subled_info[0].channel = 2;
		led->subled_info[0].intensity = 1;
		led->subled_info[1].color_index = LED_COLOR_ID_GREEN;
		led->subled_info[1].channel = 1;
		led->subled_info[1].intensity = 1;
		led->subled_info[2].color_index = LED_COLOR_ID_BLUE;
		led->subled_info[2].channel = 0;
		led->subled_info[2].intensity = 1;

		led->mc_cdev.subled_info = led->subled_info;
		led->mc_cdev.num_colors = KTD20XX_LED_CHANNELS;

		init_data.fwnode = child;

		led->chip = chip;
		led_cdev = &led->mc_cdev.led_cdev;
		led_cdev->brightness_set_blocking = ktd20xx_brightness_set;

		switch (led->index) {
		case RGB_A1:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a1_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a1_enable);
			break;
		case RGB_A2:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a2_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a2_enable);
			break;
		case RGB_A3:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a3_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a3_enable);
			break;
		case RGB_A4:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a4_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_a4_enable);
			break;
		case RGB_B1:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b1_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b1_enable);
			break;
		case RGB_B2:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b2_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b2_enable);
			break;
		case RGB_B3:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b3_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b3_enable);
			break;
		case RGB_B4:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b4_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_b4_enable);
			break;
		case RGB_C1:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c1_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c1_enable);
			break;
		case RGB_C2:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c2_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c2_enable);
			break;
		case RGB_C3:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c3_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c3_enable);
			break;
		case RGB_C4:
			led->select = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c4_select);
			led->enable = devm_regmap_field_alloc(dev,
					chip->regmap, kt20xx_c4_enable);
			break;
		}

		ret = devm_led_classdev_multicolor_register_ext(dev,
			&led->mc_cdev,
			&init_data);

		if (ret) {
			dev_err(dev, "led register err: %d\n", ret);
			goto child_out;
		}

		i++;
	}

	return 0;

child_out:
	fwnode_handle_put(child);
	return ret;
}

/*
 * The chip also offers the option "Night Mode".
 * All LED current settings are divided by 16 for a 0 to 1.5mA current
 * setting range.
 */
static ssize_t nightmode_show(struct device *dev, struct device_attribute *a,
		char *buf)
{
	struct ktd20xx *chip = dev_get_drvdata(dev);
	unsigned int value;

	mutex_lock(&chip->lock);
	regmap_field_read(chip->control_mode, &value);
	mutex_unlock(&chip->lock);

	return sysfs_emit(buf, "%d\n", value == CONTROL_MODE_NIGHT ? 1 : 0);
}

static ssize_t nightmode_store(struct device *dev, struct device_attribute *a,
		const char *buf, size_t count)
{
	struct ktd20xx *chip = dev_get_drvdata(dev);
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	mutex_lock(&chip->lock);
	ret = regmap_field_write(chip->control_mode,
			value == 1 ? CONTROL_MODE_NIGHT : CONTROL_MODE_NORMAL);
	mutex_unlock(&chip->lock);

	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(nightmode);

static struct attribute *ktd20xx_led_controller_attrs[] = {
	&dev_attr_nightmode.attr,
	NULL
};
ATTRIBUTE_GROUPS(ktd20xx_led_controller);

static int ktd20xx_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ktd20xx *chip;
	unsigned int vendor;
	unsigned int chip_id;
	unsigned int chip_rev;
	struct device *dev;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	mutex_init(&chip->lock);
	chip->client = client;
	dev = &client->dev;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &ktd20xx_regmap_config);
	if (IS_ERR(chip->regmap)) {
		dev_err_probe(dev, PTR_ERR(chip->regmap),
			"Failed to allocate register map\n");
		goto error;
	}

	chip->control_mode = devm_regmap_field_alloc(dev, chip->regmap,
			kt20xx_control_mode);
	chip->faderate = devm_regmap_field_alloc(dev, chip->regmap,
			kt20xx_faderate);
	chip->vendor = devm_regmap_field_alloc(dev, chip->regmap,
			kt20xx_vendor);
	chip->chip_id = devm_regmap_field_alloc(dev, chip->regmap,
			kt20xx_chip_id);
	chip->chip_rev = devm_regmap_field_alloc(dev, chip->regmap,
			kt20xx_chip_rev);

	/* Reset all registers to hardware device default settings */
	regmap_field_write(chip->control_mode, CONTROL_MODE_RESET);

	ret = regmap_field_read(chip->vendor, &vendor);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to read vendor\n");
		goto error;
	}

	ret = regmap_field_read(chip->chip_id, &chip_id);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to read chip id\n");
		goto error;
	}

	ret = regmap_field_read(chip->chip_rev, &chip_rev);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to read chip rev\n");
		goto error;
	}

	dev_info(dev, "vendor: 0x%02x chip-id: 0x%02x chip-rev: 0x%02x\n",
			vendor, chip_id, chip_rev);

	ret = ktd20xx_probe_dt(chip);
	if (ret)
		goto error;

	ret = ktd20xx_hwinit(chip);
	if (ret)
		goto error;

	return 0;

error:
	return ret;
}

static int ktd20xx_remove(struct i2c_client *client)
{
	struct ktd20xx *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	regmap_field_write(chip->control_mode, CONTROL_MODE_OFF);
	mutex_unlock(&chip->lock);

	return 0;
}

static const struct i2c_device_id ktd20xx_id[] = {
	{ "ktd20xx", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ktd20xx_id);

static const struct of_device_id of_ktd20xx_leds_match[] = {
	{ .compatible = "kinetic,ktd20xx", },
	{}
};
MODULE_DEVICE_TABLE(of, of_ktd20xx_leds_match);

static struct i2c_driver ktd20xx_driver = {
	.driver = {
		.name = "ktd20xx",
		.dev_groups = ktd20xx_led_controller_groups,
		.of_match_table = of_ktd20xx_leds_match,
	},
	.probe = ktd20xx_probe,
	.remove = ktd20xx_remove,
	.id_table = ktd20xx_id
};
module_i2c_driver(ktd20xx_driver);

MODULE_DESCRIPTION("Kinetic KTD20xx LED driver");
MODULE_AUTHOR("Florian Eckert <fe@dev.tdt.de>");
MODULE_LICENSE("GPL v2");
