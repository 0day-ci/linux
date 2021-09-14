// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED trigger
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/genhd.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/part_stat.h>

/* Default blink time & check interval (milliseconds) */
#define LED_BDEV_BLINK_MSEC	75
#define LED_BDEV_INTERVAL	100

/* Minimum blink time & check interval (milliseconds) */
#define LED_BDEV_MIN_BLINK	10
#define LED_BDEV_MIN_INT	25

/* Device activity type that will make LED blink */
enum led_bdev_mode {
	LED_BDEV_MODE_RO	= 0,
	LED_BDEV_MODE_WO	= 1,
	LED_BDEV_MODE_RW	= 2
};

/* Block device information (BDI) - 1 per blkdev linked to at least 1 LED */
struct led_bdev_bdi {
	struct block_device	*bdev;
	struct hlist_head	leds;
	unsigned long		read_ios;
	unsigned long		write_ios;
	unsigned int		generation;
	bool			read_act;
	bool			write_act;
};

/* For many-to-many relationships between block devices and LEDs */
struct led_bdev_link {
	struct hlist_node	bdi_leds_node;
	struct hlist_node	led_bdis_node;
	struct led_bdev_bdi	*bdi;
	struct led_bdev_led	*led;
};

/* Every LED associated with the led_bdev trigger gets one of these */
struct led_bdev_led {
	struct led_classdev	*led_cdev;
	unsigned int		blink_msec;
	struct hlist_head	bdis;
	struct hlist_node	leds_node;
	enum led_bdev_mode	mode;
};

/* Forward declarations to make this file compile in a reasonable order */
static void led_bdev_process(struct work_struct *work);
static struct led_bdev_bdi *led_bdev_get_bdi(const char *name);
static int led_bdev_link(struct led_bdev_led *led, struct led_bdev_bdi *bdi);
static void led_bdev_put_bdi(struct led_bdev_bdi *bdi);
static void led_bdev_bdi_release(struct device *dev, void *res);
static void led_bdev_unlink(struct led_bdev_led *led,
			    struct led_bdev_link *link,
			    struct led_bdev_bdi *bdi, bool releasing);
static bool led_bdev_process_link(const struct led_bdev_led *led,
				  const struct led_bdev_link *link,
				  unsigned int generation);

/* Protects everything except atomic sysfs attributes */
static DEFINE_MUTEX(led_bdev_mutex);

/* All LEDs associated with the trigger */
static HLIST_HEAD(led_bdev_leds);

/* Class for "global" trigger attributes (interval) */
static struct class *led_bdev_class;

/* How often to check for drive activity - in jiffies */
static unsigned int led_bdev_interval;

/* Delayed work to periodically check for activity & blink LEDs */
static DECLARE_DELAYED_WORK(led_bdev_work, led_bdev_process);

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
 * Allocates & initializes the @led_bdev_led structure and adds it to the
 * @led_bdev_leds list.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	``0`` on success, ``-errno`` on error.
 */
static int led_bdev_activate(struct led_classdev *const led_cdev)
{
	struct led_bdev_led *led;
	int ret;

	led = kmalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL) {
		ret = -ENOMEM;
		goto exit_return;
	}

	led->led_cdev = led_cdev;
	led->blink_msec = LED_BDEV_BLINK_MSEC;
	led->mode = LED_BDEV_MODE_RW;
	INIT_HLIST_HEAD(&led->bdis);

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_free;

	hlist_add_head(&led->leds_node, &led_bdev_leds);
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
 * @buf:	The name of the block device (as written to sysfs attribute)
 * @count:	The number of bytes written to the attribute
 *
 * Block device name is written to the attribute to "link" the block device
 * to the LED.  LED will blink to show activity on any of its linked blocked
 * devices.
 *
 *   * Calls led_bdev_get_bdi() to find or create the BDI for the device.
 *   * Checks that device isn't already linked to this LED.
 *   * Calls led_bdev_link() to create the link.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t link_device_store(struct device *const dev,
				 struct device_attribute *const attr,
				 const char *const buf, const size_t count)
{
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	const struct led_bdev_link *link;
	struct led_bdev_bdi *bdi;
	int ret;

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_return;

	bdi = led_bdev_get_bdi(buf);
	if (IS_ERR(bdi)) {
		ret = PTR_ERR(bdi);
		goto exit_unlock;
	}

	hlist_for_each_entry(link, &bdi->leds, bdi_leds_node) {
		if (link->led == led) {
			ret = -EEXIST;
			goto exit_put_bdi;
		}
	}

	ret = led_bdev_link(led, bdi);

exit_put_bdi:
	if (ret != 0)
		led_bdev_put_bdi(bdi);
exit_unlock:
	mutex_unlock(&led_bdev_mutex);
exit_return:
	return (ret == 0) ? count : ret;
}

