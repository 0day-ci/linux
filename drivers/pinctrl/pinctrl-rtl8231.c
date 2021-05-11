// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/rtl8231.h>
#include <linux/module.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define RTL8231_NUM_GPIOS	37

enum rtl8231_gpio_regfield {
	RTL8231_FIELD_GPIO_DIR0,
	RTL8231_FIELD_GPIO_DIR1,
	RTL8231_FIELD_GPIO_DIR2,
	RTL8231_FIELD_GPIO_DATA0,
	RTL8231_FIELD_GPIO_DATA1,
	RTL8231_FIELD_GPIO_DATA2,
	RTL8231_FIELD_GPIO_MAX
};

static struct reg_field rtl8231_gpio_fields[RTL8231_FIELD_GPIO_MAX] = {
	[RTL8231_FIELD_GPIO_DIR0] = REG_FIELD(RTL8231_REG_GPIO_DIR0, 0, 15),
	[RTL8231_FIELD_GPIO_DIR1] = REG_FIELD(RTL8231_REG_GPIO_DIR1, 0, 15),
	[RTL8231_FIELD_GPIO_DIR2] = REG_FIELD(RTL8231_REG_PIN_HI_CFG, 5, 9),
	[RTL8231_FIELD_GPIO_DATA0] = REG_FIELD(RTL8231_REG_GPIO_DATA0, 0, 15),
	[RTL8231_FIELD_GPIO_DATA1] = REG_FIELD(RTL8231_REG_GPIO_DATA1, 0, 15),
	[RTL8231_FIELD_GPIO_DATA2] = REG_FIELD(RTL8231_REG_GPIO_DATA2, 0, 4),
};

struct rtl8231_function {
	const char *name;
	unsigned int ngroups;
	const char **groups;
};

struct rtl8231_pin_ctrl {
	/* Pin controller */
	struct pinctrl_desc pctl_desc;
	unsigned int nfunctions;
	struct rtl8231_function *functions;
	struct regmap *map;
	/* GPIO controller */
	struct gpio_chip gc;
	struct regmap_field *fields[RTL8231_FIELD_GPIO_MAX];
};

/*
 * Pin controller functionality
 */
static const char * const rtl8231_pin_function_names[] = {
	"gpio",
	"led",
	"pwm",
};

enum rtl8231_pin_function {
	RTL8231_PIN_FUNCTION_GPIO = BIT(0),
	RTL8231_PIN_FUNCTION_LED = BIT(1),
	RTL8231_PIN_FUNCTION_PWM = BIT(2),
};

struct rtl8231_pin_desc {
	unsigned int number;
	const char *name;
	enum rtl8231_pin_function functions;
	u8 reg;
	u8 offset;
	u8 gpio_function_value;
};

#define RTL8231_PIN(_num, _func, _reg, _fld, _val)		\
	{							\
		.number = _num,					\
		.name = "gpio" #_num,				\
		.functions = RTL8231_PIN_FUNCTION_GPIO | _func,	\
		.reg = _reg,					\
		.offset = _fld,					\
		.gpio_function_value = _val,			\
	}
#define RTL8231_GPIO_PIN(_num)					\
	RTL8231_PIN(_num, 0, 0, 0, 0)
#define RTL8231_LED_PIN(_num, _reg, _fld)			\
	RTL8231_PIN(_num, RTL8231_PIN_FUNCTION_LED, _reg, _fld, RTL8231_PIN_MODE_GPIO)
#define RTL8231_PWM_PIN(_num, _reg, _fld)			\
	RTL8231_PIN(_num, RTL8231_PIN_FUNCTION_PWM, _reg, _fld, 0)

