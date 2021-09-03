// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers - modular components
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/part_stat.h>

#include "ledtrig-blkdev.h"

MODULE_DESCRIPTION("Block device LED trigger");
MODULE_AUTHOR("Ian Pilcher <arequipeno@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(LEDTRIG_BLKDEV);

/* Default blink time & polling interval (milliseconds) */
#define LEDTRIG_BLKDEV_BLINK_MSEC	75
#define LEDTRIG_BLKDEV_INTERVAL		100

/* Minimum VALUE for interval or blink_time */
#define LEDTRIG_BLKDEV_MIN_TIME		25

enum ledtrig_blkdev_mode {
	LEDTRIG_BLKDEV_MODE_RO	= 0,	/* blink for reads */
	LEDTRIG_BLKDEV_MODE_WO	= 1,	/* blink for writes */
	LEDTRIG_BLKDEV_MODE_RW	= 2	/* blink for reads and writes */
};

/* Trigger-specific info about a block device */
struct ledtrig_blkdev_disk {
	struct gendisk		*gd;
	struct kobject		*dir;		/* blkdev_leds dir */
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
	struct kobject			*dir;		/* block_devices dir */
	struct led_classdev		*led_dev;
	unsigned int			blink_msec;
	struct hlist_head		disks;		/* linked block devs */
	struct hlist_node		leds_node;
	enum ledtrig_blkdev_mode	mode;
};

/* All LEDs associated with the trigger */
static HLIST_HEAD(ledtrig_blkdev_leds);

/* Total number of device-to-LED associations */
static unsigned int ledtrig_blkdev_count;

/* How often to check for drive activity - in jiffies */
static unsigned int ledtrig_blkdev_interval;

/* Delayed work used to periodically check for activity & blink LEDs */
static void blkdev_process(struct work_struct *const work);
static DECLARE_DELAYED_WORK(ledtrig_blkdev_work, blkdev_process);


/*
 *
 *	Miscellaneous helper functions
 *
 */

/*
 * Returns a pointer to the first non-whitespace character in s
 * (or a pointer to the terminating null).
 */
static const char *blkdev_skip_space(const char *s)
{
	while (*s != 0 && isspace(*s))
		++s;

	return s;
}

/*
 * Returns a pointer to the first whitespace character in s (or a pointer to the
 * terminating null), which is effectively a pointer to the position *after* the
 * last character in the non-whitespace token at the beginning of s.  (s is
 * expected to be the result of a previous call to blkdev_skip_space()).
 */
static const char *blkdev_find_space(const char *s)
{
	while (*s != 0 && !isspace(*s))
		++s;

	return s;
}

static bool blkdev_read_mode(const enum ledtrig_blkdev_mode mode)
{
	return mode != LEDTRIG_BLKDEV_MODE_WO;
}

static bool blkdev_write_mode(const enum ledtrig_blkdev_mode mode)
{
	return mode != LEDTRIG_BLKDEV_MODE_RO;
}


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

	if (WARN_ON(!try_module_get(THIS_MODULE))) {
		ret = -ENODEV;		/* -ESHOULDNEVERHAPPEN */
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

	led->dir = kobject_create_and_add("block_devices", &led_dev->dev->kobj);
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
 *	Associate a block device with an LED
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

	dir = kobject_create_and_add("blkdev_leds", &disk_to_dev(gd)->kobj);
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

static int blkdev_disk_add_locked(struct ledtrig_blkdev_led *const led,
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

		if (link->led == led) {
			pr_info("blkdev LED: %s already associated with %s\n",
				gd->disk_name, led->led_dev->name);
			return true;
		}
	}

	return false;
}

static int blkdev_disk_add(struct ledtrig_blkdev_led *const led,
			   const char *const disk_name, const size_t name_len)
{
	struct gendisk *gd;
	int ret;

	ret = mutex_lock_interruptible(&ledtrig_blkdev_mutex);
	if (ret != 0)
		goto exit_return;

	gd = ledtrig_blkdev_get_disk(disk_name, name_len);
	if (gd == NULL) {
		pr_info("blkdev LED: no such block device %.*s\n",
			(int)name_len, disk_name);
		ret = -ENODEV;
		goto exit_unlock;
	}

	if (blkdev_already_linked(led, gd)) {
		ret = -EEXIST;
		goto exit_put_disk;
	}

	ret = blkdev_disk_add_locked(led, gd);

exit_put_disk:
	if (ret != 0)
		put_disk(gd);
exit_unlock:
	mutex_unlock(&ledtrig_blkdev_mutex);
exit_return:
	return ret;
}


/*
 *
 *	Disassociate a block device from an LED
 *
 */

static void blkdev_disk_del_locked(struct ledtrig_blkdev_led *const led,
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

static void blkdev_disk_delete(struct ledtrig_blkdev_led *const led,
			       const char *const disk_name,
			       const size_t name_len)
{
	struct ledtrig_blkdev_link *link;

	mutex_lock(&ledtrig_blkdev_mutex);

	hlist_for_each_entry(link, &led->disks, led_disks_node) {

		if (ledtrig_blkdev_streq(link->disk->gd->disk_name,
						disk_name, name_len)) {
			blkdev_disk_del_locked(led, link, link->disk);
			goto exit_unlock;
		}
	}

	pr_info("blkdev LED: %.*s not associated with LED %s\n",
		(int)name_len, disk_name, led->led_dev->name);

exit_unlock:
	mutex_unlock(&ledtrig_blkdev_mutex);
}


/*
 *
 *	Disassociate all LEDs from a block device (because it's going away)
 *
 */

static void blkdev_disk_cleanup(struct gendisk *const gd)
{
	struct ledtrig_blkdev_link *link;
	struct hlist_node *next;

	hlist_for_each_entry_safe(link, next,
				  &gd->ledtrig->leds, disk_leds_node)
		blkdev_disk_del_locked(link->led, link, gd->ledtrig);
}


/*
 *
 *	Disassociate an LED from the trigger
 *
 */

static void blkdev_deactivate(struct led_classdev *const led_dev)
{
	struct ledtrig_blkdev_led *const led = led_get_trigger_data(led_dev);
	struct ledtrig_blkdev_link *link;
	struct hlist_node *next;

	mutex_lock(&ledtrig_blkdev_mutex);

	hlist_for_each_entry_safe(link, next, &led->disks, led_disks_node)
		blkdev_disk_del_locked(led, link, link->disk);

	hlist_del(&led->leds_node);
	kobject_put(led->dir);
	kfree(led);

	mutex_unlock(&ledtrig_blkdev_mutex);

	module_put(THIS_MODULE);
}


/*
 *
 *	sysfs attributes to add & delete devices from LEDs
 *
 */

static ssize_t blkdev_add_or_del(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count);

static struct device_attribute ledtrig_blkdev_attr_add =
	__ATTR(add_blkdev, 0200, NULL, blkdev_add_or_del);

static struct device_attribute ledtrig_blkdev_attr_del =
	__ATTR(delete_blkdev, 0200, NULL, blkdev_add_or_del);

static ssize_t blkdev_add_or_del(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count)
{
	struct ledtrig_blkdev_led *const led = led_trigger_get_drvdata(dev);
	const char *const disk_name = blkdev_skip_space(buf);
	const char *const endp = blkdev_find_space(disk_name);
	const ptrdiff_t name_len = endp - disk_name;	/* always >= 0 */
	int ret;

	if (name_len == 0) {
		pr_info("blkdev LED: empty block device name\n");
		return -EINVAL;
	}

	if (attr == &ledtrig_blkdev_attr_del) {
		blkdev_disk_delete(led, disk_name, name_len);
	} else {	/* attr == &ledtrig_blkdev_attr_add */
		ret = blkdev_disk_add(led, disk_name, name_len);
		if (ret != 0)
			return ret;
	}

	/*
	 * Consume everything up to the next non-whitespace token (or the end
	 * of the input).  Avoids "empty block device name" error if there is
	 * whitespace (such as a newline) after the last token.
	 */
	return blkdev_skip_space(endp) - buf;
}


/*
 *
 *	blink_time & interval device attributes
 *
 */

static ssize_t blkdev_time_show(struct device *const dev,
				      struct device_attribute *const attr,
				      char *const buf);

static ssize_t blkdev_time_store(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count);

static struct device_attribute ledtrig_blkdev_attr_blink_time =
	__ATTR(blink_time, 0644, blkdev_time_show, blkdev_time_store);

static struct device_attribute ledtrig_blkdev_attr_interval =
	__ATTR(interval, 0644, blkdev_time_show, blkdev_time_store);

static ssize_t blkdev_time_show(struct device *const dev,
				struct device_attribute *const attr,
				char *const buf)
{
	const struct ledtrig_blkdev_led *const led =
						led_trigger_get_drvdata(dev);
	unsigned int value;

	if (attr == &ledtrig_blkdev_attr_blink_time)
		value = READ_ONCE(led->blink_msec);
	else	// attr == &ledtrig_blkdev_attr_interval
		value = jiffies_to_msecs(READ_ONCE(ledtrig_blkdev_interval));

	return sprintf(buf, "%u\n", value);
}

static ssize_t blkdev_time_store(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count)
{
	struct ledtrig_blkdev_led *const led = led_trigger_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret != 0)
		return ret;

	if (value < LEDTRIG_BLKDEV_MIN_TIME) {
		pr_info("blkdev LED: attempt to set time < %s milliseconds\n",
			__stringify(LEDTRIG_BLKDEV_MIN_TIME));
		return -ERANGE;
	}

	if (attr == &ledtrig_blkdev_attr_blink_time)
		WRITE_ONCE(led->blink_msec, value);
	else	// attr == &ledtrig_blkdev_attr_interval
		WRITE_ONCE(ledtrig_blkdev_interval, msecs_to_jiffies(value));

	return count;
}


/*
 *
 *	LED mode device attribute
 *
 */

static const struct {
	const char	*name;
	const char	*show;
} blkdev_modes[] = {
	[LEDTRIG_BLKDEV_MODE_RO] = {
		.name	= "read",
		.show	= "[read] write rw\n",
	},
	[LEDTRIG_BLKDEV_MODE_WO] = {
		.name	= "write",
		.show	= "read [write] rw\n",
	},
	[LEDTRIG_BLKDEV_MODE_RW] = {
		.name	= "rw",
		.show	= "read write [rw]\n",
	},
};

static ssize_t blkdev_mode_show(struct device *const dev,
				struct device_attribute *const attr,
				char *const buf)
{
	const struct ledtrig_blkdev_led *const led =
					led_trigger_get_drvdata(dev);

	return sprintf(buf, blkdev_modes[READ_ONCE(led->mode)].show);
}

static ssize_t blkdev_mode_store(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count)
{
	struct ledtrig_blkdev_led *const led = led_trigger_get_drvdata(dev);
	const char *const mode_name = blkdev_skip_space(buf);
	const char *const endp = blkdev_find_space(mode_name);
	const ptrdiff_t name_len = endp - mode_name;	/* always >= 0 */
	enum ledtrig_blkdev_mode mode;

	if (name_len == 0) {
		pr_info("blkdev LED: empty mode\n");
		return -EINVAL;
	}

	for (mode = LEDTRIG_BLKDEV_MODE_RO;
				mode <= LEDTRIG_BLKDEV_MODE_RW; ++mode) {

		if (ledtrig_blkdev_streq(blkdev_modes[mode].name,
						mode_name, name_len)) {
			WRITE_ONCE(led->mode, mode);
			return count;
		}
	}

	pr_info("blkdev LED: invalid mode (%.*s)\n", (int)name_len, mode_name);
	return -EINVAL;
}

static struct device_attribute ledtrig_blkdev_attr_mode =
	__ATTR(mode, 0644, blkdev_mode_show, blkdev_mode_store);


/*
 *
 *	Initialization - register the trigger
 *
 */

static struct attribute *ledtrig_blkdev_attrs[] = {
	&ledtrig_blkdev_attr_add.attr,
	&ledtrig_blkdev_attr_del.attr,
	&ledtrig_blkdev_attr_blink_time.attr,
	&ledtrig_blkdev_attr_interval.attr,
	&ledtrig_blkdev_attr_mode.attr,
	NULL
};

static const struct attribute_group ledtrig_blkdev_attr_group = {
	.attrs	= ledtrig_blkdev_attrs,
};

static const struct attribute_group *ledtrig_blkdev_attr_groups[] = {
	&ledtrig_blkdev_attr_group,
	NULL
};

static struct led_trigger ledtrig_blkdev_trigger = {
	.name		= "blkdev",
	.activate	= blkdev_activate,
	.deactivate	= blkdev_deactivate,
	.groups		= ledtrig_blkdev_attr_groups,
};

static int __init blkdev_init(void)
{
	int ret;

	ret = mutex_lock_interruptible(&ledtrig_blkdev_mutex);
	if (ret != 0)
		return ret;

	ledtrig_blkdev_interval = msecs_to_jiffies(LEDTRIG_BLKDEV_INTERVAL);
	__ledtrig_blkdev_disk_cleanup = blkdev_disk_cleanup;

	/*
	 * Can't call led_trigger_register() with ledtrig_blkdev_mutex locked.
	 * If an LED has blkdev as its default_trigger, blkdev_activate() will
	 * be called for that LED, and it will try to lock the mutex, which will
	 * hang.
	 */
	mutex_unlock(&ledtrig_blkdev_mutex);

	ret = led_trigger_register(&ledtrig_blkdev_trigger);
	if (ret != 0) {
		mutex_lock(&ledtrig_blkdev_mutex);
		__ledtrig_blkdev_disk_cleanup = NULL;
		mutex_unlock(&ledtrig_blkdev_mutex);
	}

	return ret;
}
module_init(blkdev_init);

static void __exit blkdev_exit(void)
{
	mutex_lock(&ledtrig_blkdev_mutex);

	/*
	 * It's OK to call led_trigger_unregister() with the mutex locked,
	 * because the module can only be unloaded when no LEDs are using
	 * the blkdev trigger, so blkdev_deactivate() won't be called.
	 */
	led_trigger_unregister(&ledtrig_blkdev_trigger);
	__ledtrig_blkdev_disk_cleanup = NULL;

	mutex_unlock(&ledtrig_blkdev_mutex);
}
module_exit(blkdev_exit);