/**
 * led_bdev_get_bdi() - Find or create the BDI for a block device.
 * @name:	The name of the block device
 *
 * If the device already has a BDI (because it is already linked to an LED),
 * simply return the existing BDI.
 *
 * Otherwise, allocate a new BDI (as a device resource), create the block
 * device's ``linked_leds`` directory (attribute group), and add the BDI
 * resource to the block device.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	Pointer to the BDI, error pointer on error.
 */
static struct led_bdev_bdi *led_bdev_get_bdi(const char *const name)
{
	struct led_bdev_bdi *bdi;
	struct device *dev;
	int ret;

	dev = class_find_device_by_name(&block_class, name);
	if (dev == NULL) {
		ret = -ENODEV;
		goto exit_return;
	}

	bdi = devres_find(dev, led_bdev_bdi_release, NULL, NULL);
	if (bdi != NULL) {
		ret = 0;
		goto exit_put_dev;
	}

	bdi = devres_alloc(led_bdev_bdi_release, sizeof(*bdi), GFP_KERNEL);
	if (bdi == NULL) {
		ret = -ENOMEM;
		goto exit_put_dev;
	}

	ret = sysfs_create_group(&dev->kobj, &led_bdev_linked_leds);
	if (ret != 0)
		goto exit_free_bdi;

	INIT_HLIST_HEAD(&bdi->leds);
	bdi->bdev = dev_to_bdev(dev);
	bdi->read_ios = 0;
	bdi->write_ios = 0;

	devres_add(dev, bdi);

exit_free_bdi:
	if (ret != 0)
		devres_free(bdi);
exit_put_dev:
	put_device(dev);	/* Allow the device to be released */
exit_return:
	return (ret == 0) ? bdi : ERR_PTR(ret);
}

/**
 * led_bdev_link() - "Link" a block device to an LED.
 * @led:	The LED
 * @bdi:	The block device
 *
 * Called from link_device_store() to create the link between an LED and a
 * block device.
 *
 *   * Allocates & initializes link structure.
 *   * Adds block device symlink to LED's ``linked_devices`` directory.
 *   * Adds LED symlink to block devices's ``linked_leds`` directory.
 *   * Adds link to LED's list of block devices and device's list of LEDs.
 *
 * If the new link is the only one (i.e. no other block device/LED links
 * already exist), schedule delayed work to periodically check for block
 * device activity and blink LEDs.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	0 on success, ``-errno`` on error.
 */
static int led_bdev_link(struct led_bdev_led *const led,
			 struct led_bdev_bdi *const bdi)
{
	struct led_bdev_link *link;
	unsigned long delay;
	int ret;

	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (link == NULL) {
		ret = -ENOMEM;
		goto error_return;
	}

	ret = sysfs_add_link_to_group(bdev_kobj(bdi->bdev),
				      led_bdev_linked_leds.name,
				      &led->led_cdev->dev->kobj,
				      led->led_cdev->name);
	if (ret != 0)
		goto error_free_link;

