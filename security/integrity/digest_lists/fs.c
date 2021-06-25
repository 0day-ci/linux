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

#define HDR_ASCII_FMT \
	"actions: %d, version: %d, algo: %s, type: %d, modifiers: %d, count: %d, datalen: %d\n"

static struct dentry *digest_lists_dir;
static struct dentry *digest_lists_loaded_dir;
static struct dentry *digest_list_add_dentry;
static struct dentry *digest_list_del_dentry;
char digest_label[NAME_MAX + 1];

static int parse_digest_list_filename(const char *digest_list_filename,
				      u8 *digest, enum hash_algo *algo)
{
	int i;

	for (i = 0; i < HASH_ALGO__LAST; i++)
		if (!strncmp(digest_list_filename, hash_algo_name[i],
			     strlen(hash_algo_name[i])))
			break;

	if (i == HASH_ALGO__LAST)
		return -ENOENT;

	*algo = i;
	return hex2bin(digest, strchr(digest_list_filename, '-') + 1,
		       hash_digest_size[*algo]);
}

/* returns pointer to hlist_node */
static void *digest_list_start(struct seq_file *m, loff_t *pos)
{
	struct digest_item *d;
	u8 digest[IMA_MAX_DIGEST_SIZE];
	enum hash_algo algo;
	struct compact_list_hdr *hdr;
	u32 count = 0;
	void *buf, *bufp, *bufendp;
	int ret;

	ret = parse_digest_list_filename(file_dentry(m->file)->d_name.name,
					 digest, &algo);
	if (ret < 0)
		return NULL;

	d = digest_lookup(digest, algo, COMPACT_DIGEST_LIST, NULL, NULL);
	if (!d)
		return NULL;

	bufp = buf = d->refs[0].digest_list->buf;
	bufendp = bufp + d->refs[0].digest_list->size;

	while (bufp < bufendp) {
		hdr = (struct compact_list_hdr *)bufp;
		count += hdr->count;
		bufp += sizeof(*hdr) + hdr->datalen;
	}

	return *pos <= count ? d : NULL;
}

static void *digest_list_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct digest_item *d = (struct digest_item *)v;
	struct compact_list_hdr *hdr;
	u32 count = 0;
	void *buf = d->refs[0].digest_list->buf;
	void *bufp = buf;
	void *bufendp = bufp + d->refs[0].digest_list->size;

	(*pos)++;

	while (bufp < bufendp) {
		hdr = (struct compact_list_hdr *)bufp;
		count += hdr->count;
		bufp += sizeof(*hdr) + hdr->datalen;
	}

	return *pos <= count ? d : NULL;
}

static void digest_list_stop(struct seq_file *m, void *v)
{
}

static void print_digest(struct seq_file *m, u8 *digest, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++)
		seq_printf(m, "%02x", *(digest + i));
}

static void digest_list_putc(struct seq_file *m, void *data, int datalen)
{
	while (datalen--)
		seq_putc(m, *(char *)data++);
}

static int digest_list_show_common(struct seq_file *m, void *v, bool binary)
{
	struct digest_item *d = (struct digest_item *)v;
	struct compact_list_hdr *hdr;
	u32 count = 0;
	void *buf = d->refs[0].digest_list->buf;
	void *bufp = buf;
	void *bufendp = bufp + d->refs[0].digest_list->size;

	while (bufp < bufendp) {
		hdr = (struct compact_list_hdr *)bufp;

		if (m->index >= count + hdr->count) {
			bufp += sizeof(*hdr) + hdr->datalen;
			count += hdr->count;
			continue;
		}

		if (count == m->index) {
			if (binary)
				digest_list_putc(m, (void *)hdr, sizeof(*hdr));
			else
				seq_printf(m, HDR_ASCII_FMT,
					   d->refs[0].digest_list->actions,
					   hdr->version,
					   hash_algo_name[hdr->algo], hdr->type,
					   hdr->modifiers, hdr->count,
					   hdr->datalen);
		}

		if (binary) {
			digest_list_putc(m, bufp + sizeof(*hdr) +
					 (m->index - count) *
					 hash_digest_size[hdr->algo],
					 hash_digest_size[hdr->algo]);
		} else {
			print_digest(m, bufp + sizeof(*hdr) +
				     (m->index - count) *
				     hash_digest_size[hdr->algo],
				     hash_digest_size[hdr->algo]);
			seq_puts(m, "\n");
		}

		break;
	}

	return 0;
}

static int digest_list_show(struct seq_file *m, void *v)
{
	return digest_list_show_common(m, v, true);
}

static int digest_list_ascii_show(struct seq_file *m, void *v)
{
	return digest_list_show_common(m, v, false);
}

static const struct seq_operations digest_list_seqops = {
	.start = digest_list_start,
	.next = digest_list_next,
	.stop = digest_list_stop,
	.show = digest_list_show
};

