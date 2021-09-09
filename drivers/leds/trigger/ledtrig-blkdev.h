/* SPDX-License-Identifier: GPL-2.0-only */

/*
 *	Block device LED trigger
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#ifndef __LEDTRIG_BLKDEV_H
#define __LEDTRIG_BLKDEV_H

extern struct mutex ledtrig_blkdev_mutex;
extern void (*__ledtrig_blkdev_disk_cleanup)(struct gendisk *gd);

/* Caller must call put_disk() */
struct gendisk *ledtrig_blkdev_get_disk(const char *const name);

#endif	/* __LEDTRIG_BLKDEV_H */
