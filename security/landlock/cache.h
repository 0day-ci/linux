/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Generic cache management
 *
 * Copyright © 2021 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_CACHE_H
#define _SECURITY_LANDLOCK_CACHE_H

#include <linux/compiler.h>
#include <linux/refcount.h>
#include <linux/types.h>

#include "ruleset.h"

/**
 * struct landlock_cache - Generic access cache for an object
 *
 * Store cached access rights for a Landlock object (i.e. tied to specific
 * domain).  Allowed accesses are set once (e.g. at file opening) and never
 * change after that.  As a side effect, this means that such cache created
 * before a domain transition will not get an up to date allowed accesses.
 * This implies to always check a cached domain against the current domain
 * thanks to landlock_cache_is_valid().
 *
 * This struct is part of a typed cache (e.g. &struct landlock_fs_cache.core)
 * that identifies the tied object.
 */
struct landlock_cache {
	/**
	 * @dangling_domain: If not NULL, points to the domain for which
	 * @allowed_accesses is valid.  This brings two constraints:
	 * - @dangling_domain must only be read with READ_ONCE() and written
	 *   with WRITE_ONCE() (except at initialization).
	 * - @dangling_domain can only be safely dereferenced by the cache
	 *   owner (e.g. with landlock_disable_cache() when the underlying file
	 *   is being closed).
	 */
	void *dangling_domain;
	/**
	 * @usage: This counter is used to keep a cache alive while it can
	 * still be checked against.
	 */
	refcount_t usage;
	/**
	 * @allowed_accesses: Mask of absolute known-to-be allowed accesses to
	 * an object at creation-time (e.g. at open-time for the file hierarchy
	 * of a file descriptor).  A bit not set doesn't mean that the related
	 * access is denied.  The type of access is inferred from the type of
	 * the related object.  The task domain may not be the same as the
	 * cached one and they must then be checked against each other when
	 * evaluating @allowed_accesses thanks to landlock_cache_is_valid().
	 */
	u16 allowed_accesses;
};

static inline void landlock_disable_cache(struct landlock_cache *cache)
{
	struct landlock_ruleset *const dom = cache->dangling_domain;

	/* Atomically marks the cache as disabled. */
	WRITE_ONCE(cache->dangling_domain, NULL);
	/*
	 * There is no need for other synchronisation mechanism because the
	 * domain is never dereferenced elsewhere.
	 */
	landlock_put_ruleset(dom);
}

static inline bool landlock_cache_is_valid(const struct landlock_cache *cache,
		const struct landlock_ruleset *domain)
{
	return (domain && domain == READ_ONCE(cache->dangling_domain));
}

#endif /* _SECURITY_LANDLOCK_CACHE_H */
