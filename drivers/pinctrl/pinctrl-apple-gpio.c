// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple SoC pinctrl+GPIO+external IRQ driver
 *
 * Copyright (C) 2021 The Asahi Linux Contributors
 * Copyright (C) 2020 Corellium LLC
 *
 * Based on: pinctrl-pistachio.c
 * Copyright (C) 2014 Imagination Technologies Ltd.
 * Copyright (C) 2014 Google, Inc.
 */

#define USE_PINMUX_GENERIC_FN 1
#define USE_PINCTRL_GENERIC_FN 1

#include <dt-bindings/pinctrl/apple.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>

#include "pinctrl-utils.h"
#include "core.h"
#include "pinmux.h"

struct apple_gpio_pincfg {
	uint8_t irqtype;
	uint8_t stat;
};

#define PINCFG_STAT_OUTVAL	0x01
#define PINCFG_STAT_OUTEN	0x02
#define PINCFG_STAT_PERIPH	0x20
#define PINCFG_STAT_IRQEN	0x80

struct apple_gpio_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;

	unsigned int npins;
	struct pinctrl_pin_desc *pins;
	struct apple_gpio_pincfg *pin_cfgs;
	const char **pin_names;
	unsigned *pin_nums;

	void __iomem *base;
	unsigned int nirqgrps;

	struct pinctrl_desc pinctrl_desc;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
};

#define REG_GPIO(x)		(4 * (x))
#define  REG_GPIOx_DATA		BIT(0)
#define  REG_GPIOx_MODE_MASK	GENMASK(3, 1)
#define REG_GPIOx_OUT	(1 << REG_GPIOx_DATA)
#define REG_GPIOx_IN_IRQ_HI	(2 << REG_GPIOx_DATA)
#define REG_GPIOx_IN_IRQ_LO	(3 << REG_GPIOx_DATA)
#define REG_GPIOx_IN_IRQ_UP	(4 << REG_GPIOx_DATA)
#define REG_GPIOx_IN_IRQ_DN	(5 << REG_GPIOx_DATA)
#define REG_GPIOx_IN_IRQ_ANY	(6 << REG_GPIOx_DATA)
#define REG_GPIOx_IN_IRQ_OFF	(7 << REG_GPIOx_DATA)
#define  REG_GPIOx_PERIPH	BIT(5)
#define  REG_GPIOx_CFG_DONE	BIT(9)
#define  REG_GPIOx_GRP_MASK	GENMASK(18, 16)
#define REG_IRQ(g,x)		(0x800 + 0x40 * (g) + 4 * ((x) >> 5))

static void apple_gpio_set_reg(struct apple_gpio_pinctrl *pctl, unsigned pin, uint32_t clr, uint32_t set)
{
	void __iomem *ppin = pctl->base + pin * 4;
	uint32_t prev, cfg;

	prev = readl(ppin);
	cfg = (prev & ~clr) | set;

	if(!(prev & REG_GPIOx_CFG_DONE))
		writel(cfg & ~REG_GPIOx_CFG_DONE, ppin);
	writel(cfg, ppin);
}

static void apple_gpio_refresh_reg(struct apple_gpio_pinctrl *pctl, unsigned pin)
{
	struct apple_gpio_pincfg *pincfg = &pctl->pin_cfgs[pin];

	int stat = pincfg->stat;
	int outval = (stat & PINCFG_STAT_OUTVAL);

	int clear = REG_GPIOx_MODE_MASK | REG_GPIOx_DATA;
	int set = REG_GPIOx_CFG_DONE | outval;

	if (stat & PINCFG_STAT_PERIPH) {
		set |= REG_GPIOx_PERIPH;
	} else {
		clear |= REG_GPIOx_PERIPH;
		if (stat & PINCFG_STAT_OUTEN)
			set |= REG_GPIOx_OUT;
		else if (stat & PINCFG_STAT_IRQEN)
			set |= pincfg->irqtype;
		else
			set |= REG_GPIOx_IN_IRQ_OFF;
	}

	apple_gpio_set_reg(pctl, pin, clear, set);
}

static uint32_t apple_gpio_get_reg(struct apple_gpio_pinctrl *pctl, unsigned pin)
{
	return readl(pctl->base + pin * 4);
}

