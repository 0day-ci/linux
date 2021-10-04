// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED trigger
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/blkdev.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/part_stat.h>
#include <linux/xarray.h>

/* Default blink time & check interval (milliseconds) */
#define LED_BDEV_BLINK_MSEC	75
#define LED_BDEV_INTERVAL	100

/* Minimum blink time & check interval (milliseconds) */
#define LED_BDEV_MIN_BLINK	10
#define LED_BDEV_MIN_INT	25

/* Mode for blkdev_get_by_path() & blkdev_put() */
#define LED_BDEV_FMODE		0

/* Device activity type that will make LED blink */
enum led_bdev_led_mode {
	LED_BDEV_MODE_RO	= 0,
	LED_BDEV_MODE_WO	= 1,
	LED_BDEV_MODE_RW	= 2
};

/* When unlinking a block device from an LED, is the blkdev being released? */
enum led_bdev_unlink_mode {
	LED_BDEV_RELEASING,
	LED_BDEV_NOT_RELEASING
};

/* An "LED block device" (LBD) - 1 per blkdev linked to at least 1 LED */
struct led_bdev_lbd {
	unsigned long		index;
	struct block_device	*bdev;
	struct xarray		linked_leds;
	unsigned long		read_ios;
	unsigned long		write_ios;
	unsigned int		generation;
	bool			read_act;
	bool			write_act;
};

/* Every LED associated with the blkdev trigger gets one of these */
struct led_bdev_led {
	unsigned long		index;
	struct led_classdev	*led_cdev;
	unsigned int		blink_msec;
	struct xarray		linked_lbds;
	struct hlist_node	all_leds_node;
	enum led_bdev_led_mode	mode;
};

/* Forward declarations to make this file compile in a more readable order */
static void led_bdev_process(struct work_struct *work);
static struct led_bdev_lbd *led_bdev_get_lbd(const char *buf, size_t size);
static struct block_device *led_bdev_get_bdev(const char *buf, size_t size,
					      fmode_t mode);
static int led_bdev_link(struct led_bdev_led *led, struct led_bdev_lbd *lbd);
static void led_bdev_put_lbd(struct led_bdev_lbd *lbd);
static void led_bdev_lbd_release(struct device *dev, void *res);
static void led_bdev_unlink(struct led_bdev_led *led,
			    struct led_bdev_lbd *lbd,
			    enum led_bdev_unlink_mode unlink_mode);
static void led_bdev_update_lbd(struct led_bdev_lbd *lbd);
static bool led_bdev_blink(const struct led_bdev_led *led,
			   const struct led_bdev_lbd *lbd);

/* Index for next LBD or LED */
static unsigned long led_bdev_next_index;

/* Protects everything except atomic sysfs attributes */
static DEFINE_MUTEX(led_bdev_mutex);

/* All LEDs associated with the trigger */
static HLIST_HEAD(led_bdev_all_leds);

/* sysfs class for "global" trigger attributes (interval) */
static struct class *led_bdev_class;

/* Delayed work to periodically check for activity & blink LEDs */
static DECLARE_DELAYED_WORK(led_bdev_work, led_bdev_process);

/* How often to run the delayed work - in jiffies */
static unsigned int led_bdev_interval;

/* Incremented every time the delayed work runs */
static unsigned int led_bdev_generation;

/* Total number of device-to-LED associations (links) */
static unsigned int led_bdev_link_count;

/* Empty attribute list for the linked_leds & linked_devices "groups" */
static struct attribute *led_bdev_attrs_empty[] = { NULL };

/* linked_leds sysfs directory for block devs linked to 1 or more LEDs */
static const struct attribute_group led_bdev_linked_leds = {
	.name	= "linked_leds",
	.attrs	= led_bdev_attrs_empty,
};

/* linked_devices sysfs directory for each LED associated with the trigger */
static const struct attribute_group led_bdev_linked_devs = {
	.name	= "linked_devices",
	.attrs	= led_bdev_attrs_empty,
};

/**
 * led_bdev_activate() - Called when an LED is associated with the trigger.
 * @led_cdev:	The LED
 *
 * Allocates & initializes the @led_bdev_led structure, adds it to the
 * @led_bdev_all_leds list, and sets the LED's trigger data.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	``0`` on success, ``-errno`` on error.
 */
