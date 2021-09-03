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
extern struct gendisk *ledtrig_blkdev_get_disk(const char *name, size_t len);

/*
 * Compare a null-terminated C string with a non-null-terminated character
 * sequence of a known length.  Returns true (1) if equal, false (0) if not.
 */
static inline bool ledtrig_blkdev_streq(const char *const cstr,
					const char *const cbuf,
					const size_t buf_len)
{
	return strlen(cstr) == buf_len && memcmp(cstr, cbuf, buf_len) == 0;
}

#endif	/* __LEDTRIG_BLKDEV_H */
