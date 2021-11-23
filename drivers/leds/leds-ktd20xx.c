// SPDX-License-Identifier: GPL-2.0-only
/*
 *  LEDs driver for the Kinetic KDT20xx device
 *
 *  Copyright (C) 2021 TDT AG
 *       Florian Eckert <fe@dev.tdt.de>
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/mutex.h>
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

enum ktd20xx_rgb {
	RED,
	GRN,
	BLU,
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
	struct device *dev;
	struct regmap_field *control_mode;
	struct regmap_field *faderate;
	struct regmap_field *vendor;
	struct regmap_field *chip_id;
	struct regmap_field *chip_rev;
	struct ktd20xx_led leds[KTD20XX_MAX_LEDS];
};

static int ktd20xx_hwinit(struct ktd20xx *chip)
{
	struct fwnode_handle *fwnode;
	struct device *dev = &chip->client->dev;
	u8 value[3];

	fwnode = dev_fwnode(dev);

	if (fwnode_property_read_u8_array(fwnode, "kinetic,color-current0", value, 3)) {
		dev_warn(dev, "no kinetic,color-current0 property, use chip default value 0x28.\n");
	} else {
		regmap_write(chip->regmap, KTD20XX_IRED0, value[0]);
		regmap_write(chip->regmap, KTD20XX_IGRN0, value[1]);
		regmap_write(chip->regmap, KTD20XX_IBLU0, value[2]);
	}

	if (fwnode_property_read_u8_array(fwnode, "kinetic,color-current1", value, 3)) {
		dev_warn(dev, "no kinetic,color-current1 property, use chip default value 0x60.\n");
	} else {
		regmap_write(chip->regmap, KTD20XX_IRED1, value[0]);
		regmap_write(chip->regmap, KTD20XX_IGRN1, value[1]);
		regmap_write(chip->regmap, KTD20XX_IBLU1, value[2]);
	}

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
	struct device *dev = led->chip->dev;
	int ret = 0;
	int i = 0;
	unsigned int rgb = 0;
	unsigned int bit = 0;

	mutex_lock(&led->chip->lock);
	if (brightness > 0)
		ret = regmap_field_write(led->enable, 1);
	else
		ret = regmap_field_write(led->enable, 0);

	if (ret) {
		dev_err(dev, "Cannot set enable flag of LED %d error: %d\n",
				led->index, ret);
		goto out;
	}

	for (i = 0; i < led->mc_cdev.num_colors; i++) {
		unsigned int intensity = mc_dev->subled_info[i].intensity;

		switch (i) {
		case RED:
			bit = 4;
			break;
		case GRN:
			bit = 2;
			break;
		case BLU:
			bit = 1;
			break;
		}

		if (intensity > 0)
			rgb += bit;
	}

	ret = regmap_field_write(led->select, rgb);
	if (ret) {
		dev_err(dev, "Can not set RGB for LED %d error: %d\n",
				led->index, ret);
		goto out;
	}

out:
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
	u8 value[3] = { 0, 0, 0 };
	int i = 0;
	int ret = -EINVAL;
	int color;

	device_for_each_child_node(chip->dev, child) {
		led = &chip->leds[i];

		ret = fwnode_property_read_u32(child, "reg", &led->index);
		if (ret) {
			dev_err(dev, "missing property: reg\n");
			goto child_out;
		}
		if (led->index >= KTD20XX_MAX_LEDS) {
			dev_warn(dev, "property 'reg' must contain value between '0' and '%i'\n",
					KTD20XX_MAX_LEDS);
			ret = -EINVAL;
			goto child_out;
		}

		ret = fwnode_property_read_u32(child, "color", &color);
		if (ret) {
			dev_err(dev, "missing property: color\n");
			goto child_out;
		}
		if (color != LED_COLOR_ID_RGB) {
			dev_warn(dev, "property 'color' must contain value 'LED_COLOR_ID_RGB'\n");
			ret = -EINVAL;
			goto child_out;
		}

		/* Get default color register selection */
		if (fwnode_property_read_u8_array(child, "kinetic,color-selection", value, 3))
			dev_warn(dev, "no kinetic,color-selection property found, use default rgbt color selection from register 0.\n");

		led->subled_info[0].color_index = LED_COLOR_ID_RED;
		led->subled_info[0].channel = 0;
		led->subled_info[0].intensity = value[0];
		led->subled_info[1].color_index = LED_COLOR_ID_GREEN;
		led->subled_info[1].channel = 1;
		led->subled_info[1].intensity = value[1];
		led->subled_info[2].color_index = LED_COLOR_ID_BLUE;
		led->subled_info[2].channel = 2;
		led->subled_info[2].intensity = value[2];

		led->mc_cdev.subled_info = led->subled_info;
		led->mc_cdev.num_colors = KTD20XX_LED_CHANNELS;

		init_data.fwnode = child;

		led->chip = chip;
		led_cdev = &led->mc_cdev.led_cdev;
		led_cdev->brightness_set_blocking = ktd20xx_brightness_set;

		switch (led->index) {
		case RGB_A1:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a1_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a1_enable);
			break;
		case RGB_A2:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a2_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a2_enable);
			break;
		case RGB_A3:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a3_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a3_enable);
			break;
		case RGB_A4:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a4_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_a4_enable);
			break;
		case RGB_B1:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b1_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b1_enable);
			break;
		case RGB_B2:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b2_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b2_enable);
			break;
		case RGB_B3:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b3_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b3_enable);
			break;
		case RGB_B4:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b4_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_b4_enable);
			break;
		case RGB_C1:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c1_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c1_enable);
			break;
		case RGB_C2:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c2_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c2_enable);
			break;
		case RGB_C3:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c3_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c3_enable);
			break;
		case RGB_C4:
			led->select = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c4_select);
			led->enable = devm_regmap_field_alloc(chip->dev,
					chip->regmap, kt20xx_c4_enable);
			break;
		}

		ret = devm_led_classdev_multicolor_register_ext(&chip->client->dev,
			&led->mc_cdev,
			&init_data);

		if (ret) {
			dev_err(&chip->client->dev, "led register err: %d\n", ret);
			goto child_out;
		}

		i++;
		fwnode_handle_put(child);
	}

	return 0;