static int led_bdev_activate(struct led_classdev *const led_cdev)
{
	struct led_bdev_led *led;
	int ret;

	if (led_bdev_next_index == ULONG_MAX) {
		ret = -EOVERFLOW;
		goto exit_return;
	}

	led = kmalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL) {
		ret = -ENOMEM;
		goto exit_return;
	}

	led->index = led_bdev_next_index++;
	led->led_cdev = led_cdev;
	led->blink_msec = LED_BDEV_BLINK_MSEC;
	led->mode = LED_BDEV_MODE_RW;
	xa_init(&led->linked_lbds);

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_free;

	hlist_add_head(&led->all_leds_node, &led_bdev_all_leds);
	led_set_trigger_data(led_cdev, led);

	mutex_unlock(&led_bdev_mutex);

exit_free:
	if (ret != 0)
		kfree(led);
exit_return:
	return ret;
}

/**
 * link_device_store() - ``link_device`` device attribute store function.
 * @dev:	The LED device
 * @attr:	The ``link_device`` attribute (@dev_attr_link_device)
 * @buf:	The value written to the attribute, which should be the path to
 *		the special file that represents the block device to be linked
 *		to the LED (e.g. /dev/sda)
 * @count:	The number of characters in @buf
 *
 * Calls led_bdev_get_lbd() to find or create the LBD for the block device,
 * checks that the device isn't already linked to this LED, and calls
 * led_bdev_link() to create the link.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t link_device_store(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count)
{
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	struct led_bdev_lbd *lbd;
	int ret;

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_return;

	lbd = led_bdev_get_lbd(buf, count);
	if (IS_ERR(lbd)) {
		ret = PTR_ERR(lbd);
		goto exit_unlock;
	}

	if (xa_load(&lbd->linked_leds, led->index) != NULL) {
		ret = -EEXIST;
		goto exit_put_lbd;
	}

	ret = led_bdev_link(led, lbd);

exit_put_lbd:
	if (ret != 0)
		led_bdev_put_lbd(lbd);
exit_unlock:
	mutex_unlock(&led_bdev_mutex);
exit_return:
	return (ret == 0) ? count : ret;
}

/**
 * led_bdev_get_lbd() - Find or create the LBD for a block device.
 * @buf:	The value written to the ``link_device`` attribute, which should
 *		be the path to a special file that represents a block device
 * @count:	The number of characters in @buf
 *
 * Calls led_bdev_get() to get the block device represented by the path in @buf.
 * If the device already has an LBD (because it is already linked to an LED),
 * simply returns the existing LBD.
 *
 * Otherwise, allocates a new LBD (as a device resource), creates the block
 * device's ``linked_leds`` directory (attribute group), calls
 * led_bdev_update_lbd() to set the LBD's activity counters, and adds the LBD
 * resource to the block device.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	Pointer to the LBD, error pointer on error.
 */
static struct led_bdev_lbd *led_bdev_get_lbd(const char *const buf,
					     const size_t count)
{
	struct block_device *bdev;
	struct led_bdev_lbd *lbd;
	int ret;

	bdev = led_bdev_get_bdev(buf, count, LED_BDEV_FMODE);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto exit_return;
	}

	lbd = devres_find(&bdev->bd_device, led_bdev_lbd_release, NULL, NULL);
	if (lbd != NULL) {
		ret = 0;
		goto exit_put_bdev;
	}

	if (led_bdev_next_index == ULONG_MAX) {
		ret = -EOVERFLOW;
		goto exit_put_bdev;
	}

	lbd = devres_alloc(led_bdev_lbd_release, sizeof(*lbd), GFP_KERNEL);
	if (lbd == NULL) {
		ret = -ENOMEM;
		goto exit_put_bdev;
	}

	ret = sysfs_create_group(bdev_kobj(bdev), &led_bdev_linked_leds);
	if (ret != 0)
		goto exit_free_lbd;

	lbd->index = led_bdev_next_index++;
	lbd->bdev = bdev;
	xa_init(&lbd->linked_leds);

	/* Ensure that led_bdev_update_lbd() updates the LBD */
	lbd->generation = led_bdev_generation - 1;
	led_bdev_update_lbd(lbd);
	/* Don't blink for activity that may have happened long ago */
	lbd->read_act = false;
	lbd->write_act = false;

	devres_add(&bdev->bd_device, lbd);

exit_free_lbd:
	if (ret != 0)
		devres_free(lbd);
exit_put_bdev:
	blkdev_put(bdev, LED_BDEV_FMODE);  /* Allow the device to be released */
