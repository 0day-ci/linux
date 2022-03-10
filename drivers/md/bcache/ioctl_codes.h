/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BCACHE_IOCTL_CODES_H__
#define __BCACHE_IOCTL_CODES_H__

#include <linux/ioctl.h>
#include <linux/types.h>

struct bch_register_device {
	const char *dev_name;
	size_t size;
	struct cache_sb *sb;
};

#define BCH_IOCTL_MAGIC (0xBC)

/** Start new cache instance, load cache or recover cache */
#define BCH_IOCTL_REGISTER_DEVICE	_IOWR(BCH_IOCTL_MAGIC, 1, struct bch_register_device)

#endif
