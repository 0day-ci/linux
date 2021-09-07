/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2021 Cisco Systems
 *
 * Author: Stefan Schaeckeler
 */

#ifndef __UBIFS_SYSFS_H__
#define __UBIFS_SYSFS_H__

struct ubifs_info;

/*
 * The UBIFS sysfs directory name pattern and maximum name length (3 for "ubi"
 * + 1 for "_" and plus 2x2 for 2 UBI numbers and 1 for the trailing zero byte.
 */
#define UBIFS_DFS_DIR_NAME "ubi%d_%d"
#define UBIFS_DFS_DIR_LEN  (3 + 1 + 2*2 + 1)

/**
 * ubifs_stats_info - per-FS statistics information.
 * @magic_errors: number of bad magic numbers (will be reset with a new mount).
 * @node_errors: number of bad nodes (will be reset with a new mount).
 * @crc_errors: number of bad crcs (will be reset with a new mount).
 */
struct ubifs_stats_info {
	unsigned int magic_errors;
	unsigned int node_errors;
	unsigned int crc_errors;
};

int ubifs_sysfs_init(void);
void ubifs_sysfs_exit(void);
int ubifs_sysfs_register(struct ubifs_info *c);
void ubifs_sysfs_unregister(struct ubifs_info *c);

#endif
