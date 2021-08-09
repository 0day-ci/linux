// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/genhd.h>
#include <linux/leds.h>
#include <linux/mutex.h>

#include "blk-ledtrig.h"


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


/*
 *
 *	Clear a block device's LED
 *
 */

// Also called from blk_ledtrig_dev_set()
static void blk_ledtrig_dev_cleanup(struct gendisk *const disk,
				    struct blk_ledtrig_led *const old_led)
{
	sysfs_remove_link(old_led->dir, disk->disk_name);
	list_del(&disk->led_dev_list_node);
}

// Also called from blk_ledtrig_deactivate()
static void blk_ledtrig_dev_clear_locked(struct gendisk *const disk,
					 struct blk_ledtrig_led *const old_led)
{
	RCU_INIT_POINTER(disk->led, NULL);
	if (old_led != NULL)
		blk_ledtrig_dev_cleanup(disk, old_led);
}

// Also called from genhd.c:del_gendisk()
void blk_ledtrig_dev_clear(struct gendisk *const disk)
{
	struct blk_ledtrig_led *old_led;

	mutex_lock(&blk_ledtrig_mutex);
	old_led = rcu_dereference_protected(disk->led,
					lockdep_is_held(&blk_ledtrig_mutex));
	blk_ledtrig_dev_clear_locked(disk, old_led);
	mutex_unlock(&blk_ledtrig_mutex);
}


/*
 *
 *	Set a block device's LED
 *
 */

static int blk_ledtrig_dev_set(struct gendisk *const disk,
			       const char *const led_name,
			       const size_t name_len)
{
	struct blk_ledtrig_led *new_led, *old_led;
	int ret;

	ret = mutex_lock_interruptible(&blk_ledtrig_mutex);
	if (ret != 0)
		goto led_set_exit_return;

	new_led = blk_ledtrig_find(led_name, name_len);
	if (new_led == NULL) {
		pr_info("no LED named %.*s associated with blkdev trigger\n",
			(int)name_len, led_name);
		ret = -ENODEV;
		goto led_set_exit_unlock;
	}

	old_led = rcu_dereference_protected(disk->led,
					lockdep_is_held(&blk_ledtrig_mutex));

	if (old_led == new_led) {
		ret = 0;
		goto led_set_exit_unlock;
	}

	ret = sysfs_create_link(new_led->dir, &disk_to_dev(disk)->kobj,
				disk->disk_name);
	if (ret != 0)
		goto led_set_exit_unlock;

	if (old_led != NULL)
		blk_ledtrig_dev_cleanup(disk, old_led);

	rcu_assign_pointer(disk->led, new_led);
	list_add(&disk->led_dev_list_node, &new_led->dev_list);

	ret = 0;

led_set_exit_unlock:
	mutex_unlock(&blk_ledtrig_mutex);
led_set_exit_return:
	return ret;
}
