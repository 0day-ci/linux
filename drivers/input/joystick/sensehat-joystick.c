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
#include <linux/regmap.h>

#include "sensehat.h"

int sensehat_get_joystick_state(struct sensehat *sensehat);

static unsigned char keymap[] = {KEY_DOWN, KEY_RIGHT, KEY_UP, KEY_ENTER, KEY_LEFT,};

static irqreturn_t sensehat_joystick_report(int n, void *cookie)
{
	int i;
	static s32 prev_keys;
	struct sensehat *sensehat = cookie;
	struct sensehat_joystick *sensehat_joystick = &sensehat->joystick;
	s32 keys = sensehat_get_joystick_state(sensehat);
	s32 changes = keys ^ prev_keys;

	prev_keys = keys;
	for (i = 0; i < ARRAY_SIZE(keymap); ++i) {
		if (changes & (1<<i)) {
			input_report_key(sensehat_joystick->keys_dev,
					 keymap[i], keys & (1<<i));
		}
	}
	input_sync(sensehat_joystick->keys_dev);
	return IRQ_HANDLED;
}

static int sensehat_joystick_probe(struct platform_device *pdev)
{
	int ret;
	int i;
	struct sensehat *sensehat = dev_get_drvdata(&pdev->dev);
	struct sensehat_joystick *sensehat_joystick = &sensehat->joystick;

	sensehat_joystick->keys_desc = devm_gpiod_get(&sensehat->i2c_client->dev,
						"keys-int", GPIOD_IN);
	if (IS_ERR(sensehat_joystick->keys_desc)) {
		dev_warn(&pdev->dev, "Failed to get keys-int descriptor.\n");
		return PTR_ERR(sensehat_joystick->keys_desc);
	}


	sensehat_joystick->keys_dev = devm_input_allocate_device(&pdev->dev);
	if (!sensehat_joystick->keys_dev) {
		dev_err(&pdev->dev, "Could not allocate input device.\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(keymap); i++) {
		set_bit(keymap[i],
			sensehat_joystick->keys_dev->keybit);
	}

	sensehat_joystick->keys_dev->name = "Raspberry Pi Sense HAT Joystick";
	sensehat_joystick->keys_dev->phys = "rpi-sense-joy/input0";
	sensehat_joystick->keys_dev->id.bustype = BUS_I2C;
	sensehat_joystick->keys_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	sensehat_joystick->keys_dev->keycode = keymap;
	sensehat_joystick->keys_dev->keycodesize = sizeof(unsigned char);
	sensehat_joystick->keys_dev->keycodemax = ARRAY_SIZE(keymap);

	ret = input_register_device(sensehat_joystick->keys_dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register input device.\n");
		return ret;
	}

	ret = gpiod_direction_input(sensehat_joystick->keys_desc);
	if (ret) {
		dev_err(&pdev->dev, "Could not set keys-int direction.\n");
		return ret;
	}

	sensehat_joystick->keys_irq = gpiod_to_irq(sensehat_joystick->keys_desc);
	if (sensehat_joystick->keys_irq < 0) {
		dev_err(&pdev->dev, "Could not determine keys-int IRQ.\n");
		return sensehat_joystick->keys_irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, sensehat_joystick->keys_irq,
		NULL, sensehat_joystick_report, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"keys", sensehat);

	if (ret) {
		dev_err(&pdev->dev, "IRQ request failed.\n");
		return ret;
	}
	return 0;
}

int sensehat_get_joystick_state(struct sensehat *sensehat)
{
	unsigned int reg;
	int ret = regmap_read(sensehat->regmap, SENSEHAT_KEYS, &reg);

	return ret < 0 ? ret : reg;
}

static struct platform_device_id sensehat_joystick_device_id[] = {
	{ .name = "sensehat-joystick" },
	{ },
};
MODULE_DEVICE_TABLE(platform, sensehat_joystick_device_id);

static struct platform_driver sensehat_joystick_driver = {
	.probe = sensehat_joystick_probe,
	.driver = {
		.name = "sensehat-joystick",
	},
};

module_platform_driver(sensehat_joystick_driver);

MODULE_DESCRIPTION("Raspberry Pi Sense HAT joystick driver");
MODULE_AUTHOR("Serge Schneider <serge@raspberrypi.org>");
MODULE_LICENSE("GPL");
