/* SPDX-License-Identifier: GPL-2.0 */
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
 * File: digest_lists.h
 *      Unexported definitions for digest lists.
 */

#ifndef __DIGEST_LISTS_INTERNAL_H
#define __DIGEST_LISTS_INTERNAL_H

#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/hash.h>
#include <linux/tpm.h>
#include <linux/audit.h>
#include <crypto/hash_info.h>
#include <linux/hash_info.h>
#include <uapi/linux/digest_lists.h>

#define MAX_DIGEST_SIZE	64
#define HASH_BITS 10
#define MEASURE_HTABLE_SIZE (1 << HASH_BITS)

struct digest_list_item {
	loff_t size;
	u8 *buf;
	u8 actions;
	u8 digest[64];
	enum hash_algo algo;
	const char *label;
};

struct digest_list_item_ref {
	struct digest_list_item *digest_list;
	loff_t digest_offset;
	loff_t hdr_offset;
};

struct digest_item {
	/* hash table pointers */
	struct hlist_node hnext;
	/* digest list references (protected by RCU) */
	struct digest_list_item_ref *refs;
};

struct h_table {
	atomic_long_t len;
	struct hlist_head queue[MEASURE_HTABLE_SIZE];
};

static inline unsigned int hash_key(u8 *digest)
{
	return (digest[0] | digest[1] << 8) % MEASURE_HTABLE_SIZE;
}

extern struct h_table htable[COMPACT__LAST];

static inline struct compact_list_hdr *get_hdr(
					struct digest_list_item *digest_list,
					loff_t hdr_offset)
{
	return (struct compact_list_hdr *)(digest_list->buf + hdr_offset);
}

static inline enum hash_algo get_algo(struct digest_list_item *digest_list,
				      loff_t digest_offset, loff_t hdr_offset)
{
	/* Digest list digest algorithm is stored in a different place. */
	if (!digest_offset)
		return digest_list->algo;

	return get_hdr(digest_list, hdr_offset)->algo;
}

static inline u8 *get_digest(struct digest_list_item *digest_list,
			     loff_t digest_offset, loff_t hdr_offset)
{
	/* Digest list digest is stored in a different place. */
	if (!digest_offset)
		return digest_list->digest;

	return digest_list->buf + digest_offset;
}

static inline struct compact_list_hdr *get_hdr_ref(
					struct digest_list_item_ref *ref)
{
	return get_hdr(ref->digest_list, ref->hdr_offset);
}

static inline enum hash_algo get_algo_ref(struct digest_list_item_ref *ref)
{
	/* Digest list digest algorithm is stored in a different place. */
	if (!ref->digest_offset)
		return ref->digest_list->algo;

	return get_hdr_ref(ref)->algo;
}

static inline u8 *get_digest_ref(struct digest_list_item_ref *ref)
{
	/* Digest list digest is stored in a different place. */
	if (!ref->digest_offset)
		return ref->digest_list->digest;

	return ref->digest_list->buf + ref->digest_offset;
}

static inline bool digest_list_ref_invalidated(struct digest_list_item_ref *ref)
{
	return (ref->digest_list == ZERO_SIZE_PTR);
}

static inline void digest_list_ref_invalidate(struct digest_list_item_ref *ref)
{
	ref->digest_list = ZERO_SIZE_PTR;
}

static inline bool digest_list_ref_is_last(struct digest_list_item_ref *ref)
{
	return (ref->digest_list == NULL);
}

struct digest_item *digest_lookup(u8 *digest, enum hash_algo algo,
				  enum compact_types type, u16 *modifiers,
				  u8 *actions);
struct digest_item *digest_add(u8 *digest, enum hash_algo algo,
			       enum compact_types type,
			       struct digest_list_item *digest_list,
			       loff_t digest_offset, loff_t hdr_offset);
struct digest_item *digest_del(u8 *digest, enum hash_algo algo,
			       enum compact_types type,
			       struct digest_list_item *digest_list,
			       loff_t digest_offset, loff_t hdr_offset);
struct digest_item *digest_list_add(u8 *digest, enum hash_algo algo,
				    loff_t size, u8 *buf, u8 actions,
				    const char *label);
struct digest_item *digest_list_del(u8 *digest, enum hash_algo algo, u8 actions,
				    struct digest_list_item *digest_list);
#endif /*__DIGEST_LISTS_INTERNAL_H*/
