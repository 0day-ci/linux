// SPDX-License-Identifier: GPL-2.0
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
 * File: parser.c
 *      Functions to parse digest lists.
 */

#include <linux/vmalloc.h>
#include <linux/module.h>

#include "digest_lists.h"
#include "../integrity.h"

/**
 * digest_list_validate - validate format of digest list
 * @size: buffer size
 * @buf: buffer containing the digest list
 *
 * This function validates the format of the passed digest list.
 *
 * Returns 0 if the digest list was successfully validated, -EINVAL otherwise.
 */
static int digest_list_validate(loff_t size, void *buf)
{
	void *bufp = buf, *bufendp = buf + size;
	struct compact_list_hdr *hdr;
	size_t digest_len;

	while (bufp < bufendp) {
		if (bufp + sizeof(*hdr) > bufendp) {
			pr_err("invalid data\n");
			return -EINVAL;
		}

		hdr = bufp;

		if (hdr->version != 1) {
			pr_err("unsupported version\n");
			return -EINVAL;
		}

		hdr->type = le16_to_cpu(hdr->type);
		hdr->modifiers = le16_to_cpu(hdr->modifiers);
		hdr->algo = le16_to_cpu(hdr->algo);
		hdr->count = le32_to_cpu(hdr->count);
		hdr->datalen = le32_to_cpu(hdr->datalen);

		if (hdr->algo >= HASH_ALGO__LAST) {
			pr_err("invalid hash algorithm\n");
			return -EINVAL;
		}

		digest_len = hash_digest_size[hdr->algo];

		if (hdr->type >= COMPACT__LAST ||
		    hdr->type == COMPACT_DIGEST_LIST) {
			pr_err("invalid type %d\n", hdr->type);
			return -EINVAL;
		}

		bufp += sizeof(*hdr);

		if (hdr->datalen != hdr->count * digest_len ||
		    bufp + hdr->datalen > bufendp) {
			pr_err("invalid data\n");
			return -EINVAL;
		}

		bufp += hdr->count * digest_len;
	}

	return 0;
}

/**
 * _digest_list_parse - parse digest list and add/delete digests
 * @size: buffer size
 * @buf: buffer containing the digest list
 * @op: operation to be performed
 * @digest_list: digest list digests being added/deleted belong to
 *
 * This function parses the digest list and adds or delete the digests in the
 * found digest blocks.
 *
 * Returns the buffer size if all digests were successfully added or deleted,
 * the size of the already parsed buffer on error.
 */
static int _digest_list_parse(loff_t size, void *buf, enum ops op,
			      struct digest_list_item *digest_list)
{
	void *bufp = buf, *bufendp = buf + size;
	struct compact_list_hdr *hdr;
	struct digest_item *d;
	size_t digest_len;
	int i;

	while (bufp < bufendp) {
		if (bufp + sizeof(*hdr) > bufendp)
			break;

		hdr = bufp;
		bufp += sizeof(*hdr);

		digest_len = hash_digest_size[hdr->algo];

		for (i = 0; i < hdr->count && bufp + digest_len <= bufendp;
		     i++, bufp += digest_len) {
			switch (op) {
			case DIGEST_LIST_ADD:
				d = digest_add(bufp, hdr->algo, hdr->type,
					       digest_list, bufp - buf,
					       (void *)hdr - buf);
				break;
			case DIGEST_LIST_DEL:
				d = digest_del(bufp, hdr->algo, hdr->type,
					       digest_list, bufp - buf,
					       (void *)hdr - buf);
				break;
			default:
				break;
			}

			if (IS_ERR(d)) {
				pr_err("failed to %s a digest from %s\n",
				       (op == DIGEST_LIST_ADD) ?
				       "add" : "delete", digest_list->label);
				goto out;
			}
		}
	}
out:
	return bufp - buf;
}