static void apple_gpio_init_reg(struct apple_gpio_pinctrl *pctl, unsigned pin)
{
	struct apple_gpio_pincfg *pincfg = &pctl->pin_cfgs[pin];
	uint32_t reg = apple_gpio_get_reg(pctl, pin);

	pincfg->irqtype = 0;
	if(reg & REG_GPIOx_PERIPH) {
		pincfg->stat = PINCFG_STAT_PERIPH;
	} else if((reg & REG_GPIOx_MODE_MASK) == REG_GPIOx_OUT) {
		pincfg->stat = PINCFG_STAT_OUTEN | (reg & PINCFG_STAT_OUTVAL);
	} else if((reg & REG_GPIOx_MODE_MASK) == REG_GPIOx_IN_IRQ_OFF || !(reg & REG_GPIOx_MODE_MASK)) {
		pincfg->stat = 0;
	} else {
		pincfg->irqtype = reg & REG_GPIOx_MODE_MASK;
		pincfg->stat = PINCFG_STAT_IRQEN;
	}
}

/* Pin controller functions */

#if !USE_PINCTRL_GENERIC_FN
static int apple_gpio_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct apple_gpio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->npins;
}

static const char *apple_gpio_pinctrl_get_group_name(struct pinctrl_dev *pctldev, unsigned group)
{
	struct apple_gpio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->pins[group].name;
}

static int apple_gpio_pinctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned group, const unsigned **pins, unsigned *num_pins)
{
	struct apple_gpio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pctl->pin_nums[group];
	*num_pins = 1;

	return 0;
}
#endif

#if !USE_PINMUX_GENERIC_FN
static const char *apple_gpio_pinmux_get_function_name(struct pinctrl_dev *pctldev, unsigned func);
static int apple_gpio_pinmux_get_functions_count(struct pinctrl_dev *pctldev);
#endif

static int apple_gpio_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	unsigned reserved_maps;
	struct apple_gpio_pinctrl *pctl;
	u32 pinfunc, pin, func;
	int num_pins, i;
	const char* group_name;
	const char* function_name;
	struct device_node *node = np_config;
	int ret = 0;

	*map = NULL;
	*num_maps = 0;
	reserved_maps = 0;

	pctl = pinctrl_dev_get_drvdata(pctldev);

	ret = of_property_count_u32_elems(node, "pinmux");
	if (ret <= 0) {
		dev_err(pctl->dev, "missing or empty pinmux property in node %pOFn.\n", node);
		return -EINVAL;
	}

	num_pins = ret;

	ret = pinctrl_utils_reserve_map(pctldev, map,
			&reserved_maps, num_maps, num_pins);
	if (ret) {
		return ret;
	}

	for (i = 0; i < num_pins; i++) {
		ret = of_property_read_u32_index(node, "pinmux",
				i, &pinfunc);
		if (ret) {
			goto free_map;
		}

		pin = APPLE_PIN(pinfunc);
		func = APPLE_FUNC(pinfunc);

#if USE_PINMUX_GENERIC_FN
		if (func >=pinmux_generic_get_function_count(pctldev)) {
#else
		if (func >= apple_gpio_pinmux_get_functions_count(pctldev)) {
#endif
			ret = -EINVAL;
			goto free_map;
		}

#if USE_PINCTRL_GENERIC_FN
		group_name = pinctrl_generic_get_group_name(pctldev, pin);
#else
		group_name = apple_gpio_pinctrl_get_group_name(pctldev, pin);
#endif

#if USE_PINMUX_GENERIC_FN
		function_name = pinmux_generic_get_function_name(pctl->pctldev, func);
#else
		function_name = apple_gpio_pinmux_get_function_name(pctl->pctldev, func);
#endif

		ret = pinctrl_utils_add_map_mux(pctl->pctldev, map, &reserved_maps, num_maps,
				group_name, function_name);
		if (ret) {
			goto free_map;
		}
	}

free_map:
	if (ret < 0) {
		pinctrl_utils_free_map(pctldev, *map, *num_maps);
		return ret;
	}

	return 0;
}

static const struct pinctrl_ops apple_gpio_pinctrl_ops = {
#if USE_PINCTRL_GENERIC_FN
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
#else
	.get_groups_count = apple_gpio_pinctrl_get_groups_count,
	.get_group_name = apple_gpio_pinctrl_get_group_name,
	.get_group_pins = apple_gpio_pinctrl_get_group_pins,
#endif
	.dt_node_to_map = apple_gpio_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

/* Pin multiplexer functions */

#if !USE_PINMUX_GENERIC_FN
static int apple_gpio_pinmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	return 2;
}

