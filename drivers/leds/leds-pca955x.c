// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2007-2008 Extreme Engineering Solutions, Inc.
 *
 * Author: Nate Case <ncase@xes-inc.com>
 *
 * LED driver for various PCA955x I2C LED drivers
 *
 * Supported devices:
 *
 *	Device		Description		7-bit slave address
 *	------		-----------		-------------------
 *	PCA9550		2-bit driver		0x60 .. 0x61
 *	PCA9551		8-bit driver		0x60 .. 0x67
 *	PCA9552		16-bit driver		0x60 .. 0x67
 *	PCA9553/01	4-bit driver		0x62
 *	PCA9553/02	4-bit driver		0x63
 *
 * Philips PCA955x LED driver chips follow a register map as shown below:
 *
 *	Control Register		Description
 *	----------------		-----------
 *	0x0				Input register 0
 *					..
 *	NUM_INPUT_REGS - 1		Last Input register X
 *
 *	NUM_INPUT_REGS			Frequency prescaler 0
 *	NUM_INPUT_REGS + 1		PWM register 0
 *	NUM_INPUT_REGS + 2		Frequency prescaler 1
 *	NUM_INPUT_REGS + 3		PWM register 1
 *
 *	NUM_INPUT_REGS + 4		LED selector 0
 *	NUM_INPUT_REGS + 4
 *	    + NUM_LED_REGS - 1		Last LED selector
 *
 *  where NUM_INPUT_REGS and NUM_LED_REGS vary depending on how many
 *  bits the chip supports.
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <dt-bindings/leds/leds-pca955x.h>

/* LED select registers determine the source that drives LED outputs */
#define PCA955X_LS_LED_ON	0x0	/* Output LOW */
#define PCA955X_LS_LED_OFF	0x1	/* Output HI-Z */
#define PCA955X_LS_BLINK0	0x2	/* Blink at PWM0 rate */
#define PCA955X_LS_BLINK1	0x3	/* Blink at PWM1 rate */

#define PCA955X_GPIO_INPUT	LED_OFF
#define PCA955X_GPIO_HIGH	LED_OFF
#define PCA955X_GPIO_LOW	LED_FULL

enum pca955x_type {
	pca9550,
	pca9551,
	pca9552,
	ibm_pca9552,
	pca9553,
};

struct pca955x_chipdef {
	int			bits;
	u8			slv_addr;	/* 7-bit slave address mask */
	int			slv_addr_shift;	/* Number of bits to ignore */
	struct pinctrl_desc	*pinctrl;
};

struct pca955x {
	struct mutex lock;
	struct pca955x_led *leds;
	struct pca955x_chipdef	*chipdef;
	struct i2c_client	*client;
	struct pinctrl_desc	*pctldesc;
	struct pinctrl_dev	*pctldev;
#ifdef CONFIG_LEDS_PCA955X_GPIO
	struct gpio_chip gpio;
#endif
};

struct pca955x_led {
	struct pca955x	*pca955x;
	struct led_classdev	led_cdev;
	int			led_num;	/* 0 .. 15 potentially */
	char			name[32];
	u32			type;
	const char		*default_trigger;
};

struct pca955x_platform_data {
	struct pca955x_led	*leds;
	int			num_leds;
};

/* 8 bits per input register */
static inline int pca95xx_num_input_regs(int bits)
{
	return (bits + 7) / 8;
}

/* 4 bits per LED selector register */
static inline int pca95xx_num_led_regs(int bits)
{
	return (bits + 3)  / 4;
}

/*
 * Return an LED selector register value based on an existing one, with
 * the appropriate 2-bit state value set for the given LED number (0-3).
 */
static inline u8 pca955x_ledsel(u8 oldval, int led_num, int state)
{
	return (oldval & (~(0x3 << (led_num << 1)))) |
		((state & 0x3) << (led_num << 1));
}

/*
 * Write to frequency prescaler register, used to program the
 * period of the PWM output.  period = (PSCx + 1) / 38
 */
