// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED trigger - modular components
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/part_stat.h>

#include "ledtrig-blkdev.h"

MODULE_DESCRIPTION("Block device LED trigger");
MODULE_AUTHOR("Ian Pilcher <arequipeno@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(LEDTRIG_BLKDEV);

/* Default blink time & check interval (milliseconds) */
#define LEDTRIG_BLKDEV_BLINK_MSEC	75
#define LEDTRIG_BLKDEV_INTERVAL		100

/* Minimum blink time & check interval (milliseconds) */
#define LEDTRIG_BLKDEV_MIN_BLINK	10
#define LEDTRIG_BLKDEV_MIN_INT		25

enum ledtrig_blkdev_mode {
	LEDTRIG_BLKDEV_MODE_RO	= 0,	/* blink for reads */
	LEDTRIG_BLKDEV_MODE_WO	= 1,	/* blink for writes */
	LEDTRIG_BLKDEV_MODE_RW	= 2	/* blink for reads and writes */
};

/* Per-block device information */
struct ledtrig_blkdev_disk {
	struct gendisk		*gd;
	struct kobject		*dir;		/* linked_leds dir */
	struct hlist_head	leds;
	unsigned long		read_ios;
	unsigned long		write_ios;
	unsigned int		generation;
	bool			read_act;
	bool			write_act;
};

/* For many-to-many relationships between "disks" (block devices) and LEDs */
struct ledtrig_blkdev_link {
	struct hlist_node		disk_leds_node;
	struct hlist_node		led_disks_node;
	struct ledtrig_blkdev_disk	*disk;
	struct ledtrig_blkdev_led	*led;
};

/* Every LED associated with the blkdev trigger gets one of these */
struct ledtrig_blkdev_led {
	struct kobject			*dir;		/* linked_devices dir */
	struct led_classdev		*led_dev;
	unsigned int			blink_msec;
	struct hlist_head		disks;		/* linked block devs */
	struct hlist_node		leds_node;
	enum ledtrig_blkdev_mode	mode;
};

/* All LEDs associated with the trigger */
static HLIST_HEAD(ledtrig_blkdev_leds);

/* How often to check for drive activity - in jiffies */
static unsigned int ledtrig_blkdev_interval;

/* Delayed work used to periodically check for activity & blink LEDs */
static void blkdev_process(struct work_struct *const work);
static DECLARE_DELAYED_WORK(ledtrig_blkdev_work, blkdev_process);


/*
 *
 *	Periodically check for device acitivity and blink LEDs
 *
 */

static void blkdev_blink(const struct ledtrig_blkdev_led *const led)
{
	unsigned long delay_on = READ_ONCE(led->blink_msec);
	unsigned long delay_off = 1;	/* 0 leaves LED turned on */

	led_blink_set_oneshot(led->led_dev, &delay_on, &delay_off, 0);
}

static void blkdev_update_disk(struct ledtrig_blkdev_disk *const disk,
			       const unsigned int generation)
{
	const struct block_device *const part0 = disk->gd->part0;
	const unsigned long read_ios = part_stat_read(part0, ios[STAT_READ]);
	const unsigned long write_ios = part_stat_read(part0, ios[STAT_WRITE])
				+ part_stat_read(part0, ios[STAT_DISCARD])
				+ part_stat_read(part0, ios[STAT_FLUSH]);

	if (disk->read_ios != read_ios) {
		disk->read_act = true;
		disk->read_ios = read_ios;
	} else {
		disk->read_act = false;
	}

	if (disk->write_ios != write_ios) {
		disk->write_act = true;
		disk->write_ios = write_ios;
	} else {
		disk->write_act = false;
	}

	disk->generation = generation;
}

static bool blkdev_read_mode(const enum ledtrig_blkdev_mode mode)
{
	return mode != LEDTRIG_BLKDEV_MODE_WO;
}

static bool blkdev_write_mode(const enum ledtrig_blkdev_mode mode)
{
	return mode != LEDTRIG_BLKDEV_MODE_RO;
}

static void blkdev_process(struct work_struct *const work)
{
	static unsigned int generation;

	struct ledtrig_blkdev_led *led;
	struct ledtrig_blkdev_link *link;
	unsigned long delay;

	if (!mutex_trylock(&ledtrig_blkdev_mutex))
		goto exit_reschedule;

	hlist_for_each_entry(led, &ledtrig_blkdev_leds, leds_node) {

		hlist_for_each_entry(link, &led->disks, led_disks_node) {

			struct ledtrig_blkdev_disk *const disk = link->disk;

			if (disk->generation != generation)
				blkdev_update_disk(disk, generation);

			if (disk->read_act && blkdev_read_mode(led->mode)) {
				blkdev_blink(led);
				break;
			}

			if (disk->write_act && blkdev_write_mode(led->mode)) {
				blkdev_blink(led);
				break;
			}
		}
	}

	++generation;

	mutex_unlock(&ledtrig_blkdev_mutex);

exit_reschedule:
	delay = READ_ONCE(ledtrig_blkdev_interval);
	WARN_ON_ONCE(!schedule_delayed_work(&ledtrig_blkdev_work, delay));
}


/*
 *
 *	Associate an LED with the blkdev trigger
 *
 */

static int blkdev_activate(struct led_classdev *const led_dev)
{
	struct ledtrig_blkdev_led *led;
	int ret;

	/* Don't allow module to be removed while any LEDs are linked */
	if (WARN_ON(!try_module_get(THIS_MODULE))) {
		ret = -ENODEV;		/* Shouldn't ever happen */
		goto exit_return;
	}

	led = kmalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL) {
		ret = -ENOMEM;
		goto exit_put_module;
	}

	led->led_dev = led_dev;
	led->blink_msec = LEDTRIG_BLKDEV_BLINK_MSEC;
	led->mode = LEDTRIG_BLKDEV_MODE_RW;
	INIT_HLIST_HEAD(&led->disks);

	ret = mutex_lock_interruptible(&ledtrig_blkdev_mutex);
	if (ret != 0)
		goto exit_free;

	led->dir = kobject_create_and_add("linked_devices",
					  &led_dev->dev->kobj);
	if (led->dir == NULL) {
		ret = -ENOMEM;
		goto exit_unlock;
	}

	hlist_add_head(&led->leds_node, &ledtrig_blkdev_leds);
	led_set_trigger_data(led_dev, led);
	ret = 0;

exit_unlock:
	mutex_unlock(&ledtrig_blkdev_mutex);
exit_free:
	if (ret != 0)
		kfree(led);
exit_put_module:
	if (ret != 0)
		module_put(THIS_MODULE);
exit_return:
	return ret;
}


/*
 *
 *	Initialization - register the trigger
 *
 */

static struct attribute *ledtrig_blkdev_attrs[] = {
	NULL
};

static const struct attribute_group ledtrig_blkdev_attr_group = {
	.attrs	= ledtrig_blkdev_attrs,
};

static const struct attribute_group *ledtrig_blkdev_attr_groups[] = {
	&ledtrig_blkdev_attr_group,
	NULL
};

struct led_trigger ledtrig_blkdev_trigger = {
	.name		= "blkdev",
	.activate	= blkdev_activate,
	.groups		= ledtrig_blkdev_attr_groups,
};
