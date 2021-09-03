/* SPDX-License-Identifier: GPL-2.0-only */

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#ifndef __LEDTRIG_BLKDEV_H
#define __LEDTRIG_BLKDEV_H

extern struct mutex ledtrig_blkdev_mutex;
extern void (*__ledtrig_blkdev_disk_cleanup)(struct gendisk *gd);

#endif	/* __LEDTRIG_BLKDEV_H */
