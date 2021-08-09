// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/leds.h>
#include <linux/mutex.h>

/*
 *
 *	Trigger mutex and LED list
 *
 */

// Must hold when doing anything with LED/trigger/block device
// associations
static DEFINE_MUTEX(blk_ledtrig_mutex);

static LIST_HEAD(blk_ledtrig_leds);

// Every LED associated with the blkdev trigger gets one of these
struct blk_ledtrig_led {
	struct kobject		*dir;		// block_devices subdirectory
	struct led_classdev	*led;
	unsigned int		blink_on;
	unsigned int		blink_off;
	struct list_head	leds_list_node;
	struct list_head	dev_list;
};

// Caller must hold blk_ledtrig_mutex
static struct blk_ledtrig_led *blk_ledtrig_find(const char *const led_name,
						const size_t name_len)
{
	struct blk_ledtrig_led *bd_led;

	list_for_each_entry(bd_led, &blk_ledtrig_leds, leds_list_node) {
		if (strlen(bd_led->led->name) != name_len)
			continue;
		if (memcmp(bd_led->led->name, led_name, name_len) == 0)
			return bd_led;
	}

	return NULL;
}
