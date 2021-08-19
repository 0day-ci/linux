// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/ctype.h>
#include <linux/genhd.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/part_stat.h>

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
	struct kobject		*dir;
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

/* Must hold when changing trigger/LED/device associations */
static DEFINE_MUTEX(ledtrig_blkdev_mutex);

/* Total number of device-to-LED associations */
static unsigned int ledtrig_blkdev_count;

/* How often to check for drive activity - in jiffies */
static unsigned int ledtrig_blkdev_interval;

static void blkdev_process(struct work_struct *const work);
static DECLARE_DELAYED_WORK(ledtrig_blkdev_work, blkdev_process);


/*
 *
 *	Miscellaneous helper functions
 *
 */

/* Like kobject_create_and_add(), but doesn't swallow error codes */
static struct kobject *blkdev_mkdir(const char *const name,
				    struct kobject *const parent)
{
	struct kobject *dir;
	int ret;

	dir = kobject_create();
	if (dir == NULL)
		return ERR_PTR(-ENOMEM);

	ret = kobject_add(dir, parent, "%s", name);
	if (ret != 0) {
		kobject_put(dir);
		return ERR_PTR(ret);
	}

	return dir;
}

/*
 * Compare a null-terminated C string with a non-null-terminated character
 * sequence of a known length.  Returns true if equal, false if not.
 */
static bool blkdev_streq(const char *const cstr,
			 const char *const cbuf, const size_t buf_len)
{
	return (strlen(cstr) == buf_len) && (memcmp(cstr, cbuf, buf_len) == 0);
}

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

	put_device(disk_to_dev(disk->gd));
}

static void blkdev_disk_delete(struct ledtrig_blkdev_led *const led,
			       const char *const disk_name,
			       const size_t name_len)
{
	struct ledtrig_blkdev_link *link;

	mutex_lock(&ledtrig_blkdev_mutex);

	hlist_for_each_entry(link, &led->disks, led_disks_node) {

		if (blkdev_streq(link->disk->gd->disk_name,
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

/**
 * ledtrig_blkdev_disk_cleanup - remove a block device from the blkdev LED
 * trigger
 * @disk:	the disk to be removed
 */
void ledtrig_blkdev_disk_cleanup(struct gendisk *const gd)
{
	struct ledtrig_blkdev_link *link;
	struct hlist_node *next;

	mutex_lock(&ledtrig_blkdev_mutex);

	if (gd->ledtrig != NULL) {

		hlist_for_each_entry_safe(link, next,
					  &gd->ledtrig->leds, disk_leds_node) {
			blkdev_disk_del_locked(link->led, link, gd->ledtrig);
		}
	}

	mutex_unlock(&ledtrig_blkdev_mutex);
}
EXPORT_SYMBOL_GPL(ledtrig_blkdev_disk_cleanup);


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

	dir = blkdev_mkdir("blkdev_leds", &disk_to_dev(gd)->kobj);
	if (IS_ERR(dir)) {
		kfree(disk);
		return PTR_ERR(dir);
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
	static char name[DISK_NAME_LEN];	/* only used w/ mutex locked */
	struct gendisk *gd;
	int ret;

	if (name_len >= DISK_NAME_LEN) {
		pr_info("blkdev LED: invalid device name %.*s\n",
			(int)name_len, disk_name);
		ret = -EINVAL;
		goto exit_return;
	}

	ret = mutex_lock_interruptible(&ledtrig_blkdev_mutex);
	if (ret != 0)
		goto exit_return;

	memcpy(name, disk_name, name_len);
	name[name_len] = 0;
	gd = get_disk_by_name(name);	/* increments disk's refcount */

	if (gd == NULL) {
		pr_info("blkdev LED: no such block device %.*s\n",
			(int)name_len, disk_name);
		ret = -ENODEV;
		goto exit_unlock;
	}

	if (blkdev_already_linked(led, gd)) {
		ret = -EEXIST;
		goto exit_put_dev;
	}

	ret = blkdev_disk_add_locked(led, gd);

exit_put_dev:
	if (ret != 0)
		put_device(disk_to_dev(gd));
exit_unlock:
	mutex_unlock(&ledtrig_blkdev_mutex);
exit_return:
	return ret;
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
	 * whitespace after the last token (e.g. a newline).
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

		if (blkdev_streq(blkdev_modes[mode].name,
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
 *	Associate an LED with the blkdev trigger
 *
 */

static int blkdev_activate(struct led_classdev *const led_dev)
{
	struct ledtrig_blkdev_led *led;
	int ret;

	led = kmalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL) {
		ret = -ENOMEM;
		goto exit_return;
	}

	led->led_dev = led_dev;
	led->blink_msec = LEDTRIG_BLKDEV_BLINK_MSEC;
	led->mode = LEDTRIG_BLKDEV_MODE_RW;
	INIT_HLIST_HEAD(&led->disks);

	ret = mutex_lock_interruptible(&ledtrig_blkdev_mutex);
	if (ret != 0)
		goto exit_free;

	led->dir = blkdev_mkdir("block_devices", &led_dev->dev->kobj);
	if (IS_ERR(led->dir)) {
		ret = PTR_ERR(led->dir);
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
exit_return:
	return ret;
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
}