child_out:
	fwnode_handle_put(child);
	return ret;
}

/* Device attribute for color0 register
 *
 * The device attribute colour1 is intended to adjust the colour space.
 * The colour strength can be controlled via the current in 125uA steps.
 * The maximum current for the individual channels is limited to 24mA.
 * To set a new RGB value, 3 values must be passed. This value may not be
 * less than 0 and also not greater than 194. The chip can only process the
 * maximum current of 24mA. This means that any value greater than 194
 * cannot be set.
 */
static ssize_t current_color0_show(struct device *dev,
		struct device_attribute *a,
		char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned int value;
	int len = 0;

	mutex_lock(&chip->lock);
	regmap_read(chip->regmap, KTD20XX_IRED0, &value);
	len += sprintf(buf + len, "%d", value);
	len += sprintf(buf + len, " ");

	regmap_read(chip->regmap, KTD20XX_IGRN0, &value);
	len += sprintf(buf + len, "%d", value);
	len += sprintf(buf + len, " ");

	regmap_read(chip->regmap, KTD20XX_IBLU0, &value);
	len += sprintf(buf + len, "%d", value);
	mutex_unlock(&chip->lock);

	buf[len++] = '\n';
	return len;
}

static ssize_t current_color0_store(struct device *dev,
		struct device_attribute *a,
		const char *buf, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned int value[3];
	int i;
	ssize_t ret;

	ret = sscanf(buf, "%u %u %u", &value[0], &value[1], &value[2]);
	if (ret < 3) {
		ret = -EINVAL;
		goto err_out;
	}

	for (i = 0; i < 3; i++) {
		if (value[i] > 194) {
			ret = -EINVAL;
			goto err_out;
		}
	}

	mutex_lock(&chip->lock);
	regmap_write(chip->regmap, KTD20XX_IRED0, value[0]);
	regmap_write(chip->regmap, KTD20XX_IGRN0, value[1]);
	regmap_write(chip->regmap, KTD20XX_IBLU0, value[2]);
	mutex_unlock(&chip->lock);
	return size;

err_out:
	return ret;
}
static DEVICE_ATTR_RW(current_color0);