static int pca955x_write_psc(struct i2c_client *client, int n, u8 val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_write_byte_data(client,
		pca95xx_num_input_regs(pca955x->chipdef->bits) + 2*n,
		val);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, n, val, ret);
	return ret;
}

/*
 * Write to PWM register, which determines the duty cycle of the
 * output.  LED is OFF when the count is less than the value of this
 * register, and ON when it is greater.  If PWMx == 0, LED is always OFF.
 *
 * Duty cycle is (256 - PWMx) / 256
 */
static int pca955x_write_pwm(struct i2c_client *client, int n, u8 val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_write_byte_data(client,
		pca95xx_num_input_regs(pca955x->chipdef->bits) + 1 + 2*n,
		val);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, n, val, ret);
	return ret;
}

/*
 * Write to LED selector register, which determines the source that
 * drives the LED output.
 */
static int pca955x_write_ls(struct i2c_client *client, int n, u8 val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_write_byte_data(client,
		pca95xx_num_input_regs(pca955x->chipdef->bits) + 4 + n,
		val);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, n, val, ret);
	return ret;
}

/*
 * Read the LED selector register, which determines the source that
 * drives the LED output.
 */
static int pca955x_read_ls(struct i2c_client *client, int n, u8 *val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_read_byte_data(client,
		pca95xx_num_input_regs(pca955x->chipdef->bits) + 4 + n);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n",
			__func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int pca955x_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct pca955x_led *pca955x_led;
	struct pca955x *pca955x;
	u8 ls;
	int chip_ls;	/* which LSx to use (0-3 potentially) */
	int ls_led;	/* which set of bits within LSx to use (0-3) */
	int ret;

	pca955x_led = container_of(led_cdev, struct pca955x_led, led_cdev);
	pca955x = pca955x_led->pca955x;

	chip_ls = pca955x_led->led_num / 4;
	ls_led = pca955x_led->led_num % 4;

	mutex_lock(&pca955x->lock);

	ret = pca955x_read_ls(pca955x->client, chip_ls, &ls);
	if (ret)
		goto out;

	switch (value) {
	case LED_FULL:
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_LED_ON);
		break;
	case LED_OFF:
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_LED_OFF);
		break;
	case LED_HALF:
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_BLINK0);
		break;
	default:
		/*
		 * Use PWM1 for all other values.  This has the unwanted
		 * side effect of making all LEDs on the chip share the
		 * same brightness level if set to a value other than
		 * OFF, HALF, or FULL.  But, this is probably better than
		 * just turning off for all other values.
		 */
		ret = pca955x_write_pwm(pca955x->client, 1, 255 - value);
		if (ret)
			goto out;
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_BLINK1);
		break;
	}

	ret = pca955x_write_ls(pca955x->client, chip_ls, ls);

out:
	mutex_unlock(&pca955x->lock);

	return ret;
}

static int
pca955x_set_pin_value(struct pca955x *pca955x, unsigned int pin, int val)
{
	struct led_classdev *cdev = &pca955x->leds[pin].led_cdev;
	int state = val ? PCA955X_GPIO_HIGH : PCA955X_GPIO_LOW;

	return pca955x_led_set(cdev, state);
}

#ifdef CONFIG_LEDS_PCA955X_GPIO
/*
 * Read the INPUT register, which contains the state of LEDs.
 */
static int pca955x_read_input(struct i2c_client *client, int n, u8 *val)
{
	int ret = i2c_smbus_read_byte_data(client, n);

	if (ret < 0) {
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n",
			__func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;

}

static void
pca955x_gpio_set_value(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	int pin;

	pin = pinctrl_gpio_as_pin(pca955x->pctldev, gc->base + offset);
	if (pin < 0) {
		dev_err(gc->parent, "Failed to look up pin for GPIO %d\n",
			offset);
		return;
	}

	pca955x_set_pin_value(pca955x, pin, val);
}

static int pca955x_gpio_get_value(struct gpio_chip *gc, unsigned int offset)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	u8 reg = 0;
	int pin;

	pin = pinctrl_gpio_as_pin(pca955x->pctldev, gc->base + offset);
	if (pin < 0)
		return pin;

	/* There is nothing we can do about errors */
	pca955x_read_input(pca955x->client, pin / 8, &reg);

	return !!(reg & (1 << (pin % 8)));
}

