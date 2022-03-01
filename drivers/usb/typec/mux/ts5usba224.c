// SPDX-License-Identifier: GPL-2.0+
/*
 * TI TS5USBA224 USB 2.0/audio switch mux driver
 *
 * Copyright (c) 2021 Alvin Šipraga <alsi@bang-olufsen.dk>
 */

#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_mux.h>

struct ts5usba224 {
	struct device *dev;
	struct typec_mux *mux;
	struct gpio_desc *a_sel;
};

static int ts5usba224_mux_set(struct typec_mux *mux,
			      struct typec_mux_state *state)
{
	struct ts5usba224 *chip = typec_mux_get_drvdata(mux);

	switch (state->mode) {
	case TYPEC_MODE_AUDIO:
		gpiod_set_value_cansleep(chip->a_sel, 1);
		dev_dbg(chip->dev, "audio switch enabled\n");
		break;
	case TYPEC_STATE_USB:
	case TYPEC_MODE_USB2:
	default:
		dev_dbg(chip->dev, "audio switch disabled\n");
		gpiod_set_value_cansleep(chip->a_sel, 0);
		break;
	}

	return 0;
}

static int ts5usba224_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct typec_mux_desc mux_desc = {};
	struct ts5usba224 *chip;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;

	chip->a_sel = devm_gpiod_get(dev, "asel", GPIOD_OUT_LOW);
	if (IS_ERR(chip->a_sel)) {
		ret = PTR_ERR(chip->a_sel);
		return dev_err_probe(dev, ret, "failed to get A_SEL GPIO\n");
	}

	mux_desc.drvdata = chip;
	mux_desc.fwnode = dev->fwnode;
	mux_desc.set = ts5usba224_mux_set;

	chip->mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(chip->mux))
		return PTR_ERR(chip->mux);

	platform_set_drvdata(pdev, chip);

	return 0;
}

static int ts5usba224_remove(struct platform_device *pdev)
{
	struct ts5usba224 *chip = platform_get_drvdata(pdev);

	typec_mux_unregister(chip->mux);

	return 0;
}

static const struct of_device_id ts5usba224_of_match[] = {
	{ .compatible = "ti,ts5usba224", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ts5usba224_of_match);

static struct platform_driver ts5usba224_driver = {
	.driver = {
		.name = "ts5usba224",
		.of_match_table = of_match_ptr(ts5usba224_of_match),
	},
	.probe  = ts5usba224_probe,
	.remove = ts5usba224_remove,
};

module_platform_driver(ts5usba224_driver);

MODULE_AUTHOR("Alvin Šipraga <alsi@bang-olufsen.dk>");
MODULE_DESCRIPTION("TI TS5USBA224 mux driver");
MODULE_LICENSE("GPL");