static const char *apple_gpio_pinmux_get_function_name(struct pinctrl_dev *pctldev, unsigned func)
{
	return func ? "periph" : "gpio";
}

static int apple_gpio_pinmux_get_function_groups(struct pinctrl_dev *pctldev, unsigned func, const char * const **groups, unsigned * const num_groups)
{
	struct apple_gpio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->pin_names;
	*num_groups = pctl->npins;
	return 0;
}
#endif

static int apple_gpio_pinmux_enable(struct pinctrl_dev *pctldev, unsigned func, unsigned group)
{
	struct apple_gpio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	if(func)
		pctl->pin_cfgs[group].stat |= PINCFG_STAT_PERIPH;
	else
		pctl->pin_cfgs[group].stat &= ~PINCFG_STAT_PERIPH;
	apple_gpio_refresh_reg(pctl, group);

	return 0;
}

static const struct pinmux_ops apple_gpio_pinmux_ops = {
#if USE_PINMUX_GENERIC_FN
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
#else
	.get_functions_count = apple_gpio_pinmux_get_functions_count,
	.get_function_name = apple_gpio_pinmux_get_function_name,
	.get_function_groups = apple_gpio_pinmux_get_function_groups,
#endif
	.set_mux = apple_gpio_pinmux_enable,
	.strict = true,
};

/* GPIO chip functions */

static int apple_gpio_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	return !(pctl->pin_cfgs[offset].stat & PINCFG_STAT_OUTEN) ?
			GPIO_LINE_DIRECTION_IN : GPIO_LINE_DIRECTION_OUT;
}

static int apple_gpio_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);
	uint32_t reg;

	reg = apple_gpio_get_reg(pctl, offset);
	return !!(reg & REG_GPIOx_DATA);
}

static void apple_gpio_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	if(value)
		pctl->pin_cfgs[offset].stat |= PINCFG_STAT_OUTVAL;
	else
		pctl->pin_cfgs[offset].stat &= ~PINCFG_STAT_OUTVAL;
	apple_gpio_refresh_reg(pctl, offset);
}

static int apple_gpio_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	pctl->pin_cfgs[offset].stat &= ~PINCFG_STAT_OUTEN;
	apple_gpio_refresh_reg(pctl, offset);
	return 0;
}

static int apple_gpio_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	int clear = PINCFG_STAT_PERIPH;
	int set = PINCFG_STAT_OUTEN;

	if (value)
		set |= PINCFG_STAT_OUTVAL;
	else
		clear |= PINCFG_STAT_OUTVAL;

	pctl->pin_cfgs[offset].stat &= ~clear;
	pctl->pin_cfgs[offset].stat |= set;

	apple_gpio_refresh_reg(pctl, offset);
	return 0;
}

/* IRQ chip functions */

static void apple_gpio_gpio_irq_ack(struct irq_data *data)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned irqgrp = FIELD_GET(REG_GPIOx_GRP_MASK, apple_gpio_get_reg(pctl, data->hwirq));

	writel(1u << (data->hwirq & 31), pctl->base + REG_IRQ(irqgrp, data->hwirq));
}

static void apple_gpio_gpio_irq_mask(struct irq_data *data)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));

	pctl->pin_cfgs[data->hwirq].stat &= ~PINCFG_STAT_IRQEN;
	apple_gpio_refresh_reg(pctl, data->hwirq);
}

static void apple_gpio_gpio_irq_unmask(struct irq_data *data)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));

	pctl->pin_cfgs[data->hwirq].stat |= PINCFG_STAT_IRQEN;
	apple_gpio_refresh_reg(pctl, data->hwirq);
}

static unsigned int apple_gpio_gpio_irq_startup(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);
	unsigned irqgrp = 0;

	apple_gpio_set_reg(pctl, data->hwirq, REG_GPIOx_GRP_MASK, FIELD_PREP(REG_GPIOx_GRP_MASK, irqgrp));

	apple_gpio_gpio_direction_input(chip, data->hwirq);
	apple_gpio_gpio_irq_unmask(data);

	return 0;
}

