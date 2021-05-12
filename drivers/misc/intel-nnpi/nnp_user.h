/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_INF_PROC_H
#define _NNPDRV_INF_PROC_H

#include <linux/kref.h>
#include <linux/types.h>

#include "hostres.h"

/**
 * struct nnp_user_info - structure for per-user info
 * @ref: refcount to this "user" object
 * @hostres_list: list of host resources
 * @close_completion: used to wait for all channels of this user to be
 *                    destroyed before closing the user.
 * @mutex: protects hostres_list and idr modifications
 * @idr: used to generate user handles to created host resources
 * @user_list_node: list node to attach this struct in "list of users".
 *
 * structure to hold per-user info,
 * a "user" is created for each open made to the host char dev (/dev/nnpi_host).
 * It holds a list of all host resources created through requests from
 * the same client ("user").
 * device communication "channels", created by device char dev (/dev/nnpi%d)
 * must be correlated with a "user" object which is supplied from user-space
 * by the opened file descriptor to /dev/nnpi_host. Such "channel" may access
 * only host resources created by the same "user".
 * The lifetime of this object last at least for the duration of the host char
 * device file struct but can last longer if some channel objects still hold
 * a reference to it (this is why @ref is needed).
 */
struct nnp_user_info {
	struct kref         ref;
	struct list_head    hostres_list;
	struct completion   *close_completion;
	struct mutex        mutex;
	struct idr          idr;
	struct list_head    user_list_node;
};

/**
 * struct user_hostres - structure for host resource created by user
 * @node: list node to attach this struct to nnp_user_info::hostres_list
 * @hostres: the actual host resource object
 * @user_handle: handle allocated from idr object, used as handle to this
 *               object in ioctl ABI.
 * @user_info: pointer to "user" which created this resource.
 *             it is used only during destruction of the object.
 *
 * structure for a host resource object which created through host char dev
 * request. The lifetime of this structure ends when the user request to
 * destroy it through ioctl call. The underlying @hostres may still continue
 * to exist if command channel (cmd_chan) objects has mapped the resource to
 * device access.
 */
struct user_hostres {
	struct list_head             node;
	struct host_resource         *hostres;
	int                          user_handle;
	struct nnp_user_info         *user_info;
};

void nnp_user_init(struct nnp_user_info *user_info);

void nnp_user_get(struct nnp_user_info *user_info);
void nnp_user_put(struct nnp_user_info *user_info);

int nnp_user_add_hostres(struct nnp_user_info *user_info,
			 struct host_resource *hostres,
			 struct user_hostres **user_hostres_entry);

void nnp_user_remove_hostres(struct user_hostres *hr_entry);
void nnp_user_remove_hostres_locked(struct user_hostres *hr_entry);

void nnp_user_destroy_all(struct nnp_user_info *user_info);

#endif
