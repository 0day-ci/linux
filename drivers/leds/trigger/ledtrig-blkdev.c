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

/* Every LED associated with the blkdev trigger gets one of these */
struct led_bdev_led {
	struct led_classdev	*led_cdev;
	unsigned int		blink_msec;
	struct hlist_head	bdis;
	struct hlist_node	leds_node;
	enum led_bdev_mode	mode;
};

/* Forward declarations to make this file compile in a reasonable order */
static void led_bdev_process(struct work_struct *work);
static struct led_bdev_bdi *led_bdev_get_bdi(const char *buf, size_t size);
static struct block_device *led_bdev_get(const char *buf, size_t size,
					 fmode_t mode);
static int led_bdev_link(struct led_bdev_led *led, struct led_bdev_bdi *bdi);
static void led_bdev_put_bdi(struct led_bdev_bdi *bdi);
static void led_bdev_bdi_release(struct device *dev, void *res);
static void led_bdev_unlink(struct led_bdev_led *led,
			    struct led_bdev_link *link,
			    struct led_bdev_bdi *bdi, bool releasing);
static void led_bdev_update_bdi(struct led_bdev_bdi *bdi);
static bool led_bdev_blink(const struct led_bdev_led *led,
			   const struct led_bdev_bdi *bdi);

/* Protects everything except atomic sysfs attributes */
static DEFINE_MUTEX(led_bdev_mutex);

/* All LEDs associated with the trigger */
static HLIST_HEAD(led_bdev_leds);

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
 * @led_bdev_leds list, and sets the LED's trigger data.
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
 * @buf:	The value written to the attribute, which should be the path to
 *		the special file that represents the block device to be linked
 *		to the LED (e.g. /dev/sda)
 * @count:	The number of characters in @buf
 *
 * Calls led_bdev_get_bdi() to find or create the BDI for the block device,
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
	const struct led_bdev_link *link;
	struct led_bdev_bdi *bdi;
	int ret;

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_return;

	bdi = led_bdev_get_bdi(buf, count);
	if (IS_ERR(bdi)) {
		ret = PTR_ERR(bdi);
		goto exit_unlock;
	}

	hlist_for_each_entry (link, &bdi->leds, bdi_leds_node) {
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
 * @buf:	The value written to the ``link_device`` attribute, which should
 *		be the path to a special file that represents a block device
 * @count:	The number of characters in @buf
 *
 * Calls led_bdev_get() to get the block device represented by the path in @buf.
 * If the device already has a BDI (because it is already linked to an LED),
 * simply returns the existing BDI.
 *
 * Otherwise, allocates a new BDI (as a device resource), creates the block
 * device's ``linked_leds`` directory (attribute group), calls
 * led_bdev_update_bdi() to set the BDI's activity counters, and adds the BDI
 * resource to the block device.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	Pointer to the BDI, error pointer on error.
 */
static struct led_bdev_bdi *led_bdev_get_bdi(const char *const buf,
					     const size_t count)
{
	struct block_device *bdev;
	struct led_bdev_bdi *bdi;
	struct device *dev;
	int ret;

	bdev = led_bdev_get(buf, count, 0);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto exit_return;
	}

	dev = &bdev->bd_device;

	bdi = devres_find(dev, led_bdev_bdi_release, NULL, NULL);
	if (bdi != NULL) {
		ret = 0;
		goto exit_put_bdev;
	}

	bdi = devres_alloc(led_bdev_bdi_release, sizeof(*bdi), GFP_KERNEL);
	if (bdi == NULL) {
		ret = -ENOMEM;
		goto exit_put_bdev;
	}

	ret = sysfs_create_group(&dev->kobj, &led_bdev_linked_leds);
	if (ret != 0)
		goto exit_free_bdi;

	bdi->bdev = bdev;
	INIT_HLIST_HEAD(&bdi->leds);

	/* Ensure that led_bdev_update_bdi() updates BDI */
	bdi->generation = led_bdev_generation - 1;
	led_bdev_update_bdi(bdi);
	/* Don't blink for activity that may have happened long ago */
	bdi->read_act = false;
	bdi->write_act = false;

	devres_add(dev, bdi);

exit_free_bdi:
	if (ret != 0)
		devres_free(bdi);
exit_put_bdev:
	blkdev_put(bdev, 0);  /* Allow the device to be released */
exit_return:
	return (ret == 0) ? bdi : ERR_PTR(ret);
}