static int apple_gpio_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));

	switch(type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		pctl->pin_cfgs[data->hwirq].irqtype = REG_GPIOx_IN_IRQ_UP;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		pctl->pin_cfgs[data->hwirq].irqtype = REG_GPIOx_IN_IRQ_DN;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		pctl->pin_cfgs[data->hwirq].irqtype = REG_GPIOx_IN_IRQ_ANY;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		pctl->pin_cfgs[data->hwirq].irqtype = REG_GPIOx_IN_IRQ_HI;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		pctl->pin_cfgs[data->hwirq].irqtype = REG_GPIOx_IN_IRQ_LO;
		break;
	default:
		return -EINVAL;
	}

	apple_gpio_refresh_reg(pctl, data->hwirq);

	if(type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(data, handle_level_irq);
	else
		irq_set_handler_locked(data, handle_edge_irq);
	return 0;
}

static void apple_gpio_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(gc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned irqgrp, pinh, pinl;
	unsigned long pending;
	unsigned int parent = irq_desc_get_irq(desc);

	for (irqgrp = 0; irqgrp < pctl->nirqgrps; ++irqgrp) {
		if (parent == gc->irq.parents[irqgrp])
			break;
	}

	WARN_ON(irqgrp == pctl->nirqgrps);

	chained_irq_enter(chip, desc);
	for(pinh=0; pinh<pctl->npins; pinh+=32) {
		pending = readl(pctl->base + REG_IRQ(irqgrp, pinh));
		for_each_set_bit(pinl, &pending, 32)
			generic_handle_irq(irq_linear_revmap(gc->irq.domain, pinh + pinl));
	}
	chained_irq_exit(chip, desc);
}

/* Probe & register */