static int digest_list_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &digest_list_seqops);
}

static const struct file_operations digest_list_ops = {
	.open = digest_list_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static const struct seq_operations digest_list_ascii_seqops = {
	.start = digest_list_start,
	.next = digest_list_next,
	.stop = digest_list_stop,
	.show = digest_list_ascii_show
};

static int digest_list_ascii_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &digest_list_ascii_seqops);
}

static const struct file_operations digest_list_ascii_ops = {
	.open = digest_list_ascii_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int digest_list_get_secfs_files(char *label, u8 *digest,
				       enum hash_algo algo, enum ops op,
				       struct dentry **dentry,
				       struct dentry **dentry_ascii)
{
	char digest_list_filename[NAME_MAX + 1] = { 0 };
	u8 digest_str[IMA_MAX_DIGEST_SIZE * 2 + 1] = { 0 };
	char *dot, *label_ptr;

	label_ptr = strrchr(label, '/');
	if (label_ptr)
		label = label_ptr + 1;

	bin2hex(digest_str, digest, hash_digest_size[algo]);

	snprintf(digest_list_filename, sizeof(digest_list_filename),
		 "%s-%s-%s.ascii", hash_algo_name[algo], digest_str, label);

	dot = strrchr(digest_list_filename, '.');

	*dot = '\0';
	if (op == DIGEST_LIST_ADD)
		*dentry = securityfs_create_file(digest_list_filename, 0440,
						 digest_lists_loaded_dir, NULL,
						 &digest_list_ops);
	else
		*dentry = lookup_positive_unlocked(digest_list_filename,
						digest_lists_loaded_dir,
						strlen(digest_list_filename));
	*dot = '.';
	if (IS_ERR(*dentry))
		return PTR_ERR(*dentry);

	if (op == DIGEST_LIST_ADD)
		*dentry_ascii = securityfs_create_file(digest_list_filename,
						0440, digest_lists_loaded_dir,
						NULL, &digest_list_ascii_ops);
	else
		*dentry_ascii = lookup_positive_unlocked(digest_list_filename,
						digest_lists_loaded_dir,
						strlen(digest_list_filename));
	if (IS_ERR(*dentry_ascii)) {
		if (op == DIGEST_LIST_ADD)
			securityfs_remove(*dentry);

		return PTR_ERR(*dentry_ascii);
	}

	return 0;
}

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
	struct dentry *dentry, *dentry_ascii;
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

	rc = digest_list_get_secfs_files(path, digest, algo, op, &dentry,
					 &dentry_ascii);
	if (rc < 0)
		goto out_vfree;

	rc = digest_list_parse(size, data, op, actions, digest, algo,
			       dentry->d_name.name);
	if (rc < 0)
		pr_err("unable to upload digest list %s (%d)\n", path, rc);

	if ((rc < 0 && op == DIGEST_LIST_ADD) ||
	    (rc == size && op == DIGEST_LIST_DEL)) {
		/* Release reference taken in digest_list_get_secfs_files(). */
		if (op == DIGEST_LIST_DEL) {
			dput(dentry);
			dput(dentry_ascii);
		}

		securityfs_remove(dentry);
		securityfs_remove(dentry_ascii);
	}
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
	struct dentry *dentry = file_dentry(file), *dentry_ascii;
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

		result = digest_list_get_secfs_files(digest_label[0] != '\0' ?
						     digest_label : "parser",
						     digest, algo, op,
						     &dentry, &dentry_ascii);
		if (result < 0)
			goto out_kfree;

		memset(digest_label, 0, sizeof(digest_label));

		result = digest_list_parse(datalen, data, op, actions, digest,
					   algo, dentry->d_name.name);
		if (result != datalen) {
			pr_err("unable to upload generated digest list\n");
			result = -EINVAL;
		}

		if ((result < 0 && op == DIGEST_LIST_ADD) ||
		    (result == datalen && op == DIGEST_LIST_DEL)) {
			/*
			 * Release reference taken in
			 * digest_list_get_secfs_files().
			 */
			if (op == DIGEST_LIST_DEL) {
				dput(dentry);
				dput(dentry_ascii);
			}

			securityfs_remove(dentry);
			securityfs_remove(dentry_ascii);
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

static int __init digest_lists_fs_init(void)
{
	digest_lists_dir = securityfs_create_dir("digest_lists", integrity_dir);
	if (IS_ERR(digest_lists_dir))
		return -1;

	digest_lists_loaded_dir = securityfs_create_dir("digest_lists_loaded",
							digest_lists_dir);
	if (IS_ERR(digest_lists_loaded_dir))
		goto out;

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
	securityfs_remove(digest_lists_loaded_dir);
	securityfs_remove(digest_lists_dir);
	return -1;
}

late_initcall(digest_lists_fs_init);
