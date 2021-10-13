// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe_module.h"

#include <linux/fs.h>
#include <linux/types.h>

struct counted_array {
	size_t	len;
	u8     *data;
};

static int parse(const char *valstr, void **value)
{
	int rv = 0;
	struct counted_array *arr;

	arr = kzalloc(sizeof(*arr), GFP_KERNEL);
	if (!arr) {
		rv = -ENOMEM;
		goto err;
	}

	arr->len = (strlen(valstr) / 2);

	arr->data = kzalloc(arr->len, GFP_KERNEL);
	if (!arr->data) {
		rv = -ENOMEM;
		goto err;
	}

	rv = hex2bin(arr->data, valstr, arr->len);
	if (rv != 0)
		goto err2;

	*value = arr;
	return rv;
err2:
	kfree(arr->data);
err:
	kfree(arr);
	return rv;
}

static bool evaluate(const struct ipe_eval_ctx *ctx, const void *val)
{
	const u8 *src;
	struct counted_array *expect = (struct counted_array *)val;

	if (!ctx->ipe_inode)
		return false;

	if (ctx->ipe_inode->hashlen != expect->len)
		return false;

	src = ctx->ipe_inode->hash;

	return !memcmp(expect->data, src, expect->len);
}

static int free_value(void **val)
{
	struct counted_array *expect = (struct counted_array *)val;

	kfree(expect->data);
	kfree(expect);

	return 0;
}

IPE_MODULE(fsv_digest) = {
	.name = "fsverity_digest",
	.version = 1,
	.parse = parse,
	.free = free_value,
	.eval = evaluate,
};
