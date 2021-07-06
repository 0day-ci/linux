// SPDX-License-Identifier: GPL-2.0+

/*
 * Cisco Meraki MX100 (Tinkerbell) board platform driver
 *
 * Based off of arch/x86/platform/meraki/tink.c from the
 * Meraki GPL release meraki-firmware-sources-r23-20150601
 *
 * Format inspired by platform/x86/pcengines-apuv2.c
 *
 * Copyright (C) 2021 Chris Blake <chrisrblake93@gmail.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define TINK_GPIO_OFFSET	436

/* LEDs */
static const struct gpio_led tink_leds[] = {
	{
		.name = "mx100:green:internet", 
		.gpio = TINK_GPIO_OFFSET + 11,
		.active_low = 1,
		.default_trigger = "default-on",
	},
	{
		.name = "mx100:green:lan2",
		.gpio = TINK_GPIO_OFFSET + 18,
	},
	{
		.name = "mx100:green:lan3",
		.gpio = TINK_GPIO_OFFSET + 20,
	},
	{
		.name = "mx100:green:lan4",
		.gpio = TINK_GPIO_OFFSET + 22,
	},
	{
		.name = "mx100:green:lan5",
		.gpio = TINK_GPIO_OFFSET + 23,
	},
	{
		.name = "mx100:green:lan6",
		.gpio = TINK_GPIO_OFFSET + 32,
	},
	{
		.name = "mx100:green:lan7",
		.gpio = TINK_GPIO_OFFSET + 34,
	},
	{
		.name = "mx100:green:lan8",
		.gpio = TINK_GPIO_OFFSET + 35,
	},
	{
		.name = "mx100:green:lan9",
		.gpio = TINK_GPIO_OFFSET + 36,
	},
	{
		.name = "mx100:green:lan10",
		.gpio = TINK_GPIO_OFFSET + 37,
	},
	{
		.name = "mx100:green:lan11",
		.gpio = TINK_GPIO_OFFSET + 48,
	},
	{
		.name = "mx100:green:ha",
		.gpio = TINK_GPIO_OFFSET + 16,
		.active_low = 1,
	},
	{
		.name = "mx100:orange:ha",
		.gpio = TINK_GPIO_OFFSET + 7,
		.active_low = 1,
	},
	{
		.name = "mx100:green:usb",
		.gpio = TINK_GPIO_OFFSET + 21,
		.active_low = 1,
	},
	{
		.name = "mx100:orange:usb",
		.gpio = TINK_GPIO_OFFSET + 19,
		.active_low = 1,
	},
};

static const struct gpio_led_platform_data tink_leds_pdata = {
	.num_leds	= ARRAY_SIZE(tink_leds),
	.leds		= tink_leds,
};

/* Reset Button */
static struct gpio_keys_button tink_buttons[] = {
	{
		.desc			= "Reset",
		.type			= EV_KEY,
		.code			= KEY_RESTART,
		.gpio			= TINK_GPIO_OFFSET + 60,
		.active_low             = 1,
		.debounce_interval      = 100,
	},
};

static const struct gpio_keys_platform_data tink_buttons_pdata = {
	.buttons	= tink_buttons,
	.nbuttons	= ARRAY_SIZE(tink_buttons),
	.poll_interval  = 20,
	.rep		= 0,
	.name		= "mx100-keys",
};

/* Board setup */

static struct platform_device *tink_leds_pdev;
static struct platform_device *tink_keys_pdev;

static struct platform_device * __init tink_create_dev(
	const char *name,
	const void *pdata,
	size_t sz)
{
	struct platform_device *pdev;

	pdev = platform_device_register_data(NULL,
		name,
		PLATFORM_DEVID_NONE,
		pdata,
		sz);

	if (IS_ERR(pdev))
		pr_err("failed registering %s: %ld\n", name, PTR_ERR(pdev));

	return pdev;
}

static int __init tink_board_init(void)
{
	if (!dmi_match(DMI_SYS_VENDOR, "Cisco") || !dmi_match(DMI_PRODUCT_NAME, "MX100-HW")) {
		return -ENODEV;
	}

	/* We need to make sure that GPIO60 isn't set to native mode as is default since it's our 
	 * Reset Button. To do this, write to GPIO_USE_SEL2 to have GPIO60 set to GPIO mode.
	 * This is documented on page 1609 of the PCH datasheet, order number 327879-005US
	 */
	outl(inl(0x530) | BIT(28), 0x530);

	tink_leds_pdev = tink_create_dev(
		"leds-gpio",
		&tink_leds_pdata,
		sizeof(tink_leds_pdata));

	tink_keys_pdev = tink_create_dev(
		"gpio-keys-polled",
		&tink_buttons_pdata,
		sizeof(tink_buttons_pdata));

	return 0;
}

static void __exit tink_board_exit(void)
{
	platform_device_unregister(tink_keys_pdev);
	platform_device_unregister(tink_leds_pdev);
}

module_init(tink_board_init);
module_exit(tink_board_exit);

MODULE_AUTHOR("Chris Blake <chrisrblake93@gmail.com>");
MODULE_DESCRIPTION("Cisco Meraki MX100 Platform Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:meraki-mx100");
MODULE_SOFTDEP("pre: platform:gpio_ich platform:leds-gpio platform:gpio_keys_polled");
