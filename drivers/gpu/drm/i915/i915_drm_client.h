/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __I915_DRM_CLIENT_H__
#define __I915_DRM_CLIENT_H__

#include <linux/hashtable.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/rwlock.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>

struct drm_i915_private;

struct i915_drm_clients {
	struct drm_i915_private *i915;

	struct xarray xarray;
	u32 next_id;

	rwlock_t lock;
	DECLARE_HASHTABLE(tasks, 6);
};

struct i915_drm_client {
	struct kref kref;

	unsigned int id;

	spinlock_t ctx_lock; /* For add/remove from ctx_list. */
	struct list_head ctx_list; /* List of contexts belonging to client. */

	struct task_struct *owner; /* No reference kept, never dereferenced. */
	struct hlist_node node;

	struct i915_drm_clients *clients;
};

void i915_drm_clients_init(struct i915_drm_clients *clients,
			   struct drm_i915_private *i915);

static inline struct i915_drm_client *
i915_drm_client_get(struct i915_drm_client *client)
{
	kref_get(&client->kref);
	return client;
}

void __i915_drm_client_free(struct kref *kref);

static inline void i915_drm_client_put(struct i915_drm_client *client)
{
	kref_put(&client->kref, __i915_drm_client_free);
}

struct i915_drm_client *i915_drm_client_add(struct i915_drm_clients *clients);

void i915_drm_clients_fini(struct i915_drm_clients *clients);

void i915_drm_client_update_owner(struct i915_drm_client *client,
				  struct task_struct *owner);


#endif /* !__I915_DRM_CLIENT_H__ */