exit_return:
	return (ret == 0) ? lbd : ERR_PTR(ret);
}

/**
 * led_bdev_get_bdev() - Get a block device by path.
 * @buf:	The value written to the ``link_device`` or ``unlink_device``
 *		attribute, which should be the path to a special file that
 *		represents a block device
 * @count:	The number of characters in @buf (not including its terminating
 *		null)
 *
 * Copies @buf to a writable buffer, trims the trailing newline (if any), and
 * calls blkdev_get_by_path() to resolve the block device.
 *
 * The caller must call blkdev_put() when finished with the device.
 *
 * Context:	Process context.
 * Return:	The block device, or an error pointer.
 */
static struct block_device *led_bdev_get_bdev(const char *const buf,
					      const size_t count,
					      const fmode_t mode)
{
	struct block_device *bdev;
	char *path;

	path = kmemdup(buf, count + 1, GFP_KERNEL);  /* include null */
	if (path == NULL)
		return ERR_PTR(-ENOMEM);

	if (path[count - 1] == '\n')
		path[count - 1] = 0;

	bdev = blkdev_get_by_path(path, mode, THIS_MODULE);
	kfree(path);
	return bdev;
}

/**
 * led_bdev_update_lbd() - Update an LBD's activity counters for the current
 *			   generation
 * @lbd:	The LBD
 *
 * Does nothing if the LBD has already been updated during this generation.
 *
 * Otherwise, updates the LBD's activity counters (@read_ios & @write_ios) and
 * @generation, and sets @read_act and/or @write_act if the corresponding
 * counters have changed.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 */
static void led_bdev_update_lbd(struct led_bdev_lbd *const lbd)
{
	unsigned long read_ios, write_ios;

	if (lbd->generation != led_bdev_generation) {

		read_ios = part_stat_read(lbd->bdev, ios[STAT_READ]);

		write_ios = part_stat_read(lbd->bdev, ios[STAT_WRITE])
				+ part_stat_read(lbd->bdev, ios[STAT_DISCARD])
				+ part_stat_read(lbd->bdev, ios[STAT_FLUSH]);

		if (lbd->read_ios != read_ios) {
			lbd->read_act = true;
			lbd->read_ios = read_ios;
		} else {
			lbd->read_act = false;
		}

		if (lbd->write_ios != write_ios) {
			lbd->write_act = true;
			lbd->write_ios = write_ios;
		} else {
			lbd->write_act = false;
		}

		lbd->generation = led_bdev_generation;
	}
}

/**
 * led_bdev_link() - "Link" a block device to an LED.
 * @led:	The LED
 * @lbd:	The block device
 *
 * Called from link_device_store() to create the link between an LED and a
 * block device.
 *
 *   * Adds block device symlink to LED's ``linked_devices`` directory.
 *   * Adds LED symlink to block devices's ``linked_leds`` directory.
 *   * Adds the LBD to the LED's @linked_lbds and adds the LED to the LBD's
 *     @linked_leds.
 *
 * If the new link is the only one (i.e. no other block device/LED links
 * already exist), schedule delayed work to periodically check for block
 * device activity and blink LEDs.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	0 on success, ``-errno`` on error.
 */
static int led_bdev_link(struct led_bdev_led *const led,
			 struct led_bdev_lbd *const lbd)
{
	unsigned long delay;
	int ret;

	ret = xa_insert(&lbd->linked_leds, led->index, led, GFP_KERNEL);
	if (ret != 0)
		goto error_return;

	ret = xa_insert(&led->linked_lbds, lbd->index, lbd, GFP_KERNEL);
	if (ret != 0)
		goto error_erase_led;

	/* /sys/class/block/<bdev>/linked_leds/<led> */
	ret = sysfs_add_link_to_group(bdev_kobj(lbd->bdev),
				      led_bdev_linked_leds.name,
				      &led->led_cdev->dev->kobj,
				      led->led_cdev->name);
	if (ret != 0)
		goto error_erase_lbd;

	/* /sys/class/leds/<led>/linked_devices/<bdev> */
	ret = sysfs_add_link_to_group(&led->led_cdev->dev->kobj,
				      led_bdev_linked_devs.name,
				      bdev_kobj(lbd->bdev),
				      dev_name(&lbd->bdev->bd_device));
	if (ret != 0)
		goto error_remove_symlink;