static int
pca955x_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	struct led_classdev *cdev;
	int pin;

	pin = pinctrl_gpio_as_pin(pca955x->pctldev, gc->base + offset);
	if (pin < 0)
		return pin;

	cdev = &pca955x->leds[pin].led_cdev;

	return pca955x_led_set(cdev, PCA955X_GPIO_INPUT);
}

static int
pca955x_gpio_direction_output(struct gpio_chip *gc, unsigned int offset,
			      int val)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	int pin;

	pin = pinctrl_gpio_as_pin(pca955x->pctldev, gc->base + offset);
	if (pin < 0)
		return pin;

	return pca955x_set_pin_value(pca955x, pin, val);
}

static int
pca955x_gpio_request_pin(struct gpio_chip *gc, unsigned int offset)
{
	return pinctrl_gpio_request(gc->base + offset);
}

static void
pca955x_gpio_free_pin(struct gpio_chip *gc, unsigned int offset)
{
	int rc;

	/* Go high-impedance */
	rc = pca955x_gpio_direction_input(gc, offset);
	if (rc < 0)
		dev_err(gc->parent, "Failed to set direction for GPIO %u:%u\n", gc->base, offset);

	pinctrl_gpio_free(gc->base + offset);
}
#endif /* CONFIG_LEDS_PCA955X_GPIO */

static const struct pinctrl_pin_desc pca9552_pinctrl_pins[] = {
	PINCTRL_PIN(0, "LED0"),
	PINCTRL_PIN(1, "LED1"),
	PINCTRL_PIN(2, "LED2"),
	PINCTRL_PIN(3, "LED3"),
	PINCTRL_PIN(4, "LED4"),
	PINCTRL_PIN(5, "LED5"),
	PINCTRL_PIN(6, "LED6"),
	PINCTRL_PIN(7, "LED7"),
	PINCTRL_PIN(8, "LED8"),
	PINCTRL_PIN(9, "LED9"),
	PINCTRL_PIN(10, "LED10"),
	PINCTRL_PIN(11, "LED11"),
	PINCTRL_PIN(12, "LED12"),
	PINCTRL_PIN(13, "LED13"),
	PINCTRL_PIN(14, "LED14"),
	PINCTRL_PIN(15, "LED15"),
};

static const char * const pca9552_groups[] = {
	"LED0", "LED1", "LED2",  "LED3",  "LED4",  "LED5",  "LED6",  "LED7",
	"LED8", "LED9", "LED10", "LED11", "LED12", "LED13", "LED14", "LED15",
};

static const unsigned int pca9552_group_pins[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

static const char *pca955x_pinctrl_dev_name(struct pca955x *pca955x)
{
	/* The controller is its only consumer via leds and gpios */
	return dev_name(&pca955x->client->dev);
}

static int pca955x_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);

	/* We have as many groups as we have LEDs */
	return pca955x->chipdef->bits;
}

static const char *
pca955x_pinctrl_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);

	if (unlikely(selector > pca955x->chipdef->bits)) {
		dev_err(&pca955x->client->dev,
			"Group selector (%u) exceeds groups count (%u)\n",
			selector, pca955x->chipdef->bits);
		return NULL;
	}

	if (unlikely(selector > ARRAY_SIZE(pca9552_groups))) {
		dev_err(&pca955x->client->dev,
			"Group selector (%u) exceeds the supported group count (%u)\n",
			selector, ARRAY_SIZE(pca9552_groups));
		return NULL;
	}

	return pca9552_groups[selector];
}