/**
 * led_bdev_update_bdi() - Update a BDI's activity counters for the current
 *			   generation
 * @bdi:	The BDI
 *
 * Does nothing if the BDI has already been updated during this generation.
 *
 * Otherwise, updates the BDI's activity counters (@read_ios & @write_ios) and
 * @generation, and sets @read_act and/or @write_act if the corresponding
 * counters have changed.
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 */
static void led_bdev_update_bdi(struct led_bdev_bdi *const bdi)
{
	unsigned long read_ios, write_ios;

	if (bdi->generation != led_bdev_generation) {

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

		bdi->generation = led_bdev_generation;
	}
}

/**
 * led_bdev_get_bdev() - Get a block device by path, prepending "/dev/" if
 *			 needed.
 * @buf:	The value written to the ``link_device`` or ``unlink_device``
 *		attribute, which should be the path to a special file that
 *		represents a block device
 * @count:	The number of characters in @buf (not including its terminating
 *		null)
 *
 * This function is a wrapper around blkdev_get_by_path().  It first tries the
 * path that was written to the sysfs attribute (after removing trailing
 * whitespace).  If the first attempt returns -ENOENT, it tries again with
 * "/dev/" prepended to the path.
 *
 * The caller must call blkdev_put() when finished with the device.
 *
 * Context:	Process context.
 * Return:	The block device, or an error pointer.
 */
