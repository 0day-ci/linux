// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"

static struct fscache_volume *volume;

int __init erofs_fscache_init(void)
{
	volume = fscache_acquire_volume("erofs", NULL, NULL, 0);
	if (!volume)
		return -EINVAL;

	return 0;
}

void erofs_fscache_cleanup(void)
{
	fscache_relinquish_volume(volume, NULL, false);
}