static int
pca955x_pinctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int  selector,
			       const unsigned int **pins, unsigned int *num_pins)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);

	if (unlikely(selector > pca955x->chipdef->bits)) {
		dev_err(&pca955x->client->dev,
			"Group selector (%u) exceeds groups count (%u)\n",
			selector, pca955x->chipdef->bits);
		return -EINVAL;
	}

	if (unlikely(selector > ARRAY_SIZE(pca9552_group_pins))) {
		dev_err(&pca955x->client->dev,
			"Group selector (%u) exceeds the supported group count (%u)\n",
			selector, ARRAY_SIZE(pca9552_groups));
		return -EINVAL;
	}

	*pins = &pca9552_group_pins[selector];
	*num_pins = 1;

	return 0;
}

static int pca955x_pinmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	return 1;
}

static const char *
pca955x_pinmux_get_function_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);

	if (selector != 0)
		dev_err(&pca955x->client->dev, "Only the 'LED' function is supported");

	return "LED";
}

static int pca955x_pinmux_get_function_groups(struct pinctrl_dev *pctldev,
					      unsigned int selector,
					      const char * const **groups,
					      unsigned int *num_groups)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);

	if (unlikely(pca955x->chipdef->bits > ARRAY_SIZE(pca9552_groups))) {
		dev_warn(&pca955x->client->dev,
			 "Unsupported PCA955x configuration, LED count exceed LED group count\n");
		return -EINVAL;
	}

	if (selector != 0)
		dev_err(&pca955x->client->dev, "Only the 'LED' function is supported");

	*groups = &pca9552_groups[0];
	*num_groups = pca955x->chipdef->bits;

	return 0;
}

static int
pca955x_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
		       unsigned int group_selector)
{
	/* There's no actual mux as such. */
	return 0;
}

/*
 * Implement pinctrl map parsing in a way that's backwards compatible with the
 * existing devicetree binding.
 */
static int
pca955x_dt_dev_to_map(struct pinctrl_dev *pctldev, struct device *dev)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_desc *pctldesc = pca955x->pctldesc;
	struct fwnode_handle *child;
	struct pinctrl_map *maps;
	unsigned int i = 0;
	int rc;

	if (WARN_ON(dev != &pca955x->client->dev))
		return -EINVAL;

	/* Only 1 possible mux config per LED, no further allocations needed */
	maps = devm_kmalloc_array(dev, pca955x->chipdef->bits, sizeof(*maps), GFP_KERNEL);
	if (!maps)
		return -ENOMEM;

	device_for_each_child_node(dev, child) {
		struct pinctrl_map *m;
		u32 type;
		u32 reg;

		/* Default to PCA955X_TYPE_LED as we do in pca955x_get_pdata */
		rc = fwnode_property_read_u32(child, "type", &type);
		if (rc == -EINVAL)
			type = PCA955X_TYPE_LED;
		else if (rc < 0)
			goto cleanup_maps;

		if (type != PCA955X_TYPE_LED)
			continue;

		rc = fwnode_property_read_u32(child, "reg", &reg);
		if (rc < 0)
			goto cleanup_maps;

		if (i >= pca955x->chipdef->bits) {
			dev_err(dev,
				"The number of pin configuration nodes exceeds the number of available pins (%u)\n",
				pca955x->chipdef->bits);
			break;
		}

		m = &maps[i];

		m->dev_name = pctldesc->name;
		m->name = PINCTRL_STATE_DEFAULT;
		m->type = PIN_MAP_TYPE_MUX_GROUP;
		m->ctrl_dev_name = pctldesc->name;
		m->data.mux.function = "LED";
		m->data.mux.group = devm_kasprintf(dev, GFP_KERNEL, "LED%d", reg);
		if (!m->data.mux.group) {
			rc = -ENOMEM;
			goto cleanup_maps;
		}

		i++;
	}

	/* Trim the map allocation as required */
	if (i < pca955x->chipdef->bits) {
		struct pinctrl_map *trimmed;

		trimmed = devm_krealloc(dev, maps, i * sizeof(*maps), GFP_KERNEL);
		if (trimmed)
			maps = trimmed;
		else
			dev_warn(dev, "Failed to trim pinctrl maps\n");
	}

	pinctrl_register_mappings(maps, i);

	return 0;