/* Device attribute for color1 register
 *
 * The device attribute colour1 is intended to adjust the colour space.
 * The colour strength can be controlled via the current in 125uA steps.
 * The maximum current for the individual channels is limited to 24mA.
 * To set a new RGB value, 3 values must be passed. This value may not be
 * less than 0 and also not greater than 194. The chip can only process the
 * maximum current of 24mA. This means that any value greater than 194
 * cannot be set.
 */
static ssize_t current_color1_show(struct device *dev,
		struct device_attribute *a,
		char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned int value;
	int len = 0;

	mutex_lock(&chip->lock);
	regmap_read(chip->regmap, KTD20XX_IRED1, &value);
	len += sprintf(buf + len, "%d", value);
	len += sprintf(buf + len, " ");

	regmap_read(chip->regmap, KTD20XX_IGRN1, &value);
	len += sprintf(buf + len, "%d", value);
	len += sprintf(buf + len, " ");

	regmap_read(chip->regmap, KTD20XX_IBLU1, &value);
	len += sprintf(buf + len, "%d", value);
	mutex_unlock(&chip->lock);

	buf[len++] = '\n';
	return len;
}
static ssize_t current_color1_store(struct device *dev,
		struct device_attribute *a,
		const char *buf, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned int value[3];
	int i;
	ssize_t ret;

	ret = sscanf(buf, "%u %u %u", &value[0], &value[1], &value[2]);
	if (ret < 3) {
		ret = -EINVAL;
		goto err_out;
	}

	for (i = 0; i < 3; i++) {
		if (value[i] > 194) {
			ret = -EINVAL;
			goto err_out;
		}
	}

	mutex_lock(&chip->lock);
	regmap_write(chip->regmap, KTD20XX_IRED1, value[0]);
	regmap_write(chip->regmap, KTD20XX_IGRN1, value[1]);
	regmap_write(chip->regmap, KTD20XX_IBLU1, value[2]);
	mutex_unlock(&chip->lock);
	return size;

err_out:
	return ret;
}
static DEVICE_ATTR_RW(current_color1);

/*
 * The chip also offers the option "Night Mode".
 * All LED current settings are divided by 16 for a 0 to 1.5mA current
 * setting range.
 */
static ssize_t nightmode_show(struct device *dev, struct device_attribute *a,
		char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned int value;

	mutex_lock(&chip->lock);
	regmap_field_read(chip->control_mode, &value);
	mutex_unlock(&chip->lock);

	if (value == CONTROL_MODE_NIGHT)
		return sprintf(buf, "%d\n", 1);
	else
		return sprintf(buf, "%d\n", 0);
}