	if (led_bdev_link_count == 0) {
		delay = READ_ONCE(led_bdev_interval);
		WARN_ON(!schedule_delayed_work(&led_bdev_work, delay));
	}

	++led_bdev_link_count;

	return 0;

error_remove_symlink:
	sysfs_remove_link_from_group(bdev_kobj(lbd->bdev),
				     led_bdev_linked_leds.name,
				     led->led_cdev->name);
error_erase_lbd:
	xa_erase(&led->linked_lbds, lbd->index);
error_erase_led:
	xa_erase(&lbd->linked_leds, led->index);
error_return:
	return ret;
}

/**
 * unlink_device_store() - ``unlink_device`` device attribute store function.
 * @dev:	The LED device
 * @attr:	The ``unlink_device`` attribute (@dev_attr_unlink_device)
 * @buf:	The value written to the attribute, which should be the path to
 *		the special file that represents the block device to be unlinked
 *		from the LED (e.g. /dev/sda)
 * @count:	The number of characters in @buf
 *
 * Block device name is written to the attribute to "unlink" the block device
 * from the LED.  I.e. the LED will no longer blink to show activity on that
 * block device.
 *
 * Calls led_bdev_get() to get the block device represented by the path in @buf.
 * If the device has an LBD, searches the LBD's list of LEDs for a link to this
 * LED and (if found) calls led_bdev_unlink() to destroy the link.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t unlink_device_store(struct device *const dev,
				   struct device_attribute *const attr,
				   const char *const buf, const size_t count)
{
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	struct block_device *bdev;
	struct led_bdev_lbd *lbd;
	int ret;

	bdev = led_bdev_get_bdev(buf, count, LED_BDEV_FMODE);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto exit_return;
	}

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_put_bdev;

	lbd = devres_find(&bdev->bd_device, led_bdev_lbd_release, NULL, NULL);
	if (lbd == NULL) {
		ret = -EUNATCH;  /* bdev isn't linked to any LED */
		goto exit_unlock;
	}

	if (xa_load(&lbd->linked_leds, led->index) == NULL) {
		ret = -EUNATCH;  /* bdev isn't linked to this LED */
		goto exit_unlock;
	}

	led_bdev_unlink(led, lbd, LED_BDEV_NOT_RELEASING);

exit_unlock:
	mutex_unlock(&led_bdev_mutex);
exit_put_bdev:
	blkdev_put(bdev, LED_BDEV_FMODE);
exit_return:
	return (ret == 0) ? count : ret;
}

/**
 * led_bdev_unlink() - "Unlink" a block device from an LED.
 * @led:		The LED
 * @lbd:		The block device
 * @unlink_mode:	Indicates whether the LBD is being released (because the
 *			block device has been removed)
 *
 * Removes the link between an LED and a block device.
 *
 *   * Removes the LBD from the LED's @linked_lbds and removes the LED from the
 *     LBD's @linked_leds.
 *   * Removes the block device symlink from the LED's ``linked_devices``
 *     directory.
 *
 * If the block device is **not** being released:
 *
 *   * Removes the LED symlink from the block device's ``linked_leds``
 *     directory.
 *   * Calls led_bdev_put_lbd() to clean up the LBD, if required.
 *
 * If the removed link was the only one (i.e. there are no existing block
 * device/LED links after its removal), cancels the periodic delayed work
 * which checks for device activity.
 *
 * This function is called from multiple locations.
 *
 *   * unlink_device_store() calls this function when a block device is unlinked
 *     from an LED via the ``unlink_device`` sysfs attribute.   (@unlink_mode ==
 *     ``LED_BDEV_NOT_RELEASING``)
 *   * led_bdev_deactivate() calls this function for each block device linked to
 *     an LED that is being deactivated (disassociated from the trigger).
 *     (@unlink_mode == ``LED_BDEV_NOT_RELEASING``).
 *   * led_bdev_lbd_release() calls this function for each LED linked to a block
 *     device that has been removed from the system.  (@unlink_mode ==
 *     ``LED_BDEV_RELEASING).
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 */
static void led_bdev_unlink(struct led_bdev_led *const led,
			    struct led_bdev_lbd *const lbd,
			    const enum led_bdev_unlink_mode unlink_mode)
{
	--led_bdev_link_count;

	if (led_bdev_link_count == 0)
		WARN_ON(!cancel_delayed_work_sync(&led_bdev_work));

	xa_erase(&lbd->linked_leds, led->index);
	xa_erase(&led->linked_lbds, lbd->index);

	/* /sys/class/leds/<led>/linked_devices/<bdev> */
	sysfs_remove_link_from_group(&led->led_cdev->dev->kobj,
				     led_bdev_linked_devs.name,
				     dev_name(&lbd->bdev->bd_device));

	/*
	 * If the LBD is being released, the device's attribute groups have
	 * already been removed, and the LBD will be freed automatically.
	 */
	if (unlink_mode == LED_BDEV_NOT_RELEASING) {

		/* /sys/class/block/<bdev>/linked_leds/<led> */
		sysfs_remove_link_from_group(bdev_kobj(lbd->bdev),
					     led_bdev_linked_leds.name,
					     led->led_cdev->name);
		led_bdev_put_lbd(lbd);
	}
}

