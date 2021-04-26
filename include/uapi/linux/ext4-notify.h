/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2021, Collabora Ltd.
 */

#ifndef EXT4_NOTIFY_H
#define EXT4_NOTIFY_H

#define EXT4_FSN_DESC_LEN	256

struct ext4_error_inode_report {
	u64 inode;
	u64 block;
	char desc[EXT4_FSN_DESC_LEN];
};

#endif
