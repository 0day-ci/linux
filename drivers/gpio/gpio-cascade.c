// SPDX-License-Identifier: GPL-2.0-only
/*
 *  A generic GPIO cascade driver
 *
 *  Copyright (C) 2021 Mauri Sandberg <maukka@ext.kapsi.fi>
 *
 * This allows building cascades of GPIO lines in a manner illustrated
 * below:
 *
 *                 /|---- Cascaded GPIO line 0
 *  Upstream      | |---- Cascaded GPIO line 1
 *  GPIO line ----+ | .
 *                | | .
 *                 \|---- Cascaded GPIO line n
 *
 * A gpio-mux is being used to select, which cascaded line is being
 * addressed at any given time.
 *
 * At the moment only input mode is supported due to lack of means for
 * testing output functionality. At least theoretically output should be
 * possible with an open drain constructions.
 */

#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mux/consumer.h>

struct gpio_cascade {
	struct device		*parent;
	struct gpio_chip	gpio_chip;
	struct mux_control	*mux_control;
	struct gpio_desc	*upstream_line;
};

static struct gpio_cascade *chip_to_cascade(struct gpio_chip *gc)
{
	return container_of(gc, struct gpio_cascade, gpio_chip);
}

static int gpio_cascade_get_direction(struct gpio_chip *gc,
					unsigned int offset)
{
	return GPIO_LINE_DIRECTION_IN;
}

static int gpio_cascade_get_value(struct gpio_chip *gc, unsigned int offset)
{
	struct gpio_cascade *cas;
	int ret;

	cas = chip_to_cascade(gc);
	ret = mux_control_select(cas->mux_control, offset);
	if (ret)
		return ret;

	ret = gpiod_get_value(cas->upstream_line);
	mux_control_deselect(cas->mux_control);
	return ret;
}

static int gpio_cascade_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct gpio_cascade *cas;
	struct mux_control *mc;
	struct gpio_desc *upstream;
	struct gpio_chip *gc;
	int err;

	cas = devm_kzalloc(dev, sizeof(struct gpio_cascade), GFP_KERNEL);
	if (cas == NULL)
		return -ENOMEM;

	mc = devm_mux_control_get(dev, NULL);
	if (IS_ERR(mc)) {
		err = (int) PTR_ERR(mc);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "unable to get mux-control: %d\n", err);
		return err;
	}

	cas->mux_control = mc;
	upstream = devm_gpiod_get(dev, "upstream",  GPIOD_IN);
	if (IS_ERR(upstream)) {
		err = (int) PTR_ERR(upstream);
		dev_err(dev, "unable to claim upstream GPIO line: %d\n", err);
		return err;
	}

	cas->upstream_line = upstream;
	cas->parent = dev;

	gc = &cas->gpio_chip;
	gc->get = gpio_cascade_get_value;
	gc->get_direction = gpio_cascade_get_direction;

	gc->base = -1;
	gc->ngpio = mux_control_states(mc);
	gc->label = dev_name(cas->parent);
	gc->parent = cas->parent;
	gc->owner = THIS_MODULE;
	gc->of_node = np;

	err = gpiochip_add(&cas->gpio_chip);
	if (err) {
		dev_err(dev, "unable to add gpio chip, err=%d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, cas);
	dev_info(dev, "registered %u cascaded GPIO lines\n", gc->ngpio);
	return 0;
}

static const struct of_device_id gpio_cascade_id[] = {
	{
		.compatible = "gpio-cascade",
		.data = NULL,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_cascade_id);

static struct platform_driver gpio_cascade_driver = {
	.driver	= {
		.name		= "gpio-cascade",
		.of_match_table = gpio_cascade_id,
	},
	.probe	= gpio_cascade_probe,
};
module_platform_driver(gpio_cascade_driver);

MODULE_AUTHOR("Mauri Sandberg <maukka@ext.kapsi.fi>");
MODULE_DESCRIPTION("Generic GPIO cascade");
MODULE_LICENSE("GPL");
