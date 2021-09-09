// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED trigger - built-in components
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/leds.h>

#include "ledtrig-blkdev.h"

/**
 * ledtrig_blkdev_disk_cleanup - remove a block device from the blkdev LED
 * trigger
 * @gd:	the disk to be removed
 */
void ledtrig_blkdev_disk_cleanup(struct gendisk *const gd)
{
}
