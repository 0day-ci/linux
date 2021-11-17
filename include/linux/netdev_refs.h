/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _LINUX_NETDEV_REFS_H
#define _LINUX_NETDEV_REFS_H

#include <linux/debugobjects.h>
#include <linux/netdevice.h>

/* Explicit netdevice references
 * struct netdev_ref is a storage for a reference. It's equivalent
 * to a netdev pointer, but when debug is enabled it performs extra checks.
 * Most users will want to take a reference with netdev_hold(), access it
 * via netdev_ref_ptr() and release with netdev_put().
 */

struct netdev_ref {
	struct net_device *dev;
#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	refcount_t cnt;
#endif
};

extern const struct debug_obj_descr netdev_ref_debug_descr;

/* Store a raw, unprotected pointer */
static inline void __netdev_ref_store(struct netdev_ref *ref,
				      struct net_device *dev)
{
	ref->dev = dev;

#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	refcount_set(&ref->cnt, 0);
	debug_object_init(ref, &netdev_ref_debug_descr);
#endif
}

/* Convert a previously stored unprotected pointer to a normal ref */
static inline void __netdev_hold_stored(struct netdev_ref *ref)
{
	dev_hold(ref->dev);

#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	refcount_set(&ref->cnt, 1);
	debug_object_activate(ref, &netdev_ref_debug_descr);
#endif
}

/* Take a reference on a netdev and store it in @ref */
static inline void netdev_hold(struct netdev_ref *ref, struct net_device *dev)
{
	__netdev_ref_store(ref, dev);
	__netdev_hold_stored(ref);
}

/* Release a reference on a netdev previously acquired by netdev_hold() */
static inline void netdev_put(struct netdev_ref *ref)
{
	dev_put(ref->dev);

#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	WARN_ON(refcount_read(&ref->cnt) != 1);
	debug_object_deactivate(ref, &netdev_ref_debug_descr);
#endif
}

/* Increase refcount of a reference, reference must be valid -
 * initialized by netdev_hold() or equivalent set of sub-functions.
 */
static inline void netdev_ref_get(struct netdev_ref *ref)
{
	dev_hold(ref->dev);

#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	refcount_inc(&ref->cnt);
#endif
}

/* Release a reference with unknown number of refs */
static inline void netdev_ref_put(struct netdev_ref *ref)
{
	dev_put(ref->dev);

#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	if (refcount_dec_and_test(&ref->cnt))
		debug_object_deactivate(ref, &netdev_ref_debug_descr);
#endif
}

/* Unprotected access to a pointer stored by __netdev_ref_store() */
static inline struct net_device *__netdev_ref_ptr(const struct netdev_ref *ref)
{
	return ref->dev;
}

/* Netdev pointer access on a normal ref */
static inline struct net_device *netdev_ref_ptr(const struct netdev_ref *ref)
{
#ifdef CONFIG_DEBUG_OBJECTS_NETDEV_REFS
	WARN_ON(!refcount_read(&ref->cnt));
#endif
	return ref->dev;
}

#endif
