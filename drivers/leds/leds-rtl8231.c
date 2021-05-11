// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/device.h>
#include <linux/leds.h>
#include <linux/mfd/rtl8231.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct led_toggle_rate {
	u16 interval; /* Toggle interval in ms */
	u8 mode; /* Register value */
};

struct led_modes {
	/* Array of toggle rates, sorted by interval */
	const struct led_toggle_rate *toggle_rates;
	unsigned int num_toggle_rates;
	u8 off;
	u8 on;
};

struct rtl8231_led {
	struct led_classdev led;
	const struct led_modes *modes;
	struct regmap_field *reg_field;
};
#define to_rtl8231_led(_cdev) container_of(_cdev, struct rtl8231_led, led)

#define RTL8231_NUM_LEDS	3
#define RTL8231_LED_PER_REG	5
#define RTL8231_BITS_PER_LED	3

static const unsigned int rtl8231_led_port_count_single[RTL8231_NUM_LEDS] = {32, 32, 24};
static const unsigned int rtl8231_led_port_count_bicolor[RTL8231_NUM_LEDS] = {24, 24, 24};

static const unsigned int rtl8231_led_base[RTL8231_NUM_LEDS] = {
	RTL8231_REG_LED0_BASE,
	RTL8231_REG_LED1_BASE,
	RTL8231_REG_LED2_BASE,
};

static const struct led_toggle_rate rtl8231_toggle_rates[] = {
	{  40, 1},
	{  80, 2},
	{ 160, 3},
	{ 320, 4},
	{ 640, 5},
	{1280, 6},
};

static const struct led_modes rtl8231_led_modes = {
	.off = 0,
	.on = 7,
	.num_toggle_rates = ARRAY_SIZE(rtl8231_toggle_rates),
	.toggle_rates = rtl8231_toggle_rates,
};

static void rtl8231_led_brightness_set(struct led_classdev *led_cdev,
	enum led_brightness brightness)
{
	struct rtl8231_led *pled = to_rtl8231_led(led_cdev);

	if (brightness)
		regmap_field_write(pled->reg_field, pled->modes->on);
	else
		regmap_field_write(pled->reg_field, pled->modes->off);
}

static enum led_brightness rtl8231_led_brightness_get(struct led_classdev *led_cdev)
{
	struct rtl8231_led *pled = to_rtl8231_led(led_cdev);
	u32 current_mode = pled->modes->off;

	regmap_field_read(pled->reg_field, &current_mode);

	if (current_mode == pled->modes->off)
		return LED_OFF;
	else
		return LED_ON;
}

static unsigned int rtl8231_led_current_interval(struct rtl8231_led *pled)
{
	unsigned int mode;
	unsigned int i = 0;

	if (regmap_field_read(pled->reg_field, &mode))
		return 0;

	while (i < pled->modes->num_toggle_rates && mode != pled->modes->toggle_rates[i].mode)
		i++;

	if (i < pled->modes->num_toggle_rates)
		return pled->modes->toggle_rates[i].interval;
	else
		return 0;
}

static int rtl8231_led_blink_set(struct led_classdev *led_cdev, unsigned long *delay_on,
	unsigned long *delay_off)
{
	struct rtl8231_led *pled = to_rtl8231_led(led_cdev);
	const struct led_modes *modes = pled->modes;
	unsigned int interval;
	unsigned int i = 0;
	int err;

	if (*delay_on == 0 && *delay_off == 0) {
		/* Choose 500ms as default interval */
		interval = 500;
	} else {
		/*
		 * If the current mode is blinking, choose the delay that (likely) changed.
		 * Otherwise, choose the interval that would have the same total delay.
		 */
		interval = rtl8231_led_current_interval(pled);

		if (interval > 0 && interval == *delay_off)
			interval = *delay_on;
		else if (interval > 0 && interval == *delay_on)
			interval = *delay_off;
		else
			interval = (*delay_on + *delay_off) / 2;
	}

	/* Find clamped toggle interval */
	while (i < (modes->num_toggle_rates - 1) && interval > modes->toggle_rates[i].interval)
		i++;

	interval = modes->toggle_rates[i].interval;

	err = regmap_field_write(pled->reg_field, modes->toggle_rates[i].mode);
	if (err)
		return err;

	*delay_on = interval;
	*delay_off = interval;

	return 0;
}