	ret = sysfs_add_link_to_group(&led->led_cdev->dev->kobj,
				      led_bdev_linked_devs.name,
				      bdev_kobj(bdi->bdev),
				      dev_name(&bdi->bdev->bd_device));
	if (ret != 0)
		goto error_remove_symlink;

	link->bdi = bdi;
	link->led = led;
	hlist_add_head(&link->led_bdis_node, &led->bdis);
	hlist_add_head(&link->bdi_leds_node, &bdi->leds);

	if (led_bdev_link_count == 0) {
		delay = READ_ONCE(led_bdev_interval);
		WARN_ON(!schedule_delayed_work(&led_bdev_work, delay));
	}

	++led_bdev_link_count;

	return 0;

error_remove_symlink:
	sysfs_remove_link_from_group(bdev_kobj(bdi->bdev),
				     led_bdev_linked_leds.name,
				     led->led_cdev->name);
error_free_link:
	kfree(link);
error_return:
	return ret;
}

/**
 * unlink_device_store() - ``unlink_device`` device attribute store function.
 * @dev:	The LED device
 * @attr:	The ``unlink_device`` attribute (@dev_attr_unlink_device)
 * @buf:	The name of the block device (as written to the sysfs attribute)
 * @count:	The number of bytes written to the attribute
 *
 * Block device name is written to the attribute to "unlink" the block device
 * from the LED.  I.e. the LED will no longer blink to show activity on that
 * block device.
 *
 *   * Finds block device (BDI) in LED's list of linked devices.
 *   * Calls led_bdev_unlink() to destroy the link.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t unlink_device_store(struct device *const dev,
				   struct device_attribute *const attr,
				   const char *const buf, const size_t count)
{
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	struct led_bdev_link *link;
	int ret;

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		return ret;

	hlist_for_each_entry(link, &led->bdis, led_bdis_node) {

		if (sysfs_streq(dev_name(&link->bdi->bdev->bd_device), buf)) {
			led_bdev_unlink(led, link, link->bdi, false);
			break;
		}
	}

	mutex_unlock(&led_bdev_mutex);

	return count;
}

/**
 * led_bdev_unlink() - "Unlink" a block device from an LED.
 * @led:	The LED
 * @link:	The existing link between the LED and the block device
 * @bdi:	The block device
 * @releasing:	Indicates whether the BDI is being released (because the
 *		block device has been removed)
 *
 * Removes and frees the link between an LED and a block device.
 *
 *   * Removes the link from the LED's list of block devices and the
 *     device's list of LEDs.
 *   * Frees the link structure.
 *   * Removes the block device symlink from the LED's ``linked_devices``
 *     directory.
 *
 * If the block device is **not** being released:
 *
 *   * Removes the LED symlink from the block device's ``linked_leds``
 *     directory.
 *   * Calls led_bdev_put_bdi() to clean up the BDI, if required.
 *
 * If the removed link was the only one (i.e. there are no existing block
 * device/LED links after its removal), cancel the periodic delayed work
 * which checks for device activity.
 *
 * Called from multiple locations:
 *
 *   * unlink_device_store() - requested via ``unlink_device`` sysfs attribute
 *     (@releasing == ``false``)
 *   * led_bdev_deactivate() - unlink a block device from an LED that is no
 *     longer associated with the trigger (@releasing == ``false``)
 *   * led_bdev_bdi_release() - unlink an LED from a block device that has
 *     been removed (@releasing == ``true``)
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 */
static void led_bdev_unlink(struct led_bdev_led *const led,
			    struct led_bdev_link *const link,
			    struct led_bdev_bdi *const bdi,
			    const bool releasing)
{
	--led_bdev_link_count;

	if (led_bdev_link_count == 0)
		WARN_ON(!cancel_delayed_work_sync(&led_bdev_work));

	hlist_del(&link->led_bdis_node);
	hlist_del(&link->bdi_leds_node);
	kfree(link);

	sysfs_remove_link_from_group(&led->led_cdev->dev->kobj,
				     led_bdev_linked_devs.name,
				     dev_name(&bdi->bdev->bd_device));

	/*
	 * If the BDI is being released, the device's attribute groups have
	 * already been removed, and the BDI will be freed automatically.
	 */
	if (!releasing) {

		sysfs_remove_link_from_group(bdev_kobj(bdi->bdev),
					     led_bdev_linked_leds.name,
					     led->led_cdev->name);
		led_bdev_put_bdi(bdi);
	}
}