/* Pins always support GPIO, and may support one alternate function */
static const struct rtl8231_pin_desc rtl8231_pins[RTL8231_NUM_GPIOS] = {
	RTL8231_LED_PIN(0, RTL8231_REG_PIN_MODE0, 0),
	RTL8231_LED_PIN(1, RTL8231_REG_PIN_MODE0, 1),
	RTL8231_LED_PIN(2, RTL8231_REG_PIN_MODE0, 2),
	RTL8231_LED_PIN(3, RTL8231_REG_PIN_MODE0, 3),
	RTL8231_LED_PIN(4, RTL8231_REG_PIN_MODE0, 4),
	RTL8231_LED_PIN(5, RTL8231_REG_PIN_MODE0, 5),
	RTL8231_LED_PIN(6, RTL8231_REG_PIN_MODE0, 6),
	RTL8231_LED_PIN(7, RTL8231_REG_PIN_MODE0, 7),
	RTL8231_LED_PIN(8, RTL8231_REG_PIN_MODE0, 8),
	RTL8231_LED_PIN(9, RTL8231_REG_PIN_MODE0, 9),
	RTL8231_LED_PIN(10, RTL8231_REG_PIN_MODE0, 10),
	RTL8231_LED_PIN(11, RTL8231_REG_PIN_MODE0, 11),
	RTL8231_LED_PIN(12, RTL8231_REG_PIN_MODE0, 12),
	RTL8231_LED_PIN(13, RTL8231_REG_PIN_MODE0, 13),
	RTL8231_LED_PIN(14, RTL8231_REG_PIN_MODE0, 14),
	RTL8231_LED_PIN(15, RTL8231_REG_PIN_MODE0, 15),
	RTL8231_LED_PIN(16, RTL8231_REG_PIN_MODE1, 0),
	RTL8231_LED_PIN(17, RTL8231_REG_PIN_MODE1, 1),
	RTL8231_LED_PIN(18, RTL8231_REG_PIN_MODE1, 2),
	RTL8231_LED_PIN(19, RTL8231_REG_PIN_MODE1, 3),
	RTL8231_LED_PIN(20, RTL8231_REG_PIN_MODE1, 4),
	RTL8231_LED_PIN(21, RTL8231_REG_PIN_MODE1, 5),
	RTL8231_LED_PIN(22, RTL8231_REG_PIN_MODE1, 6),
	RTL8231_LED_PIN(23, RTL8231_REG_PIN_MODE1, 7),
	RTL8231_LED_PIN(24, RTL8231_REG_PIN_MODE1, 8),
	RTL8231_LED_PIN(25, RTL8231_REG_PIN_MODE1, 9),
	RTL8231_LED_PIN(26, RTL8231_REG_PIN_MODE1, 10),
	RTL8231_LED_PIN(27, RTL8231_REG_PIN_MODE1, 11),
	RTL8231_LED_PIN(28, RTL8231_REG_PIN_MODE1, 12),
	RTL8231_LED_PIN(29, RTL8231_REG_PIN_MODE1, 13),
	RTL8231_LED_PIN(30, RTL8231_REG_PIN_MODE1, 14),
	RTL8231_LED_PIN(31, RTL8231_REG_PIN_MODE1, 15),
	RTL8231_LED_PIN(32, RTL8231_REG_PIN_HI_CFG, 0),
	RTL8231_LED_PIN(33, RTL8231_REG_PIN_HI_CFG, 1),
	RTL8231_LED_PIN(34, RTL8231_REG_PIN_HI_CFG, 2),
	RTL8231_PWM_PIN(35, RTL8231_REG_FUNC1, 3),
	RTL8231_GPIO_PIN(36),
};

static int rtl8231_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(rtl8231_pins);
}

static const char *rtl8231_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return rtl8231_pins[selector].name;
}

static int rtl8231_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
	const unsigned int **pins, unsigned int *num_pins)
{
	if (selector < ARRAY_SIZE(rtl8231_pins)) {
		*pins = &rtl8231_pins[selector].number;
		*num_pins = 1;
		return 0;
	}

	return -EINVAL;
}

static const struct pinctrl_ops rtl8231_pinctrl_ops = {
	.get_groups_count = rtl8231_get_groups_count,
	.get_group_name = rtl8231_get_group_name,
	.get_group_pins = rtl8231_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int rtl8231_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct rtl8231_pin_ctrl *ctrl = pinctrl_dev_get_drvdata(pctldev);

	return ctrl->nfunctions;
}

static const char *rtl8231_get_function_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct rtl8231_pin_ctrl *ctrl = pinctrl_dev_get_drvdata(pctldev);

	return ctrl->functions[selector].name;
}

static int rtl8231_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
	const char * const **groups, unsigned int *num_groups)
{
	struct rtl8231_pin_ctrl *ctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = ctrl->functions[selector].groups;
	*num_groups = ctrl->functions[selector].ngroups;
	return 0;
}