/**
 * led_bdev_put_lbd() - Remove and free an LBD, if it is no longer needed.
 * @lbd:	The LBD
 *
 * Does nothing if the LBD (block device) is still linked to at least one LED.
 *
 * If the LBD is no longer linked to any LEDs, removes the block device's
 * ``linked_leds`` directory (attribute group), removes the LBD from the
 * block device's resource list, and frees the LBD.
 *
 * Called from led_bdev_unlink() (and in the link_device_store() error path).
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 */
static void led_bdev_put_lbd(struct led_bdev_lbd *const lbd)
{
	struct block_device *const bdev = lbd->bdev;
	int ret;

	if (xa_empty(&lbd->linked_leds)) {

		sysfs_remove_group(bdev_kobj(bdev), &led_bdev_linked_leds);
		ret = devres_destroy(&bdev->bd_device, led_bdev_lbd_release,
				     NULL, NULL);
		WARN_ON(ret != 0);
	}
}

/**
 * led_bdev_deactivate() - Called when an LED is disassociated from the
 *			   trigger.
 * @led_cdev:	The LED
 *
 * Calls led_bdev_unlink() for each block device linked to the LED, removes
 * the LED from the @led_bdev_all_leds list, and frees the @led_bdev_led.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_deactivate(struct led_classdev *const led_cdev)
{
	struct led_bdev_led *const led = led_get_trigger_data(led_cdev);
	struct led_bdev_lbd *lbd;
	unsigned long index;

	mutex_lock(&led_bdev_mutex);

	xa_for_each (&led->linked_lbds, index, lbd)
		led_bdev_unlink(led, lbd, LED_BDEV_NOT_RELEASING);

	hlist_del(&led->all_leds_node);
	kfree(led);

	mutex_unlock(&led_bdev_mutex);
}

/**
 * led_bdev_lbd_release() - LBD device resource release function.
 * @dev:	The block device
 * @res:	The LBD
 *
 * Called by the driver core when a block device with an LBD is removed from the
 * system.  Calls led_bdev_unlink() for each LED linked to the block device.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_lbd_release(struct device *const dev, void *const res)
{
	struct led_bdev_lbd *const lbd = res;
	struct led_bdev_led *led;
	unsigned long index;

	mutex_lock(&led_bdev_mutex);

	xa_for_each (&lbd->linked_leds, index, led)
		led_bdev_unlink(led, lbd, LED_BDEV_RELEASING);

	mutex_unlock(&led_bdev_mutex);
}

/**
 * led_bdev_process() - Check linked devices for activity and blink LEDs.
 * @work:	Delayed work (@led_bdev_work)
 *
 * Iterates through block devices linked to LEDs and calls led_bdev_update_lbd()
 * to update the LBD's activity counters.  If a particular LED hasn't already
 * been blinked, calls led_bdev_blink() to blink the LED if appropriate for that
 * device's activity.
 *
 * When finished, schedules itself to run again after @led_bdev_interval
 * jiffies.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_process(struct work_struct *const work)
{
	struct led_bdev_led *led;
	unsigned long delay;

	if (!mutex_trylock(&led_bdev_mutex))
		goto exit_reschedule;

	hlist_for_each_entry (led, &led_bdev_all_leds, all_leds_node) {

		struct led_bdev_lbd *lbd;
		unsigned long index;
		bool blinked = false;

		xa_for_each (&led->linked_lbds, index, lbd) {

			led_bdev_update_lbd(lbd);
			if (!blinked)
				blinked = led_bdev_blink(led, lbd);
		}
	}

	++led_bdev_generation;

	mutex_unlock(&led_bdev_mutex);

exit_reschedule:
	delay = READ_ONCE(led_bdev_interval);
	WARN_ON_ONCE(!schedule_delayed_work(&led_bdev_work, delay));
}

/**
 * led_bdev_blink() - Blink an LED, if the correct type of activity has occurred
 *		      on the block device.
 * @led:	The LED
 * @lbd:	The block device
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	``true`` if the LED is blinked, ``false`` if not.
 */
