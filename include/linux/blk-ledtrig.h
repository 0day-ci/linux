/* SPDX-License-Identifier: GPL-2.0-only */

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#ifndef _LINUX_BLK_LEDTRIG_H
#define _LINUX_BLK_LEDTRIG_H

#ifdef CONFIG_BLK_LED_TRIGGERS

int blk_ledtrig_create(const char *name);
int blk_ledtrig_delete(const char *name);

#endif	// CONFIG_BLK_LED_TRIGGERS

#endif	// _LINUX_BLK_LEDTRIG_H
