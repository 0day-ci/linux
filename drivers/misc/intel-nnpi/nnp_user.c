// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

#include <linux/slab.h>

#include "nnp_user.h"

void nnp_user_init(struct nnp_user_info *user_info)
{
	INIT_LIST_HEAD(&user_info->hostres_list);
	mutex_init(&user_info->mutex);
	kref_init(&user_info->ref);
	idr_init(&user_info->idr);
}

void nnp_user_get(struct nnp_user_info *user_info)
{
	kref_get(&user_info->ref);
}

static void nnp_user_release(struct kref *kref)
{
	struct nnp_user_info *user_info =
		container_of(kref, struct nnp_user_info, ref);
	struct completion *completion = user_info->close_completion;

	idr_destroy(&user_info->idr);
	kfree(user_info);
	complete(completion);
}

void nnp_user_put(struct nnp_user_info *user_info)
{
	kref_put(&user_info->ref, nnp_user_release);
}

int nnp_user_add_hostres(struct nnp_user_info *user_info,
			 struct host_resource *hostres,
			 struct user_hostres **user_hostres_entry)
{
	struct user_hostres *hr_entry;
	int id;

	hr_entry = kmalloc(sizeof(*hr_entry), GFP_KERNEL);
	if (!hr_entry)
		return -ENOMEM;

	/*
	 * Increment refcount to hostres for the entry reference.
	 * (caller holds reference to it, so we know it exist).
	 */
	nnp_hostres_get(hostres);
	hr_entry->hostres = hostres;

	/*
	 * We are called from ioctl of file that own this user_info,
	 * So it safe to assume it exist.
	 */
	nnp_user_get(user_info);
	hr_entry->user_info = user_info;

	mutex_lock(&user_info->mutex);
	/*
	 * We allocate handle starting from 1 and not 0 to allow
	 * user-space treat zero as invalid handle
	 */
	id = idr_alloc(&user_info->idr, hr_entry, 1, -1, GFP_KERNEL);
	if (id < 0) {
		nnp_user_put(user_info);
		nnp_hostres_put(hostres);
		kfree(hr_entry);
		mutex_unlock(&user_info->mutex);
		return -ENOSPC;
	}
	hr_entry->user_handle = id;
	list_add(&hr_entry->node, &user_info->hostres_list);
	mutex_unlock(&user_info->mutex);

	*user_hostres_entry = hr_entry;

	return 0;
}

void nnp_user_remove_hostres_locked(struct user_hostres *hr_entry)
{
	struct nnp_user_info *user_info = hr_entry->user_info;

	idr_remove(&user_info->idr, hr_entry->user_handle);
	list_del(&hr_entry->node);

	nnp_hostres_put(hr_entry->hostres);

	kfree(hr_entry);
	nnp_user_put(user_info);
}

void nnp_user_remove_hostres(struct user_hostres *hr_entry)
{
	struct nnp_user_info *user_info = hr_entry->user_info;

	mutex_lock(&user_info->mutex);
	nnp_user_remove_hostres_locked(hr_entry);
	mutex_unlock(&user_info->mutex);
}

void nnp_user_destroy_all(struct nnp_user_info *user_info)
{
	struct user_hostres *user_hostres_entry;
	DECLARE_COMPLETION_ONSTACK(completion);

	mutex_lock(&user_info->mutex);

	/* destroy all hostreses owned by the "user" */
	while (!list_empty(&user_info->hostres_list)) {
		user_hostres_entry = list_first_entry(&user_info->hostres_list,
						      struct user_hostres, node);
		/*
		 * We can safely destroy this object without checking
		 * its refcount since we get here only after the host char-dev
		 * as well as all cmd_chan char-devs that may hold temporary
		 * reference to this object are already released.
		 */
		nnp_user_remove_hostres_locked(user_hostres_entry);
	}
	mutex_unlock(&user_info->mutex);

	/* wait for all channels and hostreses to be destroyed */
	user_info->close_completion = &completion;
	nnp_user_put(user_info);
	wait_for_completion(&completion);
}