static bool led_bdev_blink(const struct led_bdev_led *const led,
			   const struct led_bdev_lbd *const lbd)
{
	unsigned long delay_on, delay_off;

	if ((lbd->read_act && led->mode != LED_BDEV_MODE_WO)
			|| (lbd->write_act && led->mode != LED_BDEV_MODE_RO)) {

		delay_on = READ_ONCE(led->blink_msec);
		delay_off = 1;	/* 0 leaves LED turned on */

		led_blink_set_oneshot(led->led_cdev, &delay_on, &delay_off, 0);
		return true;
	}

	return false;
}

/**
 * blink_time_show() - ``blink_time`` device attribute show function.
 * @dev:	The LED device
 * @attr:	The ``blink_time`` attribute (@dev_attr_blink_time)
 * @buf:	Output buffer
 *
 * Writes the current value of the LED's @blink_msec to @buf.
 *
 * Context:	Process context.
 * Return:	The number of characters written to @buf.
 */
static ssize_t blink_time_show(struct device *const dev,
			       struct device_attribute *const attr,
			       char *const buf)
{
	const struct led_bdev_led *const led = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n", READ_ONCE(led->blink_msec));
}

/**
 * blink_time_store() - ``blink_time`` device attribute store function.
 * @dev:	The LED device
 * @attr:	The ``blink_time`` attribute (@dev_attr_blink_time)
 * @buf:	The new value (as written to the sysfs attribute)
 * @count:	The number of characters in @buf
 *
 * Sets the LED's @blink_msec (the duration in milliseconds of one blink).
 *
 * Context:	Process context.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t blink_time_store(struct device *const dev,
				struct device_attribute *const attr,
				const char *const buf, const size_t count)
{
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret != 0)
		return ret;

	if (value < LED_BDEV_MIN_BLINK)
		return -ERANGE;

	WRITE_ONCE(led->blink_msec, value);
	return count;
}

/**
 * mode_show() - ``mode`` device attribute show function.
 * @dev:	The LED device
 * @attr:	The ``mode`` attribute (@dev_attr_mode)
 * @buf:	Output buffer
 *
 * Writes the current value of the LED's @mode to @buf.
 *
 * Context:	Process context.
 * Return:	The number of characters written to @buf.
 */
static ssize_t mode_show(struct device *const dev,
			 struct device_attribute *const attr, char *const buf)
{
	static const char modes[][sizeof("[read] write rw\n")] = {
		[LED_BDEV_MODE_RO] = "[read] write rw\n",
		[LED_BDEV_MODE_WO] = "read [write] rw\n",
		[LED_BDEV_MODE_RW] = "read write [rw]\n"
	};

	const struct led_bdev_led *const led = led_trigger_get_drvdata(dev);

	return sprintf(buf, modes[READ_ONCE(led->mode)]);
}

/**
 * mode_store() - ``mode`` device attribute store function.
 * @dev:	The LED device
 * @attr:	The ``mode`` attribute (@dev_attr_mode)
 * @buf:	The new value (as written to the sysfs attribute)
 * @count:	The number of characters in @buf
 *
 * Sets the LED's @mode (``read``, ``write``, or ``rw``).
 *
 * Context:	Process context.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t mode_store(struct device *const dev,
			  struct device_attribute *const attr,
			  const char *const buf, const size_t count)
{
	static const char modes[][sizeof("write")] = {
		[LED_BDEV_MODE_RO] = "read",
		[LED_BDEV_MODE_WO] = "write",  /* longest */
		[LED_BDEV_MODE_RW] = "rw"
	};

	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	enum led_bdev_led_mode mode;

	for (mode = LED_BDEV_MODE_RO; mode <= LED_BDEV_MODE_RW; ++mode) {

		if (sysfs_streq(modes[mode], buf)) {
			WRITE_ONCE(led->mode, mode);
			return count;
		}
	}

	return -EINVAL;
}

