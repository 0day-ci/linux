// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#include "ipe.h"
#include "fs.h"
#include "policy.h"

#include <linux/dcache.h>
#include <linux/security.h>

static struct dentry *np __ro_after_init;
static struct dentry *root __ro_after_init;
static struct dentry *config __ro_after_init;

/**
 * new_policy: Write handler for the securityfs node, "ipe/new_policy"
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Suppleis a buffer passed to the write syscall
 * @len: Supplies the length of @data
 * @offset: unused.
 *
 * Return:
 * >0 - Success, Length of buffer written
 * <0 - Error
 */
static ssize_t new_policy(struct file *f, const char __user *data,
			  size_t len, loff_t *offset)
{
	int rc = 0;
	char *copy = NULL;
	struct ipe_policy *p = NULL;
	struct ipe_context *ctx = NULL;

	if (!file_ns_capable(f, &init_user_ns, CAP_MAC_ADMIN))
		return -EPERM;

	ctx = ipe_current_ctx();

	copy = memdup_user_nul(data, len);
	if (IS_ERR(copy)) {
		rc = PTR_ERR(copy);
		goto err;
	}

	p = ipe_new_policy(NULL, 0, copy, len);
	if (IS_ERR(p)) {
		rc = PTR_ERR(p);
		goto err;
	}

	rc = ipe_new_policyfs_node(ctx, p);
	if (rc)
		goto err;

	ipe_add_policy(ctx, p);
err:
	ipe_put_policy(p);
	ipe_put_ctx(ctx);
	return (rc < 0) ? rc : len;
}

/**
 * get_config: Read handler for the securityfs node, "ipe/config"
 * @f: Supplies a file structure representing the securityfs node.
 * @data: Supplies a buffer passed to the read syscall
 * @len: Supplies the length of @data
 * @offset: unused.
 *
 * Return:
 * >0 - Success, Length of buffer written
 * <0 - Error
 */
static ssize_t get_config(struct file *f, char __user *data, size_t len,
			  loff_t *offset)
{
	int rc = 0;
	char *buf = NULL;
	size_t buflen = 0;
	char tmp[30] = { 0 };
	struct ipe_parser *p = NULL;
	struct ipe_module *m = NULL;

	for (p = __start_ipe_parsers; p < __end_ipe_parsers; ++p)
		buflen += snprintf(NULL, 0, "%s=%d\n", p->first_token, p->version);
	for (m = __start_ipe_modules; m < __end_ipe_modules; ++m)
		buflen += snprintf(NULL, 0, "%s=%d\n", m->name, m->version);

	++buflen;
	buf = kzalloc(buflen, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	for (p = __start_ipe_parsers; p < __end_ipe_parsers; ++p) {
		memset(tmp, 0x0, ARRAY_SIZE(tmp));
		scnprintf(tmp, ARRAY_SIZE(tmp), "%s=%d\n", p->first_token, p->version);
		strcat(buf, tmp);
	}

	for (m = __start_ipe_modules; m < __end_ipe_modules; ++m) {
		memset(tmp, 0x0, ARRAY_SIZE(tmp));
		scnprintf(tmp, ARRAY_SIZE(tmp), "%s=%d\n", m->name, m->version);
		strcat(buf, tmp);
	}

	rc = simple_read_from_buffer(data, len, offset, buf, buflen);
out:
	kfree(buf);
	return rc;
}

static const struct file_operations cfg_fops = {
	.read = get_config,
};

static const struct file_operations np_fops = {
	.write = new_policy,
};

/**
 * ipe_init_securityfs: Initialize IPE's securityfs tree at fsinit
 *
 * Return:
 * !0 - Error
 * 0 - OK
 */
static int __init ipe_init_securityfs(void)
{
	int rc = 0;
	struct ipe_context *ctx = NULL;

	ctx = ipe_current_ctx();

	root = securityfs_create_dir("ipe", NULL);
	if (IS_ERR(root)) {
		rc = PTR_ERR(root);
		goto err;
	}

	np = securityfs_create_file("new_policy", 0200, root, NULL, &np_fops);
	if (IS_ERR(np)) {
		rc = PTR_ERR(np);
		goto err;
	}

	config = securityfs_create_file("config", 0400, root, NULL,
					&cfg_fops);
	if (IS_ERR(config)) {
		rc = PTR_ERR(config);
		goto err;
	}

	ctx->policy_root = securityfs_create_dir("policies", root);
	if (IS_ERR(ctx->policy_root)) {
		rc = PTR_ERR(ctx->policy_root);
		goto err;
	}

	return 0;
err:
	securityfs_remove(np);
	securityfs_remove(root);
	securityfs_remove(config);
	securityfs_remove(ctx->policy_root);
	return rc;
}

fs_initcall(ipe_init_securityfs);
