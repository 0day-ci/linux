// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 * Copyright (C) 2017-2021 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: fs.c
 *      Functions for the interfaces exposed in securityfs.
 */


#include <linux/fcntl.h>
#include <linux/kernel_read_file.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/parser.h>
#include <linux/vmalloc.h>
#include <linux/namei.h>
#include <linux/ima.h>

#include "digest_lists.h"

static struct dentry *digest_lists_dir;
static struct dentry *digest_list_add_dentry;
static struct dentry *digest_list_del_dentry;

static ssize_t digest_list_read(char *path, enum ops op)
{
	void *data = NULL;
	char *datap;
	size_t size;
	u8 actions = 0;
	bool measured = false;
	struct file *file;
	u8 digest[IMA_MAX_DIGEST_SIZE] = { 0 };
	enum hash_algo algo = HASH_ALGO__LAST;
	int rc, pathlen = strlen(path);

	/* remove \n */
	datap = path;
	strsep(&datap, "\n");

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("unable to open file: %s (%ld)", path, PTR_ERR(file));
		return PTR_ERR(file);
	}

	rc = kernel_read_file(file, 0, &data, INT_MAX, NULL,
			      READING_DIGEST_LIST);
	if (rc < 0) {
		pr_err("unable to read file: %s (%d)", path, rc);
		goto out;
	}

	size = rc;

	ima_measure_critical_data("digest_lists", "file_upload", data, size,
				  false, digest, &algo, &measured);
	if (algo == HASH_ALGO__LAST) {
		rc = -EINVAL;
		goto out_vfree;
	}

	if (measured)
		actions |= COMPACT_ACTION_IMA_MEASURED;

	rc = digest_list_parse(size, data, op, actions, digest, algo, "");
	if (rc < 0)
		pr_err("unable to upload digest list %s (%d)\n", path, rc);
out_vfree:
	vfree(data);
out:
	fput(file);

	if (rc < 0)
		return rc;

	return pathlen;
}

static ssize_t digest_list_write(struct file *file, const char __user *buf,
				 size_t datalen, loff_t *ppos)
{
	char *data;
	ssize_t result;
	enum ops op = DIGEST_LIST_ADD;
	struct dentry *dentry = file_dentry(file);
	u8 digest[IMA_MAX_DIGEST_SIZE];
	enum hash_algo algo = HASH_ALGO__LAST;
	u8 actions = 0;
	bool measured = false;

	/* No partial writes. */
	result = -EINVAL;
	if (*ppos != 0)
		goto out;

	result = -EFBIG;
	if (datalen > 64 * 1024 * 1024 - 1)
		goto out;

	data = memdup_user_nul(buf, datalen);
	if (IS_ERR(data)) {
		result = PTR_ERR(data);
		goto out;
	}

	if (dentry == digest_list_del_dentry)
		op = DIGEST_LIST_DEL;

	result = -EPERM;

	if (data[0] == '/') {
		result = digest_list_read(data, op);
	} else {
		ima_measure_critical_data("digest_lists", "buffer_upload", data,
					  datalen, false, digest, &algo,
					  &measured);
		if (algo == HASH_ALGO__LAST) {
			pr_err("failed to calculate buffer digest\n");
			result = -EINVAL;
			goto out_kfree;
		}

		if (measured)
			actions |= COMPACT_ACTION_IMA_MEASURED;

		result = digest_list_parse(datalen, data, op, actions, digest,
					   algo, "");
		if (result != datalen) {
			pr_err("unable to upload generated digest list\n");
			result = -EINVAL;
		}
	}
out_kfree:
	kfree(data);
out:
	return result;
}

static unsigned long flags;

/*
 * digest_list_open: sequentialize access to the add/del files
 */
static int digest_list_open(struct inode *inode, struct file *filp)
{
	if ((filp->f_flags & O_ACCMODE) != O_WRONLY)
		return -EACCES;

	if (test_and_set_bit(0, &flags))
		return -EBUSY;

	return 0;
}

/*
 * digest_list_release - release the add/del files
 */
static int digest_list_release(struct inode *inode, struct file *file)
{
	clear_bit(0, &flags);
	return 0;
}

static const struct file_operations digest_list_upload_ops = {
	.open = digest_list_open,
	.write = digest_list_write,
	.read = seq_read,
	.release = digest_list_release,
	.llseek = generic_file_llseek,
};

int __init digest_lists_fs_init(void)
{
	digest_lists_dir = securityfs_create_dir("digest_lists", integrity_dir);
	if (IS_ERR(digest_lists_dir))
		return -1;

	digest_list_add_dentry = securityfs_create_file("digest_list_add", 0200,
						digest_lists_dir, NULL,
						&digest_list_upload_ops);
	if (IS_ERR(digest_list_add_dentry))
		goto out;

	digest_list_del_dentry = securityfs_create_file("digest_list_del", 0200,
						digest_lists_dir, NULL,
						&digest_list_upload_ops);
	if (IS_ERR(digest_list_del_dentry))
		goto out;

	return 0;
out:
	securityfs_remove(digest_list_del_dentry);
	securityfs_remove(digest_list_add_dentry);
	securityfs_remove(digest_lists_dir);
	return -1;
}

late_initcall(digest_lists_fs_init);