/**
 * get_digest_list - get the digest list extracted digests will be associated to
 * @size: buffer size
 * @buf: buffer containing the digest list
 * @op: digest list operation
 * @actions: actions performed on the digest list being processed
 * @digest: digest of the digest list
 * @algo: digest algorithm
 * @label: label to identify the digest list (e.g. file name)
 *
 * This function retrieves the digest list item for the passed digest and
 * algorithm. If it is not found at addition time, this function creates a new
 * one.
 *
 * This function also ensures that the actions done at the time of deletion
 * match the actions done at the time of addition (it would guarantee that also
 * deletion is notified to remote verifiers).
 *
 * Returns the retrieved/created digest list item on success, an error pointer
 * otherwise.
 */
static struct digest_list_item *get_digest_list(loff_t size, void *buf,
						enum ops op, u8 actions,
						u8 *digest, enum hash_algo algo,
						const char *label)
{
	struct digest_item *d;
	struct digest_list_item *digest_list;
	int digest_len = hash_digest_size[algo];

	switch (op) {
	case DIGEST_LIST_ADD:
		/* Add digest list to be associated to each digest. */
		d = digest_list_add(digest, algo, size, buf, actions, label);
		if (IS_ERR(d))
			return (void *)d;

		digest_list = d->refs[0].digest_list;
		break;
	case DIGEST_LIST_DEL:
		/* Lookup digest list to delete the references. */
		d = digest_lookup(digest, algo, COMPACT_DIGEST_LIST, NULL,
				  NULL);
		if (!d) {
			print_hex_dump(KERN_ERR,
				       "digest list digest not found: ",
				       DUMP_PREFIX_NONE, digest_len, 1, digest,
				       digest_len, true);
			return ERR_PTR(-ENOENT);
		}

		digest_list = d->refs[0].digest_list;

		/* Reject deletion if actions on delete differ from add. */
		if (digest_list->actions != actions) {
			pr_err("actions mismatch, add: %d, del: %d\n",
			       digest_list->actions, actions);
			return ERR_PTR(-EPERM);
		}

		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return digest_list;
}

/**
 * digest_list_parse - parse a digest list
 * @size: buffer size
 * @buf: buffer containing the digest list
 * @op: digest list operation
 * @actions: actions performed on the digest list being processed
 * @digest: digest of the digest list
 * @algo: digest algorithm
 * @label: label to identify the digest list (e.g. file name)
 *
 * This function parses the passed digest list and executed the requested
 * operation. If the operation cannot be successfully executed, this function
 * performs a rollback to the previous state.
 *
 * Returns the buffer size on success, a negative value otherwise.
 */
int digest_list_parse(loff_t size, void *buf, enum ops op, u8 actions,
		      u8 *digest, enum hash_algo algo, const char *label)
{
	struct digest_item *d;
	struct digest_list_item *digest_list;
	enum ops rollback_op = (op == DIGEST_LIST_ADD) ?
			       DIGEST_LIST_DEL : DIGEST_LIST_ADD;
	int ret, rollback_size;

	ret = digest_list_validate(size, buf);
	if (ret < 0)
		return ret;

	digest_list = get_digest_list(size, buf, op, actions, digest, algo,
				      label);
	if (IS_ERR(digest_list))
		return PTR_ERR(digest_list);

	ret = _digest_list_parse(size, buf, op, digest_list);
	if (ret < 0)
		goto out;

	if (ret != size) {
		rollback_size = ret;

		ret = _digest_list_parse(rollback_size, buf, rollback_op,
					 digest_list);
		if (ret != rollback_size)
			pr_err("rollback failed\n");

		ret = -EINVAL;
	}
out:
	/* Delete digest list on unsuccessful add or successful delete. */
	if ((op == DIGEST_LIST_ADD && ret < 0) ||
	    (op == DIGEST_LIST_DEL && ret == size)) {
		d = digest_list_del(digest, algo, actions, digest_list);
		if (IS_ERR(d))
			return PTR_ERR(d);
	}

	return ret;
}
