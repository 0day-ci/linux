// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED trigger - built-in components
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
