/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#ifndef IPE_EVAL_H
#define IPE_EVAL_H

#include <linux/file.h>
#include <linux/types.h>

#include "ctx.h"
#include "hooks.h"
#include "policy.h"

struct ipe_bdev {
	const u8       *sigdata;
	size_t		siglen;

	const u8       *hash;
	size_t		hashlen;
};

struct ipe_inode {
	const u8       *sigdata;
	size_t		siglen;

	const u8       *hash;
	size_t		hashlen;
};

struct ipe_eval_ctx {
	enum ipe_hook hook;
	enum ipe_operation op;

	const struct file *file;
	struct ipe_context *ci_ctx;

	const struct ipe_bdev *ipe_bdev;
	const struct ipe_inode *ipe_inode;

	bool from_init_sb;
};

enum ipe_match {
	ipe_match_rule = 0,
	ipe_match_table,
	ipe_match_global,
	ipe_match_max
};

int ipe_process_event(const struct file *file, enum ipe_operation op,
		      enum ipe_hook hook);

void ipe_invalidate_pinned_sb(const struct super_block *mnt_sb);

#endif /* IPE_EVAL_H */