static int rtl8231_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
	unsigned int group_selector)
{
	struct rtl8231_pin_ctrl *ctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct rtl8231_pin_desc *desc = &rtl8231_pins[group_selector];
	unsigned int func_flag = BIT(func_selector);
	unsigned int function_mask;
	unsigned int gpio_function;
	int err = 0;

	if (!(desc->functions & func_flag))
		return -EINVAL;

	function_mask = BIT(desc->offset);
	gpio_function = desc->gpio_function_value << desc->offset;

	switch (func_flag) {
	case RTL8231_PIN_FUNCTION_LED:
	case RTL8231_PIN_FUNCTION_PWM:
		err = regmap_update_bits(ctrl->map, desc->reg, function_mask, ~gpio_function);
		break;
	case RTL8231_PIN_FUNCTION_GPIO:
		err = regmap_update_bits(ctrl->map, desc->reg, function_mask, gpio_function);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static int rtl8231_gpio_request_enable(struct pinctrl_dev *pctldev,
	struct pinctrl_gpio_range *range, unsigned int offset)
{
	struct rtl8231_pin_ctrl *ctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct rtl8231_pin_desc *desc = &rtl8231_pins[offset];
	unsigned int function_mask;
	unsigned int gpio_function;

	function_mask = BIT(desc->offset);
	gpio_function = desc->gpio_function_value << desc->offset;

	return regmap_update_bits(ctrl->map, desc->reg, function_mask, gpio_function);
}

static const struct pinmux_ops rtl8231_pinmux_ops = {
	.set_mux = rtl8231_set_mux,
	.get_functions_count = rtl8231_get_functions_count,
	.get_function_name = rtl8231_get_function_name,
	.get_function_groups = rtl8231_get_function_groups,
	.gpio_request_enable = rtl8231_gpio_request_enable,
	.strict = true
};


static int rtl8231_pinctrl_init_functions(struct device *dev, struct rtl8231_pin_ctrl *ctrl)
{
	struct rtl8231_function *function;
	const char **group_name;
	unsigned int f_idx;
	unsigned int pin;

	ctrl->nfunctions = ARRAY_SIZE(rtl8231_pin_function_names);
	ctrl->functions = devm_kcalloc(dev, ctrl->nfunctions, sizeof(*ctrl->functions), GFP_KERNEL);
	if (IS_ERR(ctrl->functions)) {
		dev_err(dev, "failed to allocate pin function descriptors\n");
		return PTR_ERR(ctrl->functions);
	}

	for (f_idx = 0; f_idx < ctrl->nfunctions; f_idx++) {
		function = &ctrl->functions[f_idx];
		function->name = rtl8231_pin_function_names[f_idx];

		for (pin = 0; pin < ctrl->pctl_desc.npins; pin++)
			if (rtl8231_pins[pin].functions & BIT(f_idx))
				function->ngroups++;

		function->groups = devm_kcalloc(dev, function->ngroups,
			sizeof(*function->groups), GFP_KERNEL);
		if (IS_ERR(function->groups)) {
			dev_err(dev, "failed to allocate pin function group names\n");
			return PTR_ERR(function->groups);
		}

		group_name = function->groups;
		for (pin = 0; pin < ctrl->pctl_desc.npins; pin++)
			if (rtl8231_pins[pin].functions & BIT(f_idx))
				*group_name++ = rtl8231_pins[pin].name;
	}

	return 0;
}

static int rtl8231_pinctrl_init(struct device *dev, struct rtl8231_pin_ctrl *ctrl)
{
	struct pinctrl_dev *pctl;
	struct pinctrl_pin_desc *pins;
	unsigned int pin;
	int err = 0;

	ctrl->pctl_desc.name = "rtl8231-pinctrl",
	ctrl->pctl_desc.owner = THIS_MODULE,
	ctrl->pctl_desc.pctlops = &rtl8231_pinctrl_ops,
	ctrl->pctl_desc.pmxops = &rtl8231_pinmux_ops,

	ctrl->pctl_desc.npins = ARRAY_SIZE(rtl8231_pins);
	pins = devm_kcalloc(dev, ctrl->pctl_desc.npins, sizeof(*pins), GFP_KERNEL);
	if (IS_ERR(pins)) {
		dev_err(dev, "failed to allocate pin descriptors\n");
		return PTR_ERR(pins);
	}
	ctrl->pctl_desc.pins = pins;

	for (pin = 0; pin < ctrl->pctl_desc.npins; pin++) {
		pins[pin].number = rtl8231_pins[pin].number;
		pins[pin].name = rtl8231_pins[pin].name;
	}

	err = rtl8231_pinctrl_init_functions(dev, ctrl);
	if (err)
		return err;

	err = devm_pinctrl_register_and_init(dev->parent, &ctrl->pctl_desc, ctrl, &pctl);
	if (err) {
		dev_err(dev, "failed to register pin controller\n");
		return err;
	}

	err = pinctrl_enable(pctl);
	if (err)
		dev_err(dev, "failed to enable pin controller\n");

	return err;
}

/*
 * GPIO controller functionality
 */
static int rtl8231_pin_read(struct rtl8231_pin_ctrl *ctrl, int base, int offset)
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

static int rtl8231_pin_write(struct rtl8231_pin_ctrl *ctrl, int base, int offset, int val)
{
	int field = base + offset / 16;
	int bit = offset % 16;

	return regmap_field_update_bits(ctrl->fields[field], BIT(bit), val << bit);
}

static int rtl8231_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);

	return rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DIR0, offset, RTL8231_GPIO_DIR_IN);
}

