// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe_module.h"

#include <linux/fs.h>
#include <linux/types.h>

static bool dvv_eval(const struct ipe_eval_ctx *ctx, const void *val)
{
	bool expect = (bool)val;
	bool eval = ctx->ipe_bdev && (!!ctx->ipe_bdev->sigdata);

	return expect == eval;
}

IPE_MODULE(dvv) = {
	.name = "dmverity_signature",
	.version = 1,
	.parse = ipe_bool_parse,
	.free = NULL,
	.eval = dvv_eval,
};
