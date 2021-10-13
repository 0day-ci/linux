// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe_module.h"

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/audit.h>
#include <linux/mount.h>

static bool evaluate(const struct ipe_eval_ctx *ctx, const void *value)
{
	bool expect = (bool)value;

	if (!ctx->file)
		return false;

	if (!IS_VERITY(ctx->file->f_inode) || !ctx->ipe_inode)
		return false;

	return (!!ctx->ipe_inode->sigdata) == expect;
}

IPE_MODULE(fsvs) = {
	.name = "fsverity_signature",
	.version = 1,
	.parse = ipe_bool_parse,
	.free = NULL,
	.eval = evaluate,
};
