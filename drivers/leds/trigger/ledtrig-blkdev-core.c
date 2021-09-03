// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers - built-in components
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/leds.h>

#include "ledtrig-blkdev.h"

DEFINE_MUTEX(ledtrig_blkdev_mutex);
EXPORT_SYMBOL_NS_GPL(ledtrig_blkdev_mutex, LEDTRIG_BLKDEV);

/* Set when module is loaded (or trigger is initialized) */
void (*__ledtrig_blkdev_disk_cleanup)(struct gendisk *gd) = NULL;
EXPORT_SYMBOL_NS_GPL(__ledtrig_blkdev_disk_cleanup, LEDTRIG_BLKDEV);

/**
 * ledtrig_blkdev_disk_cleanup - remove a block device from the blkdev LED
 * trigger
 * @gd:	the disk to be removed
 */
void ledtrig_blkdev_disk_cleanup(struct gendisk *const gd)
{
	mutex_lock(&ledtrig_blkdev_mutex);

	if (gd->ledtrig != NULL) {
		if (!WARN_ON(__ledtrig_blkdev_disk_cleanup == NULL))
			__ledtrig_blkdev_disk_cleanup(gd);
	}

	mutex_unlock(&ledtrig_blkdev_mutex);
}


/*
 *
 *	ledtrig_blkdev_get_disk() - get a gendisk by name
 *
 *	Must be built in for access to block_class and disk_type
 *	Caller must call put_disk()
 *
 */

/* Non-null-terminated character sequence of known length */
struct ledtrig_blkdev_gdname {
	const char	*buf;
	size_t		len;
};

/* Match function for ledtrig_blkdev_get_disk() */
static int blkdev_match_gdname(struct device *const dev, const void *const data)
{
	const struct ledtrig_blkdev_gdname *const gdname = data;

	if (dev->type != &disk_type)
		return 0;

	return ledtrig_blkdev_streq(dev_to_disk(dev)->disk_name,
				    gdname->buf, gdname->len);
}

struct gendisk *ledtrig_blkdev_get_disk(const char *const name,
					const size_t len)
{
	const struct ledtrig_blkdev_gdname gdname = { .buf = name, .len = len };
	struct device *dev;

	dev = class_find_device(&block_class, NULL,
				&gdname, blkdev_match_gdname);
	if (dev == NULL)
		return NULL;

	return dev_to_disk(dev);
}
EXPORT_SYMBOL_NS_GPL(ledtrig_blkdev_get_disk, LEDTRIG_BLKDEV);
