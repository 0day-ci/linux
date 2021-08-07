// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Raspberry Pi Sense HAT joystick driver
 * http://raspberrypi.org
 *
 * Copyright (C) 2015 Raspberry Pi
 * Copyright (C) 2021 Charles Mirabile, Mwesigwa Guma, Joel Savitz
 *
 * Original Author: Serge Schneider
 * Revised for upstream Linux by: Charles Mirabile, Mwesigwa Guma, Joel Savitz
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

#include <linux/mfd/rpisense.h>

static unsigned char keymap[] = {KEY_DOWN, KEY_RIGHT, KEY_UP, KEY_ENTER, KEY_LEFT,};

static irqreturn_t rpisense_js_report(int n, void *cookie)
{
	int i;
	static s32 prev_keys;
	struct rpisense *rpisense = cookie;
	struct rpisense_js *rpisense_js = &rpisense->joystick;
	s32 keys = rpisense_get_joystick_state(rpisense);
	s32 changes = keys ^ prev_keys;

	prev_keys = keys;
	for (i = 0; i < ARRAY_SIZE(keymap); ++i) {
		if (changes & (1<<i)) {
			input_report_key(rpisense_js->keys_dev,
					 keymap[i], keys & (1<<i));
		}
	}
	input_sync(rpisense_js->keys_dev);
	return IRQ_HANDLED;
}

static int rpisense_js_probe(struct platform_device *pdev)
{
	int ret;
	int i;
	struct rpisense *rpisense = dev_get_drvdata(&pdev->dev);
	struct rpisense_js *rpisense_js = &rpisense->joystick;

	rpisense_js->keys_desc = devm_gpiod_get(&rpisense->i2c_client->dev,
						"keys-int", GPIOD_IN);
	if (IS_ERR(rpisense_js->keys_desc)) {
		dev_warn(&pdev->dev, "Failed to get keys-int descriptor.\n");
		return PTR_ERR(rpisense_js->keys_desc);
	}


	rpisense_js->keys_dev = devm_input_allocate_device(&pdev->dev);
	if (rpisense_js->keys_dev == NULL) {
		dev_err(&pdev->dev, "Could not allocate input device.\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(keymap); i++) {
		set_bit(keymap[i],
			rpisense_js->keys_dev->keybit);
	}

	rpisense_js->keys_dev->name = "Raspberry Pi Sense HAT Joystick";
	rpisense_js->keys_dev->phys = "rpi-sense-joy/input0";
	rpisense_js->keys_dev->id.bustype = BUS_I2C;
	rpisense_js->keys_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	rpisense_js->keys_dev->keycode = keymap;
	rpisense_js->keys_dev->keycodesize = sizeof(unsigned char);
	rpisense_js->keys_dev->keycodemax = ARRAY_SIZE(keymap);

	ret = input_register_device(rpisense_js->keys_dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register input device.\n");
		return ret;
	}

	ret = gpiod_direction_input(rpisense_js->keys_desc);
	if (ret) {
		dev_err(&pdev->dev, "Could not set keys-int direction.\n");
		return ret;
	}

	rpisense_js->keys_irq = gpiod_to_irq(rpisense_js->keys_desc);
	if (rpisense_js->keys_irq < 0) {
		dev_err(&pdev->dev, "Could not determine keys-int IRQ.\n");
		return rpisense_js->keys_irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, rpisense_js->keys_irq,
		NULL, rpisense_js_report, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"keys", rpisense);

	if (ret) {
		dev_err(&pdev->dev, "IRQ request failed.\n");
		return ret;
	}
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rpisense_js_id[] = {
	{ .compatible = "rpi,rpi-sense-js" },
	{ },
};
MODULE_DEVICE_TABLE(of, rpisense_js_id);
#endif

static struct platform_device_id rpisense_js_device_id[] = {
	{ .name = "rpi-sense-js" },
	{ },
};
MODULE_DEVICE_TABLE(platform, rpisense_js_device_id);

static struct platform_driver rpisense_js_driver = {
	.probe = rpisense_js_probe,
	.driver = {
		.name = "rpi-sense-js",
	},
};

module_platform_driver(rpisense_js_driver);

MODULE_DESCRIPTION("Raspberry Pi Sense HAT joystick driver");
MODULE_AUTHOR("Serge Schneider <serge@raspberrypi.org>");
MODULE_LICENSE("GPL");
