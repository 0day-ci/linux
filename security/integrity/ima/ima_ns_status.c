// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2021 IBM Corporation
 * Author:
 *  Yuqiong Sun <suny@us.ibm.com>
 *  Stefan Berger <stefanb@linux.vnet.ibm.com>
 */

#include <linux/user_namespace.h>
#include <linux/ima.h>

#include "ima.h"

/*
 * An ns_status must be on a per-namespace rbtree and on a per-iint list.
 *
 * Locking order for ns_status:
 * 1) ns->ns_tree_lock  : Lock the rbtree
 * 2) iint->ns_list_lock: Lock the list
 *
 * An ns_status can be freed either by walking the rbtree (namespace deletion)
 * or by walking the linked list of ns_status (inode/iint deletion). There are
 * two functions that implement each one of the cases. To avoid concurrent
 * freeing of the same ns_status, the two freeing paths cannot be run
 * concurrently but each path can be run by multiple threads since no two
 * threads will free the same inode/iint and no two threads will free the same
 * namespace. Grouping threads like this ensures that:
 * - while walking the rbtree: all ns_status will be on their list and the iint
 *                             will still exist
 * - while walking the list:   all ns_status will be on their rbtree
 */
enum lk_group {
	GRP_NS_STATUS_LIST = 0,
	GRP_NS_STATUS_TREE
};

static atomic_t lg_ctr[2] = {
	ATOMIC_INIT(0),
	ATOMIC_INIT(0)
};

static DEFINE_SPINLOCK(lg_ctr_lock);

static struct wait_queue_head lg_wq[2] = {
	__WAIT_QUEUE_HEAD_INITIALIZER(lg_wq[0]),
	__WAIT_QUEUE_HEAD_INITIALIZER(lg_wq[1])
};

static atomic_t ns_list_waiters = ATOMIC_INIT(0);

/*
 * Any number of concurrent threads may free ns_status's in either one of the
 * groups but the groups must not run concurrently. The GRP_NS_STATUS_TREE
 * group yields to waiters in the GRP_NS_STATUS_LIST group since namespace
 * deletion has more time.
 */
static void lock_group(enum lk_group group)
{
	unsigned long flags;
	bool done = false;
	int announced = 0;

	while (1) {
		spin_lock_irqsave(&lg_ctr_lock, flags);

		switch (group) {
		case GRP_NS_STATUS_LIST:
			if (atomic_read(&lg_ctr[GRP_NS_STATUS_TREE]) == 0) {
				if (announced)
					atomic_dec(&ns_list_waiters);
				done = true;
				atomic_inc(&lg_ctr[GRP_NS_STATUS_LIST]);
			} else {
				/* rbtree being deleted; announce waiting */
				if (!announced) {
					atomic_inc(&ns_list_waiters);
					announced = 1;
				}
			}
			break;
		case GRP_NS_STATUS_TREE:
			if (atomic_read(&lg_ctr[GRP_NS_STATUS_LIST]) == 0 &&
			    atomic_read(&ns_list_waiters) == 0) {
				done = true;
				atomic_inc(&lg_ctr[GRP_NS_STATUS_TREE]);
			}
			break;
		}

		spin_unlock_irqrestore(&lg_ctr_lock, flags);

		if (done)
			break;

		/* wait until opposite group is done */
		switch (group) {
		case GRP_NS_STATUS_LIST:
			wait_event_interruptible
			    (lg_wq[GRP_NS_STATUS_LIST],
			     atomic_read(&lg_ctr[GRP_NS_STATUS_TREE]) == 0);
			break;
		case GRP_NS_STATUS_TREE:
			wait_event_interruptible
			    (lg_wq[GRP_NS_STATUS_TREE],
			     atomic_read(&lg_ctr[GRP_NS_STATUS_LIST]) == 0 &&
			     atomic_read(&ns_list_waiters) == 0);
			break;
		}
	}
}

static void unlock_group(enum lk_group group)
{
	switch (group) {
	case GRP_NS_STATUS_LIST:
		if (atomic_dec_and_test(&lg_ctr[GRP_NS_STATUS_LIST]))
			wake_up_interruptible_all(&lg_wq[GRP_NS_STATUS_TREE]);
		break;
	case GRP_NS_STATUS_TREE:
		if (atomic_dec_and_test(&lg_ctr[GRP_NS_STATUS_TREE]))
			wake_up_interruptible_all(&lg_wq[GRP_NS_STATUS_LIST]);
		break;
	}
}

static void ns_status_free(struct ima_namespace *ns,
			   struct ns_status *ns_status)
{
	pr_debug("FREE ns_status: %p\n", ns_status);

	kmem_cache_free(ns->ns_status_cache, ns_status);
}

/*
 * ima_free_ns_status_tree - free all items on the ns_status_tree and take each
 *                           one off the list; yield to ns_list free'ers
 *
 * This function is called when an ima_namespace is freed. All entries in the
 * rbtree will be taken off their list and collected in a garbage collection
 * list and freed at the end. This allows to walk the rbtree again.
 */
