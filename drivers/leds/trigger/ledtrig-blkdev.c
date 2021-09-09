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

/* Total number of device-to-LED associations */
static unsigned int ledtrig_blkdev_count;


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
 *	link_device sysfs attribute - assocate a block device with this LED
 *
 */

/* Gets or allocs & initializes the blkdev disk for a gendisk */
static int blkdev_get_disk(struct gendisk *const gd)
{
	struct ledtrig_blkdev_disk *disk;
	struct kobject *dir;

	if (gd->ledtrig != NULL) {
		kobject_get(gd->ledtrig->dir);
		return 0;
	}

	disk = kmalloc(sizeof(*disk), GFP_KERNEL);
	if (disk == NULL)
		return -ENOMEM;

	dir = kobject_create_and_add("linked_leds", &disk_to_dev(gd)->kobj);
	if (dir == NULL) {
		kfree(disk);
		return -ENOMEM;
	}

	INIT_HLIST_HEAD(&disk->leds);
	disk->gd = gd;
	disk->dir = dir;
	disk->read_ios = 0;
	disk->write_ios = 0;

	gd->ledtrig = disk;

	return 0;
}

static void blkdev_put_disk(struct ledtrig_blkdev_disk *const disk)
{
	kobject_put(disk->dir);

	if (hlist_empty(&disk->leds)) {
		disk->gd->ledtrig = NULL;
		kfree(disk);
	}
}

static int blkdev_disk_link_locked(struct ledtrig_blkdev_led *const led,
				   struct gendisk *const gd)
{
	struct ledtrig_blkdev_link *link;
	struct ledtrig_blkdev_disk *disk;
	unsigned long delay;
	int ret;

	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (link == NULL) {
		ret = -ENOMEM;
		goto error_return;
	}

	ret = blkdev_get_disk(gd);
	if (ret != 0)
		goto error_free_link;

	disk = gd->ledtrig;

	ret = sysfs_create_link(disk->dir, &led->led_dev->dev->kobj,
				led->led_dev->name);
	if (ret != 0)
		goto error_put_disk;

	ret = sysfs_create_link(led->dir, &disk_to_dev(gd)->kobj,
				gd->disk_name);
	if (ret != 0)
		goto error_remove_link;

	link->disk = disk;
	link->led = led;
	hlist_add_head(&link->led_disks_node, &led->disks);
	hlist_add_head(&link->disk_leds_node, &disk->leds);

	if (ledtrig_blkdev_count == 0) {
		delay = READ_ONCE(ledtrig_blkdev_interval);
		WARN_ON(!schedule_delayed_work(&ledtrig_blkdev_work, delay));
	}

	++ledtrig_blkdev_count;

	return 0;

error_remove_link:
	sysfs_remove_link(disk->dir, led->led_dev->name);
error_put_disk:
	blkdev_put_disk(disk);
error_free_link:
	kfree(link);
error_return:
	return ret;
}

static bool blkdev_already_linked(const struct ledtrig_blkdev_led *const led,
				  const struct gendisk *const gd)
{
	const struct ledtrig_blkdev_link *link;

	if (gd->ledtrig == NULL)
		return false;

	hlist_for_each_entry(link, &gd->ledtrig->leds, disk_leds_node) {

		if (link->led == led)
			return true;
	}

	return false;
}

static ssize_t link_device_store(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count)
{
	struct ledtrig_blkdev_led *const led = led_trigger_get_drvdata(dev);
	struct gendisk *gd;
	int ret;

	ret = mutex_lock_interruptible(&ledtrig_blkdev_mutex);
	if (ret != 0)
		goto exit_return;

	gd = ledtrig_blkdev_get_disk(buf);
	if (gd == NULL) {
		ret = -ENODEV;
		goto exit_unlock;
	}

	if (blkdev_already_linked(led, gd)) {
		ret = -EEXIST;
		goto exit_put_disk;
	}

	ret = blkdev_disk_link_locked(led, gd);

exit_put_disk:
	if (ret != 0)
		put_disk(gd);
exit_unlock:
	mutex_unlock(&ledtrig_blkdev_mutex);
exit_return:
	return (ret == 0) ? count : ret;
}

static DEVICE_ATTR_WO(link_device);


/*
 *
 *	unlink_device sysfs attribute - disassociate a device from this LED
 *
 */

static void blkdev_disk_unlink_locked(struct ledtrig_blkdev_led *const led,
				      struct ledtrig_blkdev_link *const link,
				      struct ledtrig_blkdev_disk *const disk)
{
	--ledtrig_blkdev_count;

	if (ledtrig_blkdev_count == 0)
		WARN_ON(!cancel_delayed_work_sync(&ledtrig_blkdev_work));

	sysfs_remove_link(led->dir, disk->gd->disk_name);
	sysfs_remove_link(disk->dir, led->led_dev->name);
	kobject_put(disk->dir);

	hlist_del(&link->led_disks_node);
	hlist_del(&link->disk_leds_node);
	kfree(link);

	if (hlist_empty(&disk->leds)) {
		disk->gd->ledtrig = NULL;
		kfree(disk);
	}

	put_disk(disk->gd);
}

static ssize_t unlink_device_store(struct device *const dev,
				   struct device_attribute *const attr,
				   const char *const buf, const size_t count)
{
	struct ledtrig_blkdev_led *const led = led_trigger_get_drvdata(dev);
	struct ledtrig_blkdev_link *link;

	mutex_lock(&ledtrig_blkdev_mutex);

	hlist_for_each_entry(link, &led->disks, led_disks_node) {

		if (sysfs_streq(link->disk->gd->disk_name, buf)) {
			blkdev_disk_unlink_locked(led, link, link->disk);
			break;
		}
	}

	mutex_unlock(&ledtrig_blkdev_mutex);

	return count;
}

static DEVICE_ATTR_WO(unlink_device);


/*
 *
 *	Initialization - register the trigger
 *
 */

static struct attribute *ledtrig_blkdev_attrs[] = {
	&dev_attr_link_device.attr,
	&dev_attr_unlink_device.attr,
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