static struct block_device *led_bdev_get(const char *const buf,
					 const size_t count, fmode_t mode)
{
	static const char dev[] = "/dev/";
	struct block_device *bdev;
	char *dev_path, *path;

	/* sizeof(dev) includes terminating null */
	dev_path = kmalloc(sizeof(dev) + count, GFP_KERNEL);
	if (dev_path == NULL)
		return ERR_PTR(-ENOMEM);

	/* sizeof(dev) - 1 is compile-time equivalent of strlen(dev) */
	memcpy(dev_path, dev, sizeof(dev) - 1);
	path = dev_path + sizeof(dev) - 1;
	memcpy(path, buf, count + 1);  /* include terminating null */
	strim(path);

try_blkdev_get:
	bdev = blkdev_get_by_path(path, mode, THIS_MODULE);
	if (IS_ERR(bdev) && PTR_ERR(bdev) == -ENOENT && path != dev_path) {
		path = dev_path;
		goto try_blkdev_get;
	}

	kfree(dev_path);
	return bdev;
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

	/* /sys/class/block/<bdev>/linked_leds/<led> */
	ret = sysfs_add_link_to_group(bdev_kobj(bdi->bdev),
				      led_bdev_linked_leds.name,
				      &led->led_cdev->dev->kobj,
				      led->led_cdev->name);
	if (ret != 0)
		goto error_free_link;

	/* /sys/class/leds/<led>/linked_devices/<bdev> */
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
 * If the device has a BDI, searches the BDI's list of LEDs for a link to this
 * LED and (if found) calls led_bdev_unlink() to destroy the link.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 * Return:	@count on success, ``-errno`` on error.
 */
static ssize_t unlink_device_store(struct device *const dev,
				   struct device_attribute *const attr,
				   const char *buf, const size_t count)
{
	struct led_bdev_led *const led = led_trigger_get_drvdata(dev);
	struct block_device *bdev;
	struct led_bdev_bdi *bdi;
	struct led_bdev_link *link;
	int ret;

	bdev = led_bdev_get(buf, count, 0);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto exit_return;
	}

	bdi = devres_find(&bdev->bd_device, led_bdev_bdi_release, NULL, NULL);
	if (bdi == NULL) {
		ret = -EUNATCH;
		goto exit_put_bdev;
	}

	ret = mutex_lock_interruptible(&led_bdev_mutex);
	if (ret != 0)
		goto exit_put_bdev;

	hlist_for_each_entry(link, &bdi->leds, bdi_leds_node) {

		if (link->led == led) {
			led_bdev_unlink(led, link, bdi, false);
			goto exit_unlock;
		}
	}

	ret = -EUNATCH;

exit_unlock:
	mutex_unlock(&led_bdev_mutex);
exit_put_bdev:
	blkdev_put(bdev, 0);
exit_return:
	return (ret == 0) ? count : ret;
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
 * device/LED links after its removal), cancels the periodic delayed work
 * which checks for device activity.
 *
 * This function is called from multiple locations.
 *
 *   * unlink_device_store() calls this function (with @releasing == ``false``)
 *     when a block device is unlinked from an LED via the ``unlink_device``
 *     sysfs attribute.
 *   * led_bdev_deactivate() calls this function (with @releasing == ``false``)
 *     for each block device linked to an LED that is being deactivated
 *     (disassociated from the trigger).
 *   * led_bdev_bdi_release() calls this function (with @releasing == ``true``)
 *     for each LED link to a block device that has been removed from the
 *     system.
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

	/* /sys/class/leds/<led>/linked_devices/<bdev> */
	sysfs_remove_link_from_group(&led->led_cdev->dev->kobj,
				     led_bdev_linked_devs.name,
				     dev_name(&bdi->bdev->bd_device));

	/*
	 * If the BDI is being released, the device's attribute groups have
	 * already been removed, and the BDI will be freed automatically.
	 */
	if (!releasing) {

		/* /sys/class/block/<bdev>/linked_leds/<led> */
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
 * If the BDI is no longer linked to any LEDs, removes the block device's
 * ``linked_leds`` directory (attribute group), removes the BDI from the
 * block device's resource list, and frees the BDI.
 *
 * Called from led_bdev_unlink() (and in the link_device_store() error path).
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

	hlist_for_each_entry_safe (link, next, &led->bdis, led_bdis_node)
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
 * Called by the driver core when a block device with a BDI is removed from the
 * system.  Calls led_bdev_unlink() for each LED linked to the block device.
 *
 * Context:	Process context.  Takes and releases @led_bdev_mutex.
 */
static void led_bdev_bdi_release(struct device *const dev, void *const res)
{
	struct led_bdev_bdi *const bdi = res;
	struct led_bdev_link *link;
	struct hlist_node *next;

	mutex_lock(&led_bdev_mutex);

	hlist_for_each_entry_safe (link, next, &bdi->leds, bdi_leds_node)
		led_bdev_unlink(link->led, link, bdi, true);

	mutex_unlock(&led_bdev_mutex);
}

/**
 * led_bdev_process() - Check linked devices for activity and blink LEDs.
 * @work:	Delayed work (@led_bdev_work)
 *
 * Iterates through block devices linked to LEDs and calls led_bdev_update_bdi()
 * to update the BDI's activity counters.  If a particular LED hasn't already
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
	struct led_bdev_link *link;
	unsigned long delay;

	if (!mutex_trylock(&led_bdev_mutex))
		goto exit_reschedule;

	hlist_for_each_entry (led, &led_bdev_leds, leds_node) {

		bool blinked = false;

		hlist_for_each_entry (link, &led->bdis, led_bdis_node) {

			led_bdev_update_bdi(link->bdi);
			if (!blinked)
				blinked = led_bdev_blink(led, link->bdi);
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
 * @bdi:	The block device
 *
 * Context:	Process context.  Caller must hold @led_bdev_mutex.
 * Return:	``true`` if the LED is blinked, ``false`` if not.
 */
static bool led_bdev_blink(const struct led_bdev_led *const led,
			   const struct led_bdev_bdi *const bdi)
{
	unsigned long delay_on, delay_off;

	if ((bdi->read_act && led->mode != LED_BDEV_MODE_WO)
			|| (bdi->write_act && led->mode != LED_BDEV_MODE_RO)) {

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

/* Data for mode_show() & mode_store() */
static const struct {
	char	name[sizeof("write")];		    /* longest name */
	char	show[sizeof("[read] write rw\n")];  /* all the same size */
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
