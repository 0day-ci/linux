// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel Panic LED Trigger
 *
 * Copyright 2016 Ezequiel Garcia <ezequiel@vanguardiasur.com.ar>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/leds.h>
#include "../leds.h"

enum led_display_type {
	ON,
	OFF,
	BLINK,
	DISPLAY_TYPE_COUNT,
};

static struct led_trigger *panic_trigger[DISPLAY_TYPE_COUNT];

/*
 * This is called in a special context by the atomic panic
 * notifier. This means the trigger can be changed without
 * worrying about locking.
 */
static void led_trigger_set_panic(struct led_classdev *led_cdev, const char *type)
{
	struct led_trigger *trig;

	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (strcmp(type, trig->name))
			continue;
		if (led_cdev->trigger)
			list_del(&led_cdev->trig_list);
		list_add_tail(&led_cdev->trig_list, &trig->led_cdevs);

		/* Avoid the delayed blink path */
		led_cdev->blink_delay_on = 0;
		led_cdev->blink_delay_off = 0;

		led_cdev->trigger = trig;
		if (trig->activate)
			trig->activate(led_cdev);

		/*Clear current brightness work*/
		led_cdev->work_flags = 0;

		break;
	}
}

static int led_trigger_panic_notifier(struct notifier_block *nb,
				      unsigned long code, void *unused)
{
	struct led_classdev *led_cdev;

	list_for_each_entry(led_cdev, &leds_list, node)
		if (led_cdev->flags & LED_PANIC_INDICATOR)
			led_trigger_set_panic(led_cdev, "panic");
		else if (led_cdev->flags & LED_PANIC_INDICATOR_ON)
			led_trigger_set_panic(led_cdev, "panic_on");
		else if (led_cdev->flags & LED_PANIC_INDICATOR_OFF)
			led_trigger_set_panic(led_cdev, "panic_off");

	return NOTIFY_DONE;
}

static struct notifier_block led_trigger_panic_nb = {
	.notifier_call = led_trigger_panic_notifier,
};

static long led_panic_activity(int state)
{
	led_trigger_event(panic_trigger[BLINK], state ? LED_FULL : LED_OFF);
	led_trigger_event(panic_trigger[ON], LED_FULL);
	led_trigger_event(panic_trigger[OFF], LED_OFF);

	return 0;
}

static int __init ledtrig_panic_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &led_trigger_panic_nb);

	led_trigger_register_simple("panic", &panic_trigger[BLINK]);
	led_trigger_register_simple("panic_on", &panic_trigger[ON]);
	led_trigger_register_simple("panic_off", &panic_trigger[OFF]);

	panic_blink = led_panic_activity;

	return 0;
}
device_initcall(ledtrig_panic_init);