/**
 * interval_show() - ``interval`` class attribute show function.
 * @class:	The ``ledtrig_blkdev`` class (@led_bdev_class)
 * @attr:	The ``interval`` attribute (@class_attr_interval)
 * @buf:	Output buffer
 *
 * Writes the current value of @led_bdev_interval to @buf.
 *
 * Context:	Process context.
 * Return:	The number of characters written to @buf.
 */
static ssize_t interval_show(struct class *const class,
			     struct class_attribute *const attr,
			     char *const buf)
{
	return sprintf(buf, "%u\n",
		       jiffies_to_msecs(READ_ONCE(led_bdev_interval)));
}

/**
 * interval_store() - ``interval`` class attribute store function
 * @class:	The ``ledtrig_blkdev`` class (@led_bdev_class)
 * @attr:	The ``interval`` attribute (@class_attr_interval)
 * @buf:	The new value (as written to the sysfs attribute)
 * @count:	The number of characters in @buf
 *
 * Sets @led_bdev_interval (after converting to jiffies).
 *
 * Context:	Process context.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t interval_store(struct class *const class,
			      struct class_attribute *const attr,
			      const char *const buf, const size_t count)
{
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret != 0)
		return ret;

	if (value < LED_BDEV_MIN_INT)
		return -ERANGE;

	WRITE_ONCE(led_bdev_interval, msecs_to_jiffies(value));

	return count;
}

/* Device and class attributes */
static DEVICE_ATTR_WO(link_device);
static DEVICE_ATTR_WO(unlink_device);
static DEVICE_ATTR_RW(blink_time);
static DEVICE_ATTR_RW(mode);
static CLASS_ATTR_RW(interval);

/* Device attributes in LED directory (/sys/class/leds/<led>/...) */
static struct attribute *led_bdev_attrs[] = {
	&dev_attr_link_device.attr,
	&dev_attr_unlink_device.attr,
	&dev_attr_blink_time.attr,
	&dev_attr_mode.attr,
	NULL
};

/* Unnamed attribute group == no subdirectory */
static const struct attribute_group led_bdev_attr_group = {
	.attrs	= led_bdev_attrs,
};

/* Attribute groups for the trigger */
static const struct attribute_group *led_bdev_attr_groups[] = {
	&led_bdev_attr_group,   /* /sys/class/leds/<led>/... */
	&led_bdev_linked_devs,  /* /sys/class/leds/<led>/linked_devices/ */
	NULL
};

/* Trigger registration data */
static struct led_trigger led_bdev_trigger = {
	.name		= "blkdev",
	.activate	= led_bdev_activate,
	.deactivate	= led_bdev_deactivate,
	.groups		= led_bdev_attr_groups,
};

/**
 * led_bdev_init() - Block device LED trigger initialization.
 *
 * Converts default @led_bdev_interval from milliseconds to jiffies, creates
 * the ``/sys/class/ledtrig_blkdev/interval`` attribute, and registers the LED
 * trigger.
 *
 * Return:	0 on success, ``-errno`` on failure.
 */
static int __init led_bdev_init(void)
{
	int ret;

	WRITE_ONCE(led_bdev_interval,
		   msecs_to_jiffies(LED_BDEV_INTERVAL));

	led_bdev_class = class_create(THIS_MODULE, "ledtrig_blkdev");
	if (IS_ERR(led_bdev_class)) {
		ret = PTR_ERR(led_bdev_class);
		goto exit_return;
	}

	ret = class_create_file(led_bdev_class, &class_attr_interval);
	if (ret != 0)
		goto exit_destroy_class;

	ret = led_trigger_register(&led_bdev_trigger);

exit_destroy_class:
	if (ret != 0)
		class_destroy(led_bdev_class);  /* removes the attribute file */
exit_return:
	return ret;
}
module_init(led_bdev_init);

/**
 * led_bdev_exit() - Block device LED trigger module exit.
 *
 * Unregisters the LED trigger and removes the
 * ``/sys/class/ledtrig_blkdev/interval`` attribute.
 */
static void __exit led_bdev_exit(void)
{
	led_trigger_unregister(&led_bdev_trigger);
	class_destroy(led_bdev_class);
}
module_exit(led_bdev_exit);

MODULE_DESCRIPTION("Block device LED trigger");
MODULE_AUTHOR("Ian Pilcher <arequipeno@gmail.com>");
MODULE_LICENSE("GPL v2");