static int apple_gpio_gpio_register(struct apple_gpio_pinctrl *pctl)
{
	struct device_node *node = pctl->dev->of_node;
	struct gpio_irq_chip *girq;
	int i, ret = 0;

	if(!of_find_property(node, "gpio-controller", NULL)) {
		dev_err(pctl->dev, "Apple GPIO must have 'gpio-controller' property.\n");
		return -ENODEV;
	}

	pctl->gpio_chip.label = dev_name(pctl->dev);
	pctl->gpio_chip.request = gpiochip_generic_request;
	pctl->gpio_chip.free = gpiochip_generic_free;
	pctl->gpio_chip.get_direction = apple_gpio_gpio_get_direction;
	pctl->gpio_chip.direction_input = apple_gpio_gpio_direction_input;
	pctl->gpio_chip.direction_output = apple_gpio_gpio_direction_output;
	pctl->gpio_chip.get = apple_gpio_gpio_get;
	pctl->gpio_chip.set = apple_gpio_gpio_set;
	pctl->gpio_chip.base = -1;
	pctl->gpio_chip.ngpio = pctl->npins;
	pctl->gpio_chip.parent = pctl->dev;
	pctl->gpio_chip.of_node = node;

	if (of_find_property(node, "interrupt-controller", NULL)) {
		ret = platform_irq_count(to_platform_device(pctl->dev));
		if(ret < 0)
			return ret;

		pctl->nirqgrps = ret;

		pctl->irq_chip.name = dev_name(pctl->dev);
		pctl->irq_chip.irq_startup = apple_gpio_gpio_irq_startup;
		pctl->irq_chip.irq_ack = apple_gpio_gpio_irq_ack;
		pctl->irq_chip.irq_mask = apple_gpio_gpio_irq_mask;
		pctl->irq_chip.irq_unmask = apple_gpio_gpio_irq_unmask;
		pctl->irq_chip.irq_set_type = apple_gpio_gpio_irq_set_type;

		girq = &pctl->gpio_chip.irq;
		girq->chip = &pctl->irq_chip;
		girq->parent_handler = apple_gpio_gpio_irq_handler;
		girq->num_parents = pctl->nirqgrps;

		girq->parents = devm_kmalloc_array(pctl->dev, pctl->nirqgrps,
			sizeof(girq->parents[0]), GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;

		for(i = 0; i < pctl->nirqgrps; i++) {
			ret = platform_get_irq(to_platform_device(pctl->dev), i);
			if(ret < 0) {
				if(ret != -EPROBE_DEFER)
					dev_err(pctl->dev, "Failed to map IRQ %d (%d).\n", i, ret);
				return ret;
			}
			girq->parents[i] = ret;
		}

		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
	}

	ret = devm_gpiochip_add_data(pctl->dev, &pctl->gpio_chip, pctl);
	if(ret < 0) {
		dev_err(pctl->dev, "Failed to add GPIO chip (%d).\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id apple_gpio_pinctrl_of_match[] = {
	{ .compatible = "apple,t8103-pinctrl", },
	{ },
};

static int apple_gpio_pinctrl_probe(struct platform_device *pdev)
{
	struct apple_gpio_pinctrl *pctl;
	struct of_phandle_args pinspec;
	int res;
	unsigned pin_base, i;

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if(!pctl)
		return -ENOMEM;
	pctl->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, pctl);

	if (of_parse_phandle_with_fixed_args(pdev->dev.of_node, "gpio-ranges",
			3, 0, &pinspec)) {
		dev_err(&pdev->dev, "gpio-ranges property not found\n");
		return -EINVAL;
	}

	pctl->npins = pinspec.args[2];
	pin_base = pinspec.args[1];

	pctl->pins = devm_kmalloc_array(&pdev->dev, pctl->npins, sizeof(pctl->pins[0]), GFP_KERNEL);
	if(!pctl->pins)
		return -ENOMEM;
	pctl->pin_names = devm_kmalloc_array(&pdev->dev, pctl->npins, sizeof(pctl->pin_names[0]), GFP_KERNEL);
	if(!pctl->pin_names)
		return -ENOMEM;
	pctl->pin_nums = devm_kmalloc_array(&pdev->dev, pctl->npins, sizeof(pctl->pin_nums[0]), GFP_KERNEL);
	if(!pctl->pin_nums)
		return -ENOMEM;
	pctl->pin_cfgs = devm_kmalloc_array(&pdev->dev, pctl->npins, sizeof(pctl->pin_cfgs[0]), GFP_KERNEL);
	if(!pctl->pin_cfgs)
		return -ENOMEM;

	pctl->base = devm_platform_ioremap_resource(pdev, 0);
	if(IS_ERR(pctl->base))
		return PTR_ERR(pctl->base);

	for(i=0; i<pctl->npins; i++) {
		apple_gpio_init_reg(pctl, i);

		pctl->pins[i].number = i + pin_base;
		pctl->pins[i].name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "PIN%u", i + pin_base);
		pctl->pins[i].drv_data = pctl;
		pctl->pin_names[i] = pctl->pins[i].name;
		pctl->pin_nums[i] = i + pin_base;
	}

	pctl->pinctrl_desc.name = dev_name(pctl->dev);
	pctl->pinctrl_desc.pins = pctl->pins;
	pctl->pinctrl_desc.npins = pctl->npins;
	pctl->pinctrl_desc.pctlops = &apple_gpio_pinctrl_ops;
	pctl->pinctrl_desc.pmxops = &apple_gpio_pinmux_ops;

	pctl->pctldev = devm_pinctrl_register(&pdev->dev, &pctl->pinctrl_desc, pctl);
	if (IS_ERR(pctl->pctldev)) {
		dev_err(&pdev->dev, "Failed to register pinctrl device.\n");
		return PTR_ERR(pctl->pctldev);
	}

#if USE_PINCTRL_GENERIC_FN
	for (i = 0; i < pctl->npins; i++) {
		res = pinctrl_generic_add_group(pctl->pctldev, pctl->pins[i].name,
						pctl->pin_nums + i, 1, pctl);
		if (res < 0) {
			dev_err(pctl->dev, "Failed to register group.");
			return res;
		}
	}
#endif

#if USE_PINMUX_GENERIC_FN
	res = pinmux_generic_add_function(pctl->pctldev, "gpio", pctl->pin_names, pctl->npins, pctl);

	if (res < 0) {
		dev_err(pctl->dev, "Failed to register function.");
		return res;
	}

	res = pinmux_generic_add_function(pctl->pctldev, "periph", pctl->pin_names, pctl->npins, pctl);

	if (res < 0) {
		dev_err(pctl->dev, "Failed to register function.");
		return res;
	}
#endif

	return apple_gpio_gpio_register(pctl);
}

static struct platform_driver apple_gpio_pinctrl_driver = {
	.driver = {
		.name = "apple-gpio-pinctrl",
		.of_match_table = apple_gpio_pinctrl_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = apple_gpio_pinctrl_probe,
};

module_platform_driver(apple_gpio_pinctrl_driver);

MODULE_DESCRIPTION("Apple pinctrl/GPIO driver");
MODULE_AUTHOR("Stan Skowronek <stan@corellium.com>");
MODULE_AUTHOR("Joey Gouly <joey.gouly@arm.com>");
MODULE_LICENSE("GPL v2");
