// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/genhd.h>
#include <linux/leds.h>
#include <linux/mutex.h>

#include "blk-ledtrig.h"

// Default blink_on & blink_off values
#define BLK_LEDTRIG_BLINK_ON	75
#define BLK_LEDTRIG_BLINK_OFF	25
#define BLK_LEDTRIG_BLINK_MAX	10000		// 10 seconds


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


/*
 *
 *	Associate an LED with the blkdev trigger
 *
 */

// Helper function to create <LED>/blkdevs subdirectory - doesn't
// swallow error code like kobject_create_and_add()
static int blk_ledtrig_subdir_create(struct blk_ledtrig_led *const bd_led,
				     struct led_classdev *const led)
{
	int ret;

	bd_led->dir = kobject_create();
	if (bd_led->dir == NULL)
		return -ENOMEM;

	ret = kobject_add(bd_led->dir, &led->dev->kobj, "block_devices");
	if (ret != 0)
		kobject_put(bd_led->dir);

	return ret;
}

static int blk_ledtrig_activate(struct led_classdev *const led)
{
	struct blk_ledtrig_led *bd_led;
	int ret;

	bd_led = kmalloc(sizeof(*bd_led), GFP_KERNEL);
	if (bd_led == NULL) {
		ret = -ENOMEM;
		goto activate_exit_return;
	}

	bd_led->led = led;
	bd_led->blink_on = BLK_LEDTRIG_BLINK_ON;
	bd_led->blink_off = BLK_LEDTRIG_BLINK_OFF;
	INIT_LIST_HEAD(&bd_led->dev_list);

	ret = mutex_lock_interruptible(&blk_ledtrig_mutex);
	if (ret != 0)
		goto activate_exit_free;

	ret = blk_ledtrig_subdir_create(bd_led, led);
	if (ret != 0)
		goto activate_exit_unlock;

	list_add(&bd_led->leds_list_node, &blk_ledtrig_leds);
	led_set_trigger_data(led, bd_led);
	ret = 0;

activate_exit_unlock:
	mutex_unlock(&blk_ledtrig_mutex);
activate_exit_free:
	if (ret != 0)
		kfree(bd_led);
activate_exit_return:
	return ret;
}


/*
 *
 *	Disassociate an LED from the blkdev trigger
 *
 */

static void blk_ledtrig_deactivate(struct led_classdev *const led)
{
	struct blk_ledtrig_led *const bd_led = led_get_trigger_data(led);
	struct gendisk *disk, *next;

	mutex_lock(&blk_ledtrig_mutex);

	list_for_each_entry_safe(disk, next,
				 &bd_led->dev_list, led_dev_list_node) {

		blk_ledtrig_dev_clear_locked(disk, bd_led);
	}

	list_del(&bd_led->leds_list_node);
	kobject_put(bd_led->dir);

	mutex_unlock(&blk_ledtrig_mutex);
	synchronize_rcu();
	kfree(bd_led);
}


/*
 *
 *	Per-LED blink_on & blink_off device attributes
 *
 */

static ssize_t blk_ledtrig_blink_show(struct device *const dev,
				      struct device_attribute *const attr,
				      char *const buf);

static ssize_t blk_ledtrig_blink_store(struct device *const dev,
				       struct device_attribute *const attr,
				       const char *const buf,
				       const size_t count);

static struct device_attribute blk_ledtrig_attr_blink_on =
	__ATTR(blink_on, 0644,
	       blk_ledtrig_blink_show, blk_ledtrig_blink_store);

static struct device_attribute blk_ledtrig_attr_blink_off =
	__ATTR(blink_off, 0644,
	       blk_ledtrig_blink_show, blk_ledtrig_blink_store);

static ssize_t blk_ledtrig_blink_show(struct device *const dev,
				      struct device_attribute *const attr,
				      char *const buf)
{
	struct blk_ledtrig_led *const bd_led = led_trigger_get_drvdata(dev);
	unsigned int value;

	if (attr == &blk_ledtrig_attr_blink_on)
		value = READ_ONCE(bd_led->blink_on);
	else	// attr == &blk_ledtrig_attr_blink_off
		value = READ_ONCE(bd_led->blink_off);

	return sprintf(buf, "%u\n", value);
}

static ssize_t blk_ledtrig_blink_store(struct device *const dev,
				       struct device_attribute *const attr,
				       const char *const buf,
				       const size_t count)
{
	struct blk_ledtrig_led *const bd_led = led_trigger_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret != 0)
		return ret;

	if (value > BLK_LEDTRIG_BLINK_MAX)
		return -ERANGE;

	if (attr == &blk_ledtrig_attr_blink_on)
		WRITE_ONCE(bd_led->blink_on, value);
	else	// attr == &blk_ledtrig_attr_blink_off
		WRITE_ONCE(bd_led->blink_off, value);

	return count;
}


/*
 *
 *	Initialization - register the trigger
 *
 */

static struct attribute *blk_ledtrig_attrs[] = {
	&blk_ledtrig_attr_blink_on.attr,
	&blk_ledtrig_attr_blink_off.attr,
	NULL
};

static const struct attribute_group blk_ledtrig_attr_group = {
	.attrs	= blk_ledtrig_attrs,
};

static const struct attribute_group *blk_ledtrig_attr_groups[] = {
	&blk_ledtrig_attr_group,
	NULL
};

static struct led_trigger blk_ledtrig_trigger = {
	.name		= "blkdev",
	.activate	= blk_ledtrig_activate,
	.deactivate	= blk_ledtrig_deactivate,
	.groups		= blk_ledtrig_attr_groups,
};

static int __init blk_ledtrig_init(void)
{
	return led_trigger_register(&blk_ledtrig_trigger);
}
device_initcall(blk_ledtrig_init);


/*
 *
 *	Blink the LED associated with a (non-NULL) disk (if set)
 *
 */

void __blk_ledtrig_try_blink(struct request *const rq)
{
	struct blk_ledtrig_led *bd_led;
	unsigned long delay_on, delay_off;

	rcu_read_lock();

	bd_led = rcu_dereference(rq->rq_disk->led);

	if (bd_led != NULL) {
		delay_on = READ_ONCE(bd_led->blink_on);
		delay_off = READ_ONCE(bd_led->blink_off);
		led_blink_set_oneshot(bd_led->led, &delay_on, &delay_off, 0);
	}

	rcu_read_unlock();
}