/**
 * led_bdev_put_bdi() - Remove and free a BDI, if it is no longer needed.
 * @bdi:	The BDI
 *
 * Does nothing if the BDI (block device) is still linked to at least one LED.
 *
 * If the BDI is no longer linked to any LEDs:
 *
 *   * Removes the block device's ``linked_leds`` directory (attribute group).
 *   * Removes the BDI from the block device and frees it.
 *
 * Called from led_bdev_unlink() (and in link_device_store() error path).
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 */
static void led_bdev_put_bdi(struct led_bdev_bdi *const bdi)
{
	struct block_device *const bdev = bdi->bdev;
	int ret;

	if (hlist_empty(&bdi->leds)) {

		sysfs_remove_group(bdev_kobj(bdev), &led_bdev_linked_leds);
		ret = devres_destroy(&bdev->bd_device, led_bdev_bdi_release,
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
 * the LED from the @led_bdev_leds list, and frees the @led_bdev_led structure.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_deactivate(struct led_classdev *const led_cdev)
{
	struct led_bdev_led *const led = led_get_trigger_data(led_cdev);
	struct led_bdev_link *link;
	struct hlist_node *next;

	mutex_lock(&led_bdev_mutex);

	hlist_for_each_entry_safe(link, next, &led->bdis, led_bdis_node)
		led_bdev_unlink(led, link, link->bdi, false);

	hlist_del(&led->leds_node);
	kfree(led);

	mutex_unlock(&led_bdev_mutex);
}

/**
 * led_bdev_bdi_release() - BDI device resource release function.
 * @dev:	The block device
 * @res:	The BDI
 *
 * Called when the block device with which a BDI is associated has been
 * removed.  Calls led_bdev_unlink() for each LED linked to the block device.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_bdi_release(struct device *const dev, void *const res)
{
	struct led_bdev_bdi *const bdi = res;
	struct led_bdev_link *link;
	struct hlist_node *next;

	mutex_lock(&led_bdev_mutex);

	hlist_for_each_entry_safe(link, next, &bdi->leds, bdi_leds_node)
		led_bdev_unlink(link->led, link, bdi, true);

	mutex_unlock(&led_bdev_mutex);
}

/**
 * led_bdev_process() - Check linked devices for activity and blink LEDs.
 * @work:	Delayed work (@led_bdev_work)
 *
 * Iterates through block devices linked to LEDs and calls
 * led_bdev_process_link() to check the device and (if appropriate) blink
 * the LED.
 *
 *   * If led_bdev_process_link() blinks an LED (returns ``true``), there is
 *     no need to check more devices associated with that LED.
 *   * A block device can be linked to multiple LEDs, so we use a "generation"
 *     that is incremented each time this function is called to ensure that
 *     we only check a block device once per call to this function.
 *
 * When finished, schedules itself to run again after @led_bdev_interval
 * jiffies.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_process(struct work_struct *const work)
{
	static unsigned int generation;

	struct led_bdev_led *led;
	struct led_bdev_link *link;
	unsigned long delay;

	if (!mutex_trylock(&led_bdev_mutex))
		goto exit_reschedule;

	hlist_for_each_entry(led, &led_bdev_leds, leds_node) {

		hlist_for_each_entry(link, &led->bdis, led_bdis_node) {

			if (led_bdev_process_link(led, link, generation))
				break;
		}
	}

	++generation;

	mutex_unlock(&led_bdev_mutex);

exit_reschedule:
	delay = READ_ONCE(led_bdev_interval);
	WARN_ON_ONCE(!schedule_delayed_work(&led_bdev_work, delay));
}

/**
 * led_bdev_process_link() - Check a linked block device for activity and blink
 *			     the linked LED if appropriate.
 * @led:	The LED
 * @link:	The link
 * @generation:	Identifies when the linked BDI was last updated
 *
 * Updates BDI's activity counters if BDI's generation doesn't match the
 * current value.  Blinks LED if the correct type of activity (based on
 * the LED's @mode) has occurred since the last check.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	``true`` if the LED was blinked, ``false`` if not.
 */
static bool led_bdev_process_link(const struct led_bdev_led *const led,
				  const struct led_bdev_link *const link,
				  const unsigned int generation)
{
	struct led_bdev_bdi *const bdi = link->bdi;

	/*
	 * If this block device is linked to multiple LEDs, it may have already
	 * been processed in this "generation".  No need to update its activity
	 * counters in that case.
	 */
	if (bdi->generation != generation) {

		unsigned long read_ios, write_ios;

		read_ios = part_stat_read(bdi->bdev, ios[STAT_READ]);

		write_ios = part_stat_read(bdi->bdev, ios[STAT_WRITE])
				+ part_stat_read(bdi->bdev, ios[STAT_DISCARD])
				+ part_stat_read(bdi->bdev, ios[STAT_FLUSH]);

		if (bdi->read_ios != read_ios) {
			bdi->read_act = true;
			bdi->read_ios = read_ios;
		} else {
			bdi->read_act = false;
		}

		if (bdi->write_ios != write_ios) {
			bdi->write_act = true;
			bdi->write_ios = write_ios;
		} else {
			bdi->write_act = false;
		}

		bdi->generation = generation;
	}

	if ((bdi->read_act && led->mode != LED_BDEV_MODE_WO)
			|| (bdi->write_act && led->mode != LED_BDEV_MODE_RO)) {

		unsigned long delay_on = READ_ONCE(led->blink_msec);
		unsigned long delay_off = 1;	/* 0 leaves LED turned on */

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
 * @count:	The number of bytes written to the attribute
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

/* Data for mode_show() & mode_store() */
static const struct {
	char	name[sizeof("write")];			/* longest name */
	char	show[sizeof("[read] write rw\n")];	/* all the same size */
} led_bdev_modes[] = {
	[LED_BDEV_MODE_RO] = {
		.name	= "read",
		.show	= "[read] write rw\n",
	},
	[LED_BDEV_MODE_WO] = {
		.name	= "write",
		.show	= "read [write] rw\n",
	},
	[LED_BDEV_MODE_RW] = {
		.name	= "rw",
		.show	= "read write [rw]\n",
	},
};

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
	const struct led_bdev_led *const led = led_trigger_get_drvdata(dev);

	return sprintf(buf, led_bdev_modes[READ_ONCE(led->mode)].show);
}

/**
 * mode_store() - ``mode`` device attribute store function.
 * @dev:	The LED device
 * @attr:	The ``mode`` attribute (@dev_attr_mode)
 * @buf:	The new value (as written to the sysfs attribute)
 * @count:	The number of bytes written to the attribute
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
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	enum led_bdev_mode mode;

	for (mode = LED_BDEV_MODE_RO; mode <= LED_BDEV_MODE_RW; ++mode) {

		if (sysfs_streq(led_bdev_modes[mode].name, buf)) {
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
 * @count:	The number of bytes written to the attribute
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
	&led_bdev_attr_group,	/* /sys/class/leds/<led>/... */
	&led_bdev_linked_devs,	/* /sys/class/leds/<led>/linked_devices/ */
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
		class_destroy(led_bdev_class);	/* removes the class attr */
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
MODULE_IMPORT_NS(LEDTRIG_BLKDEV);
