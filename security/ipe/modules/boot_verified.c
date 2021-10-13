// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe_module.h"

#include <linux/fs.h>
#include <linux/types.h>

static bool bv_eval(const struct ipe_eval_ctx *ctx, const void *val)
{
	bool expect = (bool)val;

	return expect == ctx->from_init_sb;
}

IPE_MODULE(bv) = {
	.name = "boot_verified",
	.version = 1,
	.parse = ipe_bool_parse,
	.free = NULL,
	.eval = bv_eval,
};
