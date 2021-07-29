/* SPDX-License-Identifier: GPL-2.0-only */

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#ifndef _BLOCK_BLK_LEDTRIG_H
#define _BLOCK_BLK_LEDTRIG_H

#ifdef CONFIG_BLK_LED_TRIGGERS

void blk_ledtrig_init(void);

static inline void blk_ledtrig_disk_init(struct gendisk *const gd)
{
	gd->ledtrig = NULL;
	mutex_init(&gd->ledtrig_mutex);
}

#else	// CONFIG_BLK_LED_TRIGGERS

static inline void blk_ledtrig_init(void) {}
static inline void blk_ledtrig_disk_init(const struct gendisk *gd) {}

// Real function (declared in include/linux/blk-ledtrig.h) returns a bool.
// This is only here for del_gendisk() (in genhd.c), which doesn't check
// the return value.
static inline void blk_ledtrig_clear(const struct gendisk *gd) {}

#endif	// CONFIG_BLK_LED_TRIGGERS

#endif	// _BLOCK_BLK_LEDTRIG_H
