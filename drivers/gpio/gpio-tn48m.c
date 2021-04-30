// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delta TN48M CPLD GPIO driver
 *
 * Copyright 2020 Sartura Ltd
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/tn48m.h>
#include <dt-bindings/gpio/tn48m-gpio.h>

struct tn48m_gpio {
	struct gpio_chip chip;
	struct tn48m_data *data;
};

static int tn48m_gpio_get_direction(struct gpio_chip *chip,
				    unsigned int offset)
{
	switch (offset) {
	case SFP_TX_DISABLE_52:
	case SFP_TX_DISABLE_51:
	case SFP_TX_DISABLE_50:
	case SFP_TX_DISABLE_49:
		return GPIO_LINE_DIRECTION_OUT;
	case SFP_PRESENT_52:
	case SFP_PRESENT_51:
	case SFP_PRESENT_50:
	case SFP_PRESENT_49:
	case SFP_LOS_52:
	case SFP_LOS_51:
	case SFP_LOS_50:
	case SFP_LOS_49:
		return GPIO_LINE_DIRECTION_IN;
	default:
		return -EINVAL;
	}
}

static int tn48m_gpio_get_reg(unsigned int offset)
{
	switch (offset) {
	case SFP_TX_DISABLE_52:
	case SFP_TX_DISABLE_51:
	case SFP_TX_DISABLE_50:
	case SFP_TX_DISABLE_49:
		return SFP_TX_DISABLE;
	case SFP_PRESENT_52:
	case SFP_PRESENT_51:
	case SFP_PRESENT_50:
	case SFP_PRESENT_49:
		return SFP_PRESENT;
	case SFP_LOS_52:
	case SFP_LOS_51:
	case SFP_LOS_50:
	case SFP_LOS_49:
		return SFP_LOS;
	default:
		return -EINVAL;
	}
}

static int tn48m_gpio_get_mask(unsigned int offset)
{
	switch (offset) {
	case SFP_TX_DISABLE_52:
	case SFP_PRESENT_52:
	case SFP_LOS_52:
		return BIT(3);
	case SFP_TX_DISABLE_51:
	case SFP_PRESENT_51:
	case SFP_LOS_51:
		return BIT(2);
	case SFP_TX_DISABLE_50:
	case SFP_PRESENT_50:
	case SFP_LOS_50:
		return BIT(1);
	case SFP_TX_DISABLE_49:
	case SFP_PRESENT_49:
	case SFP_LOS_49:
		return BIT(0);
	default:
		return -EINVAL;
	}
}

static int tn48m_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tn48m_gpio *gpio = gpiochip_get_data(chip);
	unsigned int regval;
	int ret;

	ret = regmap_read(gpio->data->regmap,
			  tn48m_gpio_get_reg(offset),
			  &regval);
	if (ret < 0)
		return ret;

	return regval & tn48m_gpio_get_mask(offset);
}

static void tn48m_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct tn48m_gpio *gpio = gpiochip_get_data(chip);

	regmap_update_bits(gpio->data->regmap,
			   tn48m_gpio_get_reg(offset),
			   tn48m_gpio_get_mask(offset),
			   value ? tn48m_gpio_get_mask(offset) : 0);
}

static int tn48m_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	tn48m_gpio_set(chip, offset, value);

	return 0;
}

/*
 * Required for SFP as it calls gpiod_direction_input()
 * and if its missing TX disable GPIO will print an
 * error and not be controlled anymore.
 */
static int tn48m_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	return 0;
}

static const struct gpio_chip tn48m_template_chip = {
	.label			= "tn48m-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= tn48m_gpio_get_direction,
	.direction_output	= tn48m_gpio_direction_output,
	.direction_input	= tn48m_gpio_direction_input,
	.get			= tn48m_gpio_get,
	.set			= tn48m_gpio_set,
	.base			= -1,
	.ngpio			= 12,
	.can_sleep		= true,
};

static int tn48m_gpio_probe(struct platform_device *pdev)
{
	struct tn48m_gpio *gpio;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, gpio);

	gpio->data = dev_get_drvdata(pdev->dev.parent);
	gpio->chip = tn48m_template_chip;
	gpio->chip.parent = gpio->data->dev;

	ret = devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct platform_device_id tn48m_gpio_id_table[] = {
	{ "delta,tn48m-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(platform, tn48m_gpio_id_table);

static struct platform_driver tn48m_gpio_driver = {
	.driver = {
		.name = "tn48m-gpio",
	},
	.probe = tn48m_gpio_probe,
	.id_table = tn48m_gpio_id_table,
};
module_platform_driver(tn48m_gpio_driver);

MODULE_AUTHOR("Robert Marko <robert.marko@sartura.hr>");
MODULE_DESCRIPTION("Delta TN48M CPLD GPIO driver");
MODULE_LICENSE("GPL");
