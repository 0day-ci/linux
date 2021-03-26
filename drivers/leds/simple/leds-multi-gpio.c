// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Axis Communications AB
 */

#include <linux/leds.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>


#define MAX_GPIO_NUM  8

struct multi_gpio_led_priv {
	struct led_classdev cdev;

	struct gpio_descs *gpios;

	u16 nr_states;

	u8 states[0];
};


static void multi_gpio_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct multi_gpio_led_priv *priv;
	int idx;

	DECLARE_BITMAP(values, MAX_GPIO_NUM);

	priv = container_of(led_cdev, struct multi_gpio_led_priv, cdev);

	idx = value > led_cdev->max_brightness ? led_cdev->max_brightness : value;

	values[0] = priv->states[idx];

	gpiod_set_array_value(priv->gpios->ndescs, priv->gpios->desc,
	    priv->gpios->info, values);
}

static int multi_gpio_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct multi_gpio_led_priv *priv = NULL;
	int ret;
	const char *state = NULL;
	struct led_init_data init_data = {};
	struct gpio_descs *gpios;
	u16 nr_states;

	gpios = devm_gpiod_get_array(dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(gpios))
		return PTR_ERR(gpios);

	if (gpios->ndescs >= MAX_GPIO_NUM) {
		dev_err(dev, "Too many GPIOs\n");
		return -EINVAL;
	}

	ret = of_property_count_u8_elems(node, "led-states");
	if (ret < 0)
		return ret;

	if (ret != 1 << gpios->ndescs) {
		dev_err(dev, "led-states number should equal to 2^led-gpios\n");
		return -EINVAL;
	}

	nr_states = ret;

	priv = devm_kzalloc(dev, sizeof(struct multi_gpio_led_priv)
			+ sizeof(u8) * nr_states , GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_property_read_u8_array(node, "led-states", priv->states, nr_states);
	if (ret)
		return ret;

	priv->gpios = gpios;
	priv->nr_states = nr_states;

	priv->cdev.max_brightness = nr_states;
	priv->cdev.default_trigger = of_get_property(node, "linux,default-trigger", NULL);
	priv->cdev.brightness_set = multi_gpio_led_set;

	init_data.fwnode = of_fwnode_handle(node);

	ret = devm_led_classdev_register_ext(dev, &priv->cdev, &init_data);
	if (ret < 0)
		return ret;

	of_property_read_string(node, "default-state", &state);
	if (!strcmp(state, "on"))
		multi_gpio_led_set(&priv->cdev, priv->cdev.max_brightness);
	else
		multi_gpio_led_set(&priv->cdev, 0);

	platform_set_drvdata(pdev, priv);

	return 0;
}

static void multi_gpio_led_shutdown(struct platform_device *pdev)
{
	struct multi_gpio_led_priv *priv = platform_get_drvdata(pdev);

	multi_gpio_led_set(&priv->cdev, 0);
}

static int multi_gpio_led_remove(struct platform_device *pdev)
{
	multi_gpio_led_shutdown(pdev);

	return 0;
}

static const struct of_device_id of_multi_gpio_led_match[] = {
	{ .compatible = "multi-gpio-led", },
	{},
};

MODULE_DEVICE_TABLE(of, of_multi_gpio_led_match);

static struct platform_driver multi_gpio_led_driver = {
	.probe		= multi_gpio_led_probe,
	.remove		= multi_gpio_led_remove,
	.shutdown	= multi_gpio_led_shutdown,
	.driver		= {
		.name	= "multi-gpio-led",
		.of_match_table = of_multi_gpio_led_match,
	},
};

module_platform_driver(multi_gpio_led_driver);

MODULE_AUTHOR("Hermes Zhang <chenhui.zhang@axis.com>");
MODULE_DESCRIPTION("Multiple GPIOs LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-multi-gpio");
