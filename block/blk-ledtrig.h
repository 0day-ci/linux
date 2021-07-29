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

#else	// CONFIG_BLK_LED_TRIGGERS

static inline void blk_ledtrig_init(void) {}

#endif	// CONFIG_BLK_LED_TRIGGERS

#endif	// _BLOCK_BLK_LEDTRIG_H