static int rtl8231_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);
	int err;

	err = rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DIR0, offset, RTL8231_GPIO_DIR_OUT);
	if (err)
		return err;

	return rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DATA0, offset, value);
}

static int rtl8231_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);

	return rtl8231_pin_read(ctrl, RTL8231_FIELD_GPIO_DIR0, offset);
}

static int rtl8231_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);

	return rtl8231_pin_read(ctrl, RTL8231_FIELD_GPIO_DATA0, offset);
}

static void rtl8231_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);

	rtl8231_pin_write(ctrl, RTL8231_FIELD_GPIO_DATA0, offset, value);
}

static int rtl8231_gpio_get_multiple(struct gpio_chip *gc,
	unsigned long *mask, unsigned long *bits)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);
	unsigned long sub_mask, bit_value;
	struct regmap_field **field;
	unsigned int reg_value;
	int offset, shift;
	int read;
	int err;

	field = &ctrl->fields[RTL8231_FIELD_GPIO_DATA0];

	for (read = 0; read < gc->ngpio; field++, read += 16) {
		shift = read % BITS_PER_TYPE(*bits);
		offset = read / BITS_PER_TYPE(*bits);
		sub_mask = mask[offset] & (0xffffUL << shift);
		if (sub_mask) {
			err = regmap_field_read(*field, &reg_value);
			if (err)
				return err;
			bit_value = ((unsigned long) reg_value) << shift;
			bits[offset] = (bits[offset] & ~sub_mask) | (bit_value & sub_mask);
		}
	}

	return err;
}

static void rtl8231_gpio_set_multiple(struct gpio_chip *gc,
	unsigned long *mask, unsigned long *bits)
{
	struct rtl8231_pin_ctrl *ctrl = gpiochip_get_data(gc);
	unsigned long sub_mask, value;
	struct regmap_field **field;
	int offset, shift;
	int read;

	field = &ctrl->fields[RTL8231_FIELD_GPIO_DATA0];

	for (read = 0; read < gc->ngpio; field++, read += 16) {
		shift = read % BITS_PER_TYPE(*bits);
		offset = read / BITS_PER_TYPE(*bits);
		sub_mask = (mask[offset] >> shift) & 0xffff;
		if (sub_mask) {
			value = bits[offset] >> shift;
			regmap_field_update_bits(*field, sub_mask, value);
		}
	}
}

static int rtl8231_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtl8231_pin_ctrl *ctrl;
	int err;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->map = dev_get_regmap(dev->parent, NULL);
	if (IS_ERR_OR_NULL(ctrl->map)) {
		dev_err(dev, "failed to retrieve regmap\n");
		if (!ctrl->map)
			return -ENODEV;
		else
			return PTR_ERR(ctrl->map);
	}

	err = devm_regmap_field_bulk_alloc(dev, ctrl->map, ctrl->fields, rtl8231_gpio_fields,
		ARRAY_SIZE(ctrl->fields));
	if (err) {
		dev_err(dev, "unable to allocate gpio regmap fields\n");
		return err;
	}

	err = rtl8231_pinctrl_init(dev, ctrl);
	if (err)
		return err;

	ctrl->gc.base = -1;
	ctrl->gc.ngpio = RTL8231_NUM_GPIOS;
	ctrl->gc.label = pdev->name;
	ctrl->gc.owner = THIS_MODULE;
	ctrl->gc.can_sleep = true;
	ctrl->gc.parent = dev->parent;

	ctrl->gc.set = rtl8231_gpio_set;
	ctrl->gc.set_multiple = rtl8231_gpio_set_multiple;
	ctrl->gc.get = rtl8231_gpio_get;
	ctrl->gc.get_multiple = rtl8231_gpio_get_multiple;
	ctrl->gc.direction_input = rtl8231_direction_input;
	ctrl->gc.direction_output = rtl8231_direction_output;
	ctrl->gc.get_direction = rtl8231_get_direction;
	ctrl->gc.request = gpiochip_generic_request;
	ctrl->gc.free = gpiochip_generic_free;

	return devm_gpiochip_add_data(dev, &ctrl->gc, ctrl);
}

static struct platform_driver rtl8231_pinctrl_driver = {
	.driver = {
		.name = "rtl8231-pinctrl",
	},
	.probe = rtl8231_pinctrl_probe,
};
module_platform_driver(rtl8231_pinctrl_driver);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Realtek RTL8231 pin control and GPIO support");
MODULE_LICENSE("GPL v2");
