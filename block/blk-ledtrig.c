// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/ctype.h>
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


/*
 *
 *	sysfs attribute store function to set or clear device LED
 *
 */

// Returns a pointer to the first non-whitespace character in s (or a pointer
// to the terminating null).
static const char *blk_ledtrig_skip_whitespace(const char *s)
{
	while (*s != 0 && isspace(*s))
		++s;

	return s;
}

// Returns a pointer to the first whitespace character in s (or a pointer to
// the terminating null), which is effectively a pointer to the position *after*
// the last character in the non-whitespace token at the beginning of s.  (s is
// expected to be the result of a previous call to blk_ledtrig_skip_whitespace.)
static const char *blk_ledtrig_find_whitespace(const char *s)
{
	while (*s != 0 && !isspace(*s))
		++s;

	return s;
}

static bool blk_ledtrig_name_is_none(const char *const name, const size_t len)
{
	static const char none[4] = "none";	// no terminating null

	return len == sizeof(none) && memcmp(name, none, sizeof(none)) == 0;
}

ssize_t blk_ledtrig_dev_led_store(struct device *const dev,
				  struct device_attribute *const attr,
				  const char *const buf, const size_t count)
{
	struct gendisk *const disk = dev_to_disk(dev);
	const char *const led_name = blk_ledtrig_skip_whitespace(buf);
	const char *const endp = blk_ledtrig_find_whitespace(led_name);
	const ptrdiff_t name_len = endp - led_name;	// always >= 0
	int ret;

	if (name_len == 0 || blk_ledtrig_name_is_none(led_name, name_len)) {
		blk_ledtrig_dev_clear(disk);
		ret = 0;
	} else {
		ret = blk_ledtrig_dev_set(disk, led_name, name_len);
	}

	if (ret < 0)
		return ret;

	return count;
}


/*
 *
 *	sysfs attribute show function for device LED
 *
 */

ssize_t blk_ledtrig_dev_led_show(struct device *const dev,
				 struct device_attribute *const attr,
				 char *const buf)
{
	struct gendisk *const disk = dev_to_disk(dev);
	struct blk_ledtrig_led *bd_led, *disk_led;
	int ret, c = 0;

	ret = mutex_lock_interruptible(&blk_ledtrig_mutex);
	if (ret != 0)
		goto led_show_exit_return;

	disk_led = rcu_dereference_protected(disk->led,
					lockdep_is_held(&blk_ledtrig_mutex));

	if (disk_led == NULL)
		c += sprintf(buf, "[none]");
	else
		c += sprintf(buf, "none");

	list_for_each_entry(bd_led, &blk_ledtrig_leds, leds_list_node) {

		ret = snprintf(buf + c, PAGE_SIZE - c - 1,
			       bd_led == disk_led ? " [%s]" : " %s",
			       bd_led->led->name);
		if (ret >= PAGE_SIZE - c - 1) {
			ret = -EOVERFLOW;
			goto led_show_exit_unlock;
		}

		c += ret;
	}

	buf[c] = '\n';
	ret = c + 1;

led_show_exit_unlock:
	mutex_unlock(&blk_ledtrig_mutex);
led_show_exit_return:
	return ret;
}
