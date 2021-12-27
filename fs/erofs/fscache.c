// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"

static struct fscache_volume *volume;

static int erofs_fscache_init_cookie(struct erofs_cookie_ctx *ctx, char *path)
{
	struct fscache_cookie *cookie;

	/*
	 * @object_size shall be non-zero to avoid
	 * FSCACHE_COOKIE_NO_DATA_TO_READ.
	 */
	cookie = fscache_acquire_cookie(volume, 0,
					path, strlen(path),
					NULL, 0, -1);
	if (!cookie)
		return -EINVAL;

	fscache_use_cookie(cookie, false);
	ctx->cookie = cookie;
	return 0;
}

static void erofs_fscache_cleanup_cookie(struct erofs_cookie_ctx *ctx)
{
	struct fscache_cookie *cookie = ctx->cookie;

	fscache_unuse_cookie(cookie, NULL, NULL);
	fscache_relinquish_cookie(cookie, false);
	ctx->cookie = NULL;
}

static int erofs_fscahce_init_ctx(struct erofs_cookie_ctx *ctx,
				  struct super_block *sb, char *path)
{
	int ret;

	ret = erofs_fscache_init_cookie(ctx, path);
	if (ret) {
		erofs_err(sb, "failed to init cookie\n");
		return ret;
	}

	return 0;
}

static void erofs_fscache_cleanup_ctx(struct erofs_cookie_ctx *ctx)
{
	erofs_fscache_cleanup_cookie(ctx);
}

struct erofs_cookie_ctx *erofs_fscache_get_ctx(struct super_block *sb,
					       char *path)
{
	struct erofs_cookie_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ret = erofs_fscahce_init_ctx(ctx, sb, path);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	return ctx;
}

void erofs_fscache_put_ctx(struct erofs_cookie_ctx *ctx)
{
	if (!ctx)
		return;

	erofs_fscache_cleanup_ctx(ctx);
	kfree(ctx);
}

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
