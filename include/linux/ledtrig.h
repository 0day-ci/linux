/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LED trigger shared structures
 */

#ifndef __LINUX_LEDTRIG_H__
#define __LINUX_LEDTRIG_H__

#include <linux/atomic.h>
#include <linux/leds.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#if IS_ENABLED(CONFIG_LEDS_TRIGGER_NETDEV)

struct led_netdev_data {
	spinlock_t lock;

	struct delayed_work work;
	struct notifier_block notifier;

	struct led_classdev *led_cdev;
	struct net_device *net_dev;

	char device_name[IFNAMSIZ];
	atomic_t interval;
	unsigned int last_activity;

	unsigned long mode;
#define NETDEV_LED_LINK		0
#define NETDEV_LED_TX		1
#define NETDEV_LED_RX		2
#define NETDEV_LED_MODE_LINKUP	3
};

#endif /* IS_ENABLED(CONFIG_LEDS_TRIGGER_NETDEV) */

#endif /* __LINUX_LEDTRIG_H__ */
