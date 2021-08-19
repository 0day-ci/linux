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