static int rtl8231_led_read_address(struct device_node *np, unsigned int *addr_port,
	unsigned int *addr_led)
{
	const __be32 *addr;

	if (of_n_addr_cells(np) != 2 || of_n_size_cells(np) != 0)
		return -ENODEV;

	addr = of_get_address(np, 0, NULL, NULL);
	if (!addr)
		return -ENODEV;

	*addr_port = of_read_number(addr, 1);
	*addr_led = of_read_number(addr + 1, 1);

	return 0;
}

static struct reg_field rtl8231_led_get_field(unsigned int port_index, unsigned int led_index)
{
	unsigned int offset, shift;
	struct reg_field field;

	offset = port_index / RTL8231_LED_PER_REG;
	shift = (port_index % RTL8231_LED_PER_REG) * RTL8231_BITS_PER_LED;

	field.reg = rtl8231_led_base[led_index] + offset;
	field.lsb = shift;
	field.msb = shift + RTL8231_BITS_PER_LED - 1;

	return field;
}

static int rtl8231_led_probe_single(struct device *dev, struct regmap *map,
	const unsigned int *port_count, struct device_node *np)
{
	struct rtl8231_led *pled;
	unsigned int port_index;
	unsigned int led_index;
	struct reg_field field;
	struct led_init_data init_data = {};
	int err;

	pled = devm_kzalloc(dev, sizeof(*pled), GFP_KERNEL);
	if (IS_ERR(pled))
		return PTR_ERR(pled);

	err = rtl8231_led_read_address(np, &port_index, &led_index);

	if (err) {
		dev_err(dev, "LED address invalid\n");
		return err;
	} else if (led_index >= RTL8231_NUM_LEDS || port_index >= port_count[led_index]) {
		dev_err(dev, "LED address (%d.%d) invalid\n", port_index, led_index);
		return -ENODEV;
	}

	field = rtl8231_led_get_field(port_index, led_index);
	pled->reg_field = devm_regmap_field_alloc(dev, map, field);
	if (IS_ERR(pled->reg_field))
		return PTR_ERR(pled->reg_field);

	pled->modes = &rtl8231_led_modes;

	pled->led.max_brightness = 1;
	pled->led.brightness_get = rtl8231_led_brightness_get;
	pled->led.brightness_set = rtl8231_led_brightness_set;
	pled->led.blink_set = rtl8231_led_blink_set;

	init_data.fwnode = of_fwnode_handle(np);

	return devm_led_classdev_register_ext(dev, &pled->led, &init_data);
}

static int rtl8231_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const unsigned int *port_count;
	struct device_node *child;
	struct regmap *map;
	int err;

	map = dev_get_regmap(dev->parent, NULL);
	if (IS_ERR_OR_NULL(map)) {
		dev_err(dev, "failed to retrieve regmap\n");
		if (!map)
			return -ENODEV;
		else
			return PTR_ERR(map);
	}

	if (!device_property_match_string(dev, "realtek,led-scan-mode", "single-color")) {
		port_count = rtl8231_led_port_count_single;
		regmap_update_bits(map, RTL8231_REG_FUNC0,
			RTL8231_FUNC0_SCAN_MODE, RTL8231_FUNC0_SCAN_SINGLE);
	} else if (!device_property_match_string(dev, "realtek,led-scan-mode", "bi-color")) {
		port_count = rtl8231_led_port_count_bicolor;
		regmap_update_bits(map, RTL8231_REG_FUNC0,
			RTL8231_FUNC0_SCAN_MODE, RTL8231_FUNC0_SCAN_BICOLOR);
	} else {
		dev_err(dev, "scan mode missing or invalid\n");
		return -EINVAL;
	}

	for_each_child_of_node(dev->of_node, child) {
		if (of_node_name_prefix(child, "led")) {
			err = rtl8231_led_probe_single(dev, map, port_count, child);
			if (err)
				dev_warn(dev, "failed to register %pOF\n", child);
			continue;
		}

		dev_dbg(dev, "skipping unsupported node %pOF\n", child);
	}

	return 0;
}

static const struct of_device_id of_rtl8231_led_match[] = {
	{ .compatible = "realtek,rtl8231-leds" },
	{}
};
MODULE_DEVICE_TABLE(of, of_rtl8231_led_match);

static struct platform_driver rtl8231_led_driver = {
	.driver = {
		.name = "rtl8231-leds",
		.of_match_table = of_rtl8231_led_match,
	},
	.probe = rtl8231_led_probe,
};
module_platform_driver(rtl8231_led_driver);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Realtek RTL8231 LED support");
MODULE_LICENSE("GPL v2");
