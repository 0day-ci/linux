// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "peerlookup.h"
#include "peer.h"
#include "noise.h"

struct pubkey_pair {
	u8 key[NOISE_PUBLIC_KEY_LEN];
	siphash_key_t skey;
};

static u32 pubkey_hash(const void *data, u32 len, u32 seed)
{
	const struct pubkey_pair *pair = data;

	/* siphash gives us a secure 64bit number based on a random key. Since
	 * the bits are uniformly distributed.
	 */

	return (u32)siphash(pair->key, len, &pair->skey);
}

static const struct rhashtable_params wg_peer_params = {
	.key_len = NOISE_PUBLIC_KEY_LEN,
	.key_offset = offsetof(struct wg_peer, handshake.remote_static),
	.head_offset = offsetof(struct wg_peer, pubkey_hash),
	.hashfn = pubkey_hash
};

struct pubkey_hashtable *wg_pubkey_hashtable_alloc(void)
{
	struct pubkey_hashtable *table = kvmalloc(sizeof(*table), GFP_KERNEL);

	if (!table)
		return NULL;

	get_random_bytes(&table->key, sizeof(table->key));
	rhashtable_init(&table->hashtable, &wg_peer_params);

	return table;
}

void wg_pubkey_hashtable_add(struct pubkey_hashtable *table,
			     struct wg_peer *peer)
{
	memcpy(&peer->handshake.skey, &table->key, sizeof(table->key));
	WARN_ON(rhashtable_insert_fast(&table->hashtable, &peer->pubkey_hash,
				       wg_peer_params));
}

void wg_pubkey_hashtable_remove(struct pubkey_hashtable *table,
				struct wg_peer *peer)
{
	memcpy(&peer->handshake.skey, &table->key, sizeof(table->key));
	rhashtable_remove_fast(&table->hashtable, &peer->pubkey_hash,
			       wg_peer_params);
}

/* Returns a strong reference to a peer */
struct wg_peer *
wg_pubkey_hashtable_lookup(struct pubkey_hashtable *table,
			   const u8 pubkey[NOISE_PUBLIC_KEY_LEN])
{
	struct wg_peer *peer = NULL;
	struct pubkey_pair pair;

	memcpy(pair.key, pubkey, NOISE_PUBLIC_KEY_LEN);
	memcpy(&pair.skey, &table->key, sizeof(pair.skey));

	rcu_read_lock_bh();
	peer = wg_peer_get_maybe_zero(rhashtable_lookup_fast(&table->hashtable,
							     &pair,
							     wg_peer_params));
	rcu_read_unlock_bh();

	return peer;
}

void wg_pubkey_hashtable_destroy(struct pubkey_hashtable *table)
{
	WARN_ON(atomic_read(&table->hashtable.nelems));
	rhashtable_destroy(&table->hashtable);
}

static u32 index_hash(const void *data, u32 len, u32 seed)
{
	const __le32 *index = data;

	/* Since the indices are random and thus all bits are uniformly
	 * distributed, we can use them as the hash value.
	 */

	return (__force u32)*index;
}

static const struct rhashtable_params index_entry_params = {
	.key_len = sizeof(__le32),
	.key_offset = offsetof(struct index_hashtable_entry, index),
	.head_offset = offsetof(struct index_hashtable_entry, index_hash),
	.hashfn = index_hash
};

struct rhashtable *wg_index_hashtable_alloc(void)
{
	struct rhashtable *table = kvmalloc(sizeof(*table), GFP_KERNEL);

	if (!table)
		return NULL;

	rhashtable_init(table, &index_entry_params);

	return table;
}

/* At the moment, we limit ourselves to 2^20 total peers, which generally might
 * amount to 2^20*3 items in this hashtable. The algorithm below works by
 * picking a random number and testing it. We can see that these limits mean we
 * usually succeed pretty quickly:
 *
 * >>> def calculation(tries, size):
 * ...     return (size / 2**32)**(tries - 1) *  (1 - (size / 2**32))
 * ...
 * >>> calculation(1, 2**20 * 3)
 * 0.999267578125
 * >>> calculation(2, 2**20 * 3)
 * 0.0007318854331970215
 * >>> calculation(3, 2**20 * 3)
 * 5.360489012673497e-07
 * >>> calculation(4, 2**20 * 3)
 * 3.9261394135792216e-10
 *
 * At the moment, we don't do any masking, so this algorithm isn't exactly
 * constant time in either the random guessing or in the hash list lookup. We
 * could require a minimum of 3 tries, which would successfully mask the
 * guessing. this would not, however, help with the growing hash lengths, which
 * is another thing to consider moving forward.
 */

__le32 wg_index_hashtable_insert(struct rhashtable *table,
				 struct index_hashtable_entry *entry)
{
	struct index_hashtable_entry *existing_entry;

	wg_index_hashtable_remove(table, entry);

	rcu_read_lock_bh();

search_unused_slot:
	/* First we try to find an unused slot, randomly, while unlocked. */
	entry->index = (__force __le32)get_random_u32();

	existing_entry = rhashtable_lookup_get_insert_fast(table,
							   &entry->index_hash,
							   index_entry_params);

	if (existing_entry) {
		WARN_ON(IS_ERR(existing_entry));

		/* If it's already in use, we continue searching. */
		goto search_unused_slot;
	}

	rcu_read_unlock_bh();

	return entry->index;
}

bool wg_index_hashtable_replace(struct rhashtable *table,
				struct index_hashtable_entry *old,
				struct index_hashtable_entry *new)
{
	int ret = rhashtable_replace_fast(table, &old->index_hash,
					  &new->index_hash,
					  index_entry_params);

	WARN_ON(ret == -EINVAL);

	return ret != -ENOENT;
}

void wg_index_hashtable_remove(struct rhashtable *table,
			       struct index_hashtable_entry *entry)
{
	rhashtable_remove_fast(table, &entry->index_hash, index_entry_params);
}

/* Returns a strong reference to a entry->peer */
struct index_hashtable_entry *
wg_index_hashtable_lookup(struct rhashtable *table,
			  const enum index_hashtable_type type_mask,
			  const __le32 index, struct wg_peer **peer)
{
	struct index_hashtable_entry *entry = NULL;

	rcu_read_lock_bh();
	entry = rhashtable_lookup_fast(table, &index, index_entry_params);

	if (likely(entry)) {
		if (unlikely(!(entry->type & type_mask))) {
			entry = NULL;
			goto out;
		}

		entry->peer = wg_peer_get_maybe_zero(entry->peer);
		if (likely(entry->peer))
			*peer = entry->peer;
		else
			entry = NULL;
	}

out:
	rcu_read_unlock_bh();

	return entry;
}

void wg_index_hashtable_destroy(struct rhashtable *table)
{
	WARN_ON(atomic_read(&table->nelems));
	rhashtable_destroy(table);
}
