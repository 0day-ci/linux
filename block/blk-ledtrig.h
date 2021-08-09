/* SPDX-License-Identifier: GPL-2.0-only */

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#ifndef _BLOCK_BLK_LEDTRIG_H
#define _BLOCK_BLK_LEDTRIG_H

#ifdef CONFIG_BLK_LED_TRIGGERS

static inline void blk_ledtrig_disk_init(struct gendisk *const disk)
{
	RCU_INIT_POINTER(disk->led, NULL);
}

void blk_ledtrig_dev_clear(struct gendisk *const disk);

#else	// CONFIG_BLK_LED_TRIGGERS

static inline void blk_ledtrig_disk_init(const struct gendisk *disk) {}
static inline void blk_ledtrig_dev_clear(const struct gendisk *disk) {}

#endif	// CONFIG_BLK_LED_TRIGGERS

#endif	// _BLOCK_BLK_LEDTRIG_H
