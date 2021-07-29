/* SPDX-License-Identifier: GPL-2.0-only */

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#ifndef _LINUX_BLK_LEDTRIG_H
#define _LINUX_BLK_LEDTRIG_H

#ifdef CONFIG_BLK_LED_TRIGGERS

#include <linux/genhd.h>
#include <linux/types.h>

int blk_ledtrig_create(const char *name);
int blk_ledtrig_delete(const char *name);
int blk_ledtrig_set(struct gendisk *const gd, const char *const name);
bool blk_ledtrig_clear(struct gendisk *const gd);

#endif	// CONFIG_BLK_LED_TRIGGERS

#endif	// _LINUX_BLK_LEDTRIG_H
