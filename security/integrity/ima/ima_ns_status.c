// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2021 IBM Corporation
 * Author:
 *  Yuqiong Sun <suny@us.ibm.com>
 *  Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#include <linux/user_namespace.h>
#include <linux/proc_ns.h>
#include <linux/ima.h>

#include "ima.h"

void free_ns_status_cache(struct ima_namespace *ns)
{
	struct ns_status *status, *next;

	write_lock(&ns->ns_status_lock);
	rbtree_postorder_for_each_entry_safe(status, next,
					     &ns->ns_status_tree, rb_node)
		kmem_cache_free(ns->ns_status_cache, status);
	ns->ns_status_tree = RB_ROOT;
	write_unlock(&ns->ns_status_lock);
	kmem_cache_destroy(ns->ns_status_cache);
}

/*
 * __ima_ns_status_find - return the ns_status associated with an inode
 */
static struct ns_status *__ima_ns_status_find(struct ima_namespace *ns,
					      struct inode *inode)
{
	struct ns_status *status;
	struct rb_node *n = ns->ns_status_tree.rb_node;

	while (n) {
		status = rb_entry(n, struct ns_status, rb_node);

		if (inode < status->inode)
			n = n->rb_left;
		else if (inode > status->inode)
			n = n->rb_right;
		else
			break;
	}
	if (!n)
		return NULL;

	return status;
}

/*
 * ima_ns_status_find - return the ns_status associated with an inode
 */
static struct ns_status *ima_ns_status_find(struct ima_namespace *ns,
					    struct inode *inode)
{
	struct ns_status *status;

	read_lock(&ns->ns_status_lock);
	status = __ima_ns_status_find(ns, inode);
	read_unlock(&ns->ns_status_lock);

	return status;
}

static void insert_ns_status(struct ima_namespace *ns, struct inode *inode,
			     struct ns_status *status)
{
	struct rb_node **p;
	struct rb_node *node, *parent = NULL;
	struct ns_status *test_status;

	p = &ns->ns_status_tree.rb_node;
	while (*p) {
		parent = *p;
		test_status = rb_entry(parent, struct ns_status, rb_node);
		if (inode < test_status->inode)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	node = &status->rb_node;
	rb_link_node(node, parent, p);
	rb_insert_color(node, &ns->ns_status_tree);
}

struct ns_status *ima_get_ns_status(struct ima_namespace *ns,
				    struct inode *inode)
{
	struct ns_status *status;
	int skip_insert = 0;

	status = ima_ns_status_find(ns, inode);
	if (status) {
		/*
		 * Unlike integrity_iint_cache we are not free'ing the
		 * ns_status data when the inode is free'd. So, in addition to
		 * checking the inode pointer, we need to make sure the
		 * (i_generation, i_ino) pair matches as well.
		 */
		if (inode->i_ino == status->i_ino &&
		    inode->i_generation == status->i_generation)
			return status;

		/* Same inode number is reused, overwrite the ns_status */
		skip_insert = 1;
	} else {
		status = kmem_cache_alloc(ns->ns_status_cache, GFP_NOFS);
		if (!status)
			return ERR_PTR(-ENOMEM);
	}

	write_lock(&ns->ns_status_lock);

	if (!skip_insert)
		insert_ns_status(ns, inode, status);

	status->inode = inode;
	status->i_ino = inode->i_ino;
	status->i_generation = inode->i_generation;
	status->flags = 0UL;

	write_unlock(&ns->ns_status_lock);

	return status;
}
