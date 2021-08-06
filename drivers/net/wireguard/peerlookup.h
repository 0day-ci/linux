/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _WG_PEERLOOKUP_H
#define _WG_PEERLOOKUP_H

#include "messages.h"

#include <linux/rhashtable.h>
#include <linux/siphash.h>

struct wg_peer;

struct pubkey_hashtable {
	struct rhashtable hashtable;
	siphash_key_t key;
};

struct pubkey_hashtable *wg_pubkey_hashtable_alloc(void);
void wg_pubkey_hashtable_add(struct pubkey_hashtable *table,
			     struct wg_peer *peer);
void wg_pubkey_hashtable_remove(struct pubkey_hashtable *table,
				struct wg_peer *peer);
struct wg_peer *
wg_pubkey_hashtable_lookup(struct pubkey_hashtable *table,
			   const u8 pubkey[NOISE_PUBLIC_KEY_LEN]);
void wg_pubkey_hashtable_destroy(struct pubkey_hashtable *table);

enum index_hashtable_type {
	INDEX_HASHTABLE_HANDSHAKE = 1U << 0,
	INDEX_HASHTABLE_KEYPAIR = 1U << 1
};

struct index_hashtable_entry {
	struct wg_peer *peer;
	struct rhash_head index_hash;
	enum index_hashtable_type type;
	__le32 index;
};

struct rhashtable *wg_index_hashtable_alloc(void);
__le32 wg_index_hashtable_insert(struct rhashtable *table,
				 struct index_hashtable_entry *entry);
bool wg_index_hashtable_replace(struct rhashtable *table,
				struct index_hashtable_entry *old,
				struct index_hashtable_entry *new);
void wg_index_hashtable_remove(struct rhashtable *table,
			       struct index_hashtable_entry *entry);
struct index_hashtable_entry *
wg_index_hashtable_lookup(struct rhashtable *table,
			  const enum index_hashtable_type type_mask,
			  const __le32 index, struct wg_peer **peer);
void wg_index_hashtable_destroy(struct rhashtable *table);

#endif /* _WG_PEERLOOKUP_H */