cleanup_maps:
	while (i--)
		devm_kfree(dev, maps[i].data.mux.group);

	devm_kfree(dev, maps);

	return rc;
}

static void
pca955x_dt_free_map(struct pinctrl_dev *pctldev, struct pinctrl_map *map,
		    unsigned int num_maps)
{
	struct pca955x *pca955x = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = &pca955x->client->dev;
	struct pinctrl_map *iter = map;

	if (!iter)
		return;

	while (num_maps) {
		devm_kfree(dev, iter->data.mux.group);
		iter++;
		num_maps--;
	}

	devm_kfree(dev, map);
}

static const struct pinctrl_ops pca955x_pinctrl_ops = {
	.get_groups_count	= pca955x_pinctrl_get_groups_count,
	.get_group_name		= pca955x_pinctrl_get_group_name,
	.get_group_pins		= pca955x_pinctrl_get_group_pins,
	.dt_dev_to_map		= pca955x_dt_dev_to_map,
	.dt_free_map		= pca955x_dt_free_map,
};

static const struct pinmux_ops pca955x_pinmux_ops = {
	.get_functions_count	= pca955x_pinmux_get_functions_count,
	.get_function_name	= pca955x_pinmux_get_function_name,
	.get_function_groups	= pca955x_pinmux_get_function_groups,
	.set_mux		= pca955x_pinmux_set_mux,
	.strict = true,
};

static struct pca955x_platform_data *
pca955x_get_pdata(struct i2c_client *client, struct pca955x_chipdef *chip)
{
	struct pca955x_platform_data *pdata;
	struct fwnode_handle *child;
	int count;

	count = device_get_child_node_count(&client->dev);
	if (!count || count > chip->bits)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds = devm_kcalloc(&client->dev,
				   chip->bits, sizeof(struct pca955x_led),
				   GFP_KERNEL);
	if (!pdata->leds)
		return ERR_PTR(-ENOMEM);

	device_for_each_child_node(&client->dev, child) {
		const char *name;
		u32 reg;
		int res;

		res = fwnode_property_read_u32(child, "reg", &reg);
		if ((res != 0) || (reg >= chip->bits))
			continue;

		res = fwnode_property_read_string(child, "label", &name);
		if ((res != 0) && is_of_node(child))
			name = to_of_node(child)->name;

		snprintf(pdata->leds[reg].name, sizeof(pdata->leds[reg].name),
			 "%s", name);

		pdata->leds[reg].type = PCA955X_TYPE_LED;
		fwnode_property_read_u32(child, "type", &pdata->leds[reg].type);
		fwnode_property_read_string(child, "linux,default-trigger",
					&pdata->leds[reg].default_trigger);
	}

	pdata->num_leds = chip->bits;

	return pdata;
}

static struct pca955x_chipdef pca955x_chipdefs[] = {
	[pca9550] = {
		.bits		= 2,
		.slv_addr	= /* 110000x */ 0x60,
		.slv_addr_shift	= 1,
	},
	[pca9551] = {
		.bits		= 8,
		.slv_addr	= /* 1100xxx */ 0x60,
		.slv_addr_shift	= 3,
	},
	[pca9552] = {
		.bits		= 16,
		.slv_addr	= /* 1100xxx */ 0x60,
		.slv_addr_shift	= 3,
	},
	[ibm_pca9552] = {
		.bits		= 16,
		.slv_addr	= /* 0110xxx */ 0x30,
		.slv_addr_shift	= 3,
	},
	[pca9553] = {
		.bits		= 4,
		.slv_addr	= /* 110001x */ 0x62,
		.slv_addr_shift	= 1,
	},
};