void ima_free_ns_status_tree(struct ima_namespace *ns)
{
	struct ns_status *ns_status, *next;
	struct llist_node *node;
	LLIST_HEAD(garbage);
	unsigned int ctr;
	bool restart;

	do {
		ctr = 0;
		restart = false;

		lock_group(GRP_NS_STATUS_TREE);
		write_lock(&ns->ns_tree_lock);

		rbtree_postorder_for_each_entry_safe(ns_status, next,
						     &ns->ns_status_tree,
						     rb_node) {
			write_lock(&ns_status->iint->ns_list_lock);
			if (!list_empty(&ns_status->ns_next)) {
				list_del_init(&ns_status->ns_next);
				llist_add(&ns_status->gc_llist, &garbage);
				ctr++;
			}
			write_unlock(&ns_status->iint->ns_list_lock);

			/*
			 * After some progress yield to any waiting ns_list
			 * free'ers.
			 */
			if (atomic_read(&ns_list_waiters) > 0 && ctr >= 5) {
				restart = true;
				break;
			}
		}

		write_unlock(&ns->ns_tree_lock);
		unlock_group(GRP_NS_STATUS_TREE);
	} while (restart);

	node = llist_del_all(&garbage);
	llist_for_each_entry_safe(ns_status, next, node, gc_llist)
		ns_status_free(ns, ns_status);

	kmem_cache_destroy(ns->ns_status_cache);
}

/*
 * ima_free_ns_status_list: free the list of ns_status items and take
 *                          each one off its namespace rbtree
 */
void ima_free_ns_status_list(struct list_head *head, rwlock_t *ns_list_lock)
{
	struct ns_status *ns_status;

	lock_group(GRP_NS_STATUS_LIST);

	while (1) {
		write_lock(ns_list_lock);
		ns_status = list_first_entry_or_null(head, struct ns_status,
						     ns_next);
		if (ns_status)
			list_del_init(&ns_status->ns_next);
		write_unlock(ns_list_lock);

		if (!ns_status)
			break;

		write_lock(&ns_status->ns->ns_tree_lock);

		rb_erase(&ns_status->rb_node, &ns_status->ns->ns_status_tree);
		RB_CLEAR_NODE(&ns_status->rb_node);

		write_unlock(&ns_status->ns->ns_tree_lock);

		ns_status_free(ns_status->ns, ns_status);
	}

	unlock_group(GRP_NS_STATUS_LIST);
}

/*
 * ns_status_find - return the ns_status associated with an inode;
 *                  caller must hold lock for tree
 */
static struct ns_status *ns_status_find(struct ima_namespace *ns,
					struct inode *inode)
{
	struct ns_status *ns_status;
	struct rb_node *n = ns->ns_status_tree.rb_node;

	while (n) {
		ns_status = rb_entry(n, struct ns_status, rb_node);

		if (inode < ns_status->inode)
			n = n->rb_left;
		else if (inode > ns_status->inode)
			n = n->rb_right;
		else
			break;
	}
	if (!n)
		return NULL;

	return ns_status;
}

static void insert_ns_status(struct ima_namespace *ns, struct inode *inode,
			     struct ns_status *ns_status)
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
	node = &ns_status->rb_node;
	rb_link_node(node, parent, p);
	rb_insert_color(node, &ns->ns_status_tree);
}

static void ns_status_unlink(struct ima_namespace *ns,
			     struct ns_status *ns_status)
{
	write_lock(&ns_status->iint->ns_list_lock);
	if (!list_empty(&ns_status->ns_next))
		list_del_init(&ns_status->ns_next);
	write_unlock(&ns_status->iint->ns_list_lock);

	rb_erase(&ns_status->rb_node, &ns->ns_status_tree);
	RB_CLEAR_NODE(&ns_status->rb_node);
}

struct ns_status *ima_get_ns_status(struct ima_namespace *ns,
				    struct inode *inode,
				    struct integrity_iint_cache *iint)
{
	struct ns_status *ns_status;
	bool get_new = true;

	/*
	 * Prevent finding the status via the list (inode/iint deletion) since
	 * we may free it.
	 */
	lock_group(GRP_NS_STATUS_TREE);

	write_lock(&ns->ns_tree_lock);

	ns_status = ns_status_find(ns, inode);
	if (ns_status) {
		if (unlikely(ns_status->iint != iint)) {
			/* Same inode but stale iint: free it and get new */
			ns_status_unlink(ns, ns_status);
			ns_status_free(ns, ns_status);
		} else if (inode->i_ino == ns_status->i_ino &&
			   inode->i_generation == ns_status->i_generation) {
			goto unlock;
		} else {
			/* Reuse of ns_status is possible but need reset */
			ns_status_reset(ns_status);
			get_new = false;
		}
	}

	if (get_new) {
		ns_status = kmem_cache_alloc(ns->ns_status_cache, GFP_NOFS);
		if (!ns_status) {
			ns_status = ERR_PTR(-ENOMEM);
			goto unlock;
		}

		pr_debug("NEW  ns_status: %p\n", ns_status);

		ns_status_init(ns_status);
		insert_ns_status(ns, inode, ns_status);
	}

	ns_status->iint = iint;
	ns_status->inode = inode;
	ns_status->ns = ns;
	ns_status->i_ino = inode->i_ino;
	ns_status->i_generation = inode->i_generation;

	/* make visible on list */
	write_lock(&iint->ns_list_lock);
	if (list_empty(&ns_status->ns_next))
		list_add_tail(&ns_status->ns_next, &iint->ns_list);
	write_unlock(&iint->ns_list_lock);

unlock:
	write_unlock(&ns->ns_tree_lock);

	unlock_group(GRP_NS_STATUS_TREE);

	return ns_status;
}