static ssize_t nightmode_store(struct device *dev, struct device_attribute *a,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned long nightmode;
	int ret;

	if (kstrtoul(buf, 10, &nightmode))
		return -EINVAL;

	if (nightmode > 1)
		return -EINVAL;

	mutex_lock(&chip->lock);
	if (nightmode == 1)
		ret = regmap_field_write(chip->control_mode, CONTROL_MODE_NIGHT);
	else
		ret = regmap_field_write(chip->control_mode, CONTROL_MODE_NORMAL);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(nightmode);

/*
 * The chip also offers the option "Fade rate".
 */
static ssize_t faderate_show(struct device *dev, struct device_attribute *a,
		char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned int value;

	mutex_lock(&chip->lock);
	regmap_field_read(chip->faderate, &value);
	mutex_unlock(&chip->lock);

	return sprintf(buf, "%d\n", value);
}

static ssize_t faderate_store(struct device *dev, struct device_attribute *a,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ktd20xx *chip = i2c_get_clientdata(client);
	unsigned long faderate;

	if (kstrtoul(buf, 10, &faderate))
		return -EINVAL;

	if (faderate > 7)
		return -EINVAL;

	mutex_lock(&chip->lock);
	regmap_field_write(chip->faderate, faderate);
	mutex_unlock(&chip->lock);

	return count;
}
static DEVICE_ATTR_RW(faderate);

static struct attribute *ktd20xx_led_controller_attrs[] = {
	&dev_attr_current_color0.attr,
	&dev_attr_current_color1.attr,
	&dev_attr_nightmode.attr,
	&dev_attr_faderate.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ktd20xx_led_controller);

static int ktd20xx_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct ktd20xx *chip;
	unsigned int vendor;
	unsigned int chip_id;
	unsigned int chip_rev;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	mutex_init(&chip->lock);
	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &ktd20xx_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		goto error;
	}

	chip->control_mode = devm_regmap_field_alloc(chip->dev, chip->regmap,
			kt20xx_control_mode);
	chip->faderate = devm_regmap_field_alloc(chip->dev, chip->regmap,
			kt20xx_faderate);
	chip->vendor = devm_regmap_field_alloc(chip->dev, chip->regmap,
			kt20xx_vendor);
	chip->chip_id = devm_regmap_field_alloc(chip->dev, chip->regmap,
			kt20xx_chip_id);
	chip->chip_rev = devm_regmap_field_alloc(chip->dev, chip->regmap,
			kt20xx_chip_rev);

	regmap_field_write(chip->control_mode, CONTROL_MODE_RESET);
	ret = regmap_field_read(chip->vendor, &vendor);
	if (ret) {
		dev_err(&client->dev, "Failed to read vendor: %d\n", ret);
		goto error;
	}

	ret = regmap_field_read(chip->chip_id, &chip_id);
	if (ret) {
		dev_err(&client->dev, "Failed to read chip id: %d\n", ret);
		goto error;
	}

	ret = regmap_field_read(chip->chip_rev, &chip_rev);
	if (ret) {
		dev_err(&client->dev, "Failed to read chip rev: %d\n", ret);
		goto error;
	}

	dev_info(&client->dev, "vendor: 0x%02x chip-id: 0x%02x chip-rev: 0x%02x\n",
			vendor, chip_id, chip_rev);

	regmap_field_write(chip->control_mode, CONTROL_MODE_NORMAL);

	if (devm_device_add_groups(chip->dev, ktd20xx_led_controller_groups))
		dev_warn(&client->dev, "Could not add attribute group!\n");

	ret = ktd20xx_hwinit(chip);
	if (ret)
		goto error;

	ret = ktd20xx_probe_dt(chip);
	if (ret)
		goto error;

	return 0;

error:
	mutex_destroy(&chip->lock);
	return ret;
}

static int ktd20xx_remove(struct i2c_client *client)
{
	struct ktd20xx *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	regmap_field_write(chip->control_mode, CONTROL_MODE_OFF);
	mutex_unlock(&chip->lock);

	mutex_destroy(&chip->lock);

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
		.name	= "ktd20xx",
		.of_match_table = of_ktd20xx_leds_match,
	},
	.probe		= ktd20xx_probe,
	.remove		= ktd20xx_remove,
	.id_table	= ktd20xx_id
};
module_i2c_driver(ktd20xx_driver);

MODULE_DESCRIPTION("Kinetic KTD20xx LED driver");
MODULE_AUTHOR("Florian Eckert <fe@dev.tdt.de>");
MODULE_LICENSE("GPL v2");