static const struct i2c_device_id pca955x_id[] = {
	{ "pca9550", pca9550 },
	{ "pca9551", pca9551 },
	{ "pca9552", pca9552 },
	{ "ibm-pca9552", ibm_pca9552 },
	{ "pca9553", pca9553 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca955x_id);

static const struct of_device_id of_pca955x_match[] = {
	{ .compatible = "nxp,pca9550", .data = (void *)pca9550 },
	{ .compatible = "nxp,pca9551", .data = (void *)pca9551 },
	{ .compatible = "nxp,pca9552", .data = (void *)pca9552 },
	{ .compatible = "ibm,pca9552", .data = (void *)ibm_pca9552 },
	{ .compatible = "nxp,pca9553", .data = (void *)pca9553 },
	{},
};
MODULE_DEVICE_TABLE(of, of_pca955x_match);

static int pca955x_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct pca955x *pca955x;
	struct pca955x_led *pca955x_led;
	struct pca955x_chipdef *chip;
	struct i2c_adapter *adapter;
	int i, err;
	struct pca955x_platform_data *pdata;
	u32 ngpios = 0;
	struct fwnode_handle *fwnode;

	fwnode = dev_fwnode(&client->dev);
	if (!fwnode)
		return -ENODATA;

	chip = &pca955x_chipdefs[id->driver_data];
	adapter = client->adapter;
	pdata = dev_get_platdata(&client->dev);
	if (!pdata) {
		pdata =	pca955x_get_pdata(client, chip);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	/* Make sure the slave address / chip type combo given is possible */
	if ((client->addr & ~((1 << chip->slv_addr_shift) - 1)) !=
	    chip->slv_addr) {
		dev_err(&client->dev, "invalid slave address %02x\n",
				client->addr);
		return -ENODEV;
	}

	dev_info(&client->dev, "leds-pca955x: Using %s %d-bit LED driver at "
			"slave address 0x%02x\n",
			client->name, chip->bits, client->addr);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if (pdata->num_leds != chip->bits) {
		dev_err(&client->dev,
			"board info claims %d LEDs on a %d-bit chip\n",
			pdata->num_leds, chip->bits);
		return -ENODEV;
	}

	pca955x = devm_kzalloc(&client->dev, sizeof(*pca955x), GFP_KERNEL);
	if (!pca955x)
		return -ENOMEM;

	pca955x->leds = devm_kcalloc(&client->dev,
			chip->bits, sizeof(*pca955x_led), GFP_KERNEL);
	if (!pca955x->leds)
		return -ENOMEM;

	pca955x->pctldesc = devm_kzalloc(&client->dev,
			sizeof(*pca955x->pctldesc), GFP_KERNEL);
	if (!pca955x->pctldesc)
		return -ENOMEM;

	i2c_set_clientdata(client, pca955x);

	mutex_init(&pca955x->lock);
	pca955x->client = client;
	pca955x->chipdef = chip;

	/* pinctrl */
	pca955x->pctldesc->name = pca955x_pinctrl_dev_name(pca955x);
	if (!pca955x->pctldesc->name)
		return -ENOMEM;

	pca955x->pctldesc->pins = &pca9552_pinctrl_pins[0];
	pca955x->pctldesc->npins = chip->bits;
	pca955x->pctldesc->pctlops = &pca955x_pinctrl_ops;
	pca955x->pctldesc->pmxops = &pca955x_pinmux_ops;
	pca955x->pctldesc->owner = THIS_MODULE;

	err = devm_pinctrl_register_and_init(&client->dev, pca955x->pctldesc,
					     pca955x, &pca955x->pctldev);
	if (err) {
		dev_err(&client->dev, "Failed to register pincontroller: %d\n", err);
		return err;
	}

	for (i = 0; i < chip->bits; i++) {
		pca955x_led = &pca955x->leds[i];
		pca955x_led->led_num = i;
		pca955x_led->pca955x = pca955x;
		pca955x_led->type = pdata->leds[i].type;

		switch (pca955x_led->type) {
		case PCA955X_TYPE_NONE:
			break;
		case PCA955X_TYPE_GPIO:
			ngpios++;
			break;
		case PCA955X_TYPE_LED:
			/*
			 * Platform data can specify LED names and
			 * default triggers
			 */
			if (pdata->leds[i].name[0] == '\0')
				snprintf(pdata->leds[i].name,
					sizeof(pdata->leds[i].name), "%d", i);

			snprintf(pca955x_led->name,
				sizeof(pca955x_led->name), "pca955x:%s",
				pdata->leds[i].name);

			if (pdata->leds[i].default_trigger)
				pca955x_led->led_cdev.default_trigger =
					pdata->leds[i].default_trigger;

			pca955x_led->led_cdev.name = pca955x_led->name;
			pca955x_led->led_cdev.brightness_set_blocking =
				pca955x_led_set;

			err = devm_led_classdev_register(&client->dev,
							&pca955x_led->led_cdev);
			if (err)
				return err;

			/* Turn off LED */
			err = pca955x_led_set(&pca955x_led->led_cdev, LED_OFF);
			if (err)
				return err;
		}
	}

	err = pinctrl_enable(pca955x->pctldev);
	if (err) {
		dev_err(&client->dev, "Failed to enable pincontroller: %d\n", err);
		return err;
	}

	/* PWM0 is used for half brightness or 50% duty cycle */
	err = pca955x_write_pwm(client, 0, 255 - LED_HALF);
	if (err)
		return err;

	/* PWM1 is used for variable brightness, default to OFF */
	err = pca955x_write_pwm(client, 1, 0);
	if (err)
		return err;

	/* Set to fast frequency so we do not see flashing */
	err = pca955x_write_psc(client, 0, 0);
	if (err)
		return err;
	err = pca955x_write_psc(client, 1, 0);
	if (err)
		return err;

#ifdef CONFIG_LEDS_PCA955X_GPIO
	/* Always register the gpiochip, no-longer conditional on ngpios */
	pca955x->gpio.label = "gpio-pca955x";
	pca955x->gpio.direction_input = pca955x_gpio_direction_input;
	pca955x->gpio.direction_output = pca955x_gpio_direction_output;
	pca955x->gpio.set = pca955x_gpio_set_value;
	pca955x->gpio.get = pca955x_gpio_get_value;
	pca955x->gpio.request = pca955x_gpio_request_pin;
	pca955x->gpio.free = pca955x_gpio_free_pin;
	pca955x->gpio.can_sleep = 1;
	pca955x->gpio.base = -1;
	pca955x->gpio.parent = &client->dev;
	pca955x->gpio.owner = THIS_MODULE;

	if (!ngpios) {
		err = fwnode_property_read_u32(fwnode, "ngpios", &ngpios);
		if (err < 0 && err != -EINVAL)
			return err;
	}

	if (!ngpios)
		ngpios = chip->bits;


	pca955x->gpio.ngpio = ngpios;

	err = devm_gpiochip_add_data(&client->dev, &pca955x->gpio, pca955x);
	if (err) {
		/* Use data->gpio.dev as a flag for freeing gpiochip */
		pca955x->gpio.parent = NULL;
		dev_warn(&client->dev, "could not add gpiochip\n");
		return err;
	}

	if (!device_property_present(&client->dev, "gpio-ranges")) {
		struct fwnode_handle *child;
		unsigned int i = 0;

		device_for_each_child_node(&client->dev, child) {
			u32 type;
			u32 reg;

			err = fwnode_property_read_u32(child, "reg", &reg);
			if (err < 0)
				return err;

			err = fwnode_property_read_u32(child, "type", &type);
			if (err < 0)
				continue;

			/* XXX: Do something less wasteful? */
			err = gpiochip_add_pin_range(&pca955x->gpio,
					pca955x_pinctrl_dev_name(pca955x),
					i, reg, 1);
			if (err)
				return err;

			i++;
		}
	}
#endif

	return 0;
}

static struct i2c_driver pca955x_driver = {
	.driver = {
		.name	= "leds-pca955x",
		.of_match_table = of_pca955x_match,
	},
	.probe	= pca955x_probe,
	.id_table = pca955x_id,
};

module_i2c_driver(pca955x_driver);

MODULE_AUTHOR("Nate Case <ncase@xes-inc.com>");
MODULE_DESCRIPTION("PCA955x LED driver");
MODULE_LICENSE("GPL v2");
