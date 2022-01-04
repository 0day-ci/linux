// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2021 IBM Corporation
 * Author:
 *  Yuqiong Sun <suny@us.ibm.com>
 *  Stefan Berger <stefanb@linux.vnet.ibm.com>
 */

#include <linux/ima.h>

#include "ima.h"

static struct kmem_cache *imans_cachep;

struct ima_namespace *create_ima_ns(struct user_namespace *user_ns)
{
	struct ima_namespace *ns;
	int err;

	ns = kmem_cache_zalloc(imans_cachep, GFP_KERNEL);
	if (!ns)
		return ERR_PTR(-ENOMEM);
	pr_debug("NEW     ima_ns: %p\n", ns);

	err = ima_init_namespace(ns);
	if (err)
		goto fail_free;

	user_ns->ima_ns = ns;

	return ns;

fail_free:
	kmem_cache_free(imans_cachep, ns);

	return ERR_PTR(err);
}

static void destroy_ima_ns(struct ima_namespace *ns)
{
	pr_debug("DESTROY ima_ns: %p\n", ns);
	ima_free_policy_rules(ns);
	ima_free_ns_status_tree(ns);
	kmem_cache_free(imans_cachep, ns);
}

void free_ima_ns(struct user_namespace *user_ns)
{
	struct ima_namespace *ns = user_ns->ima_ns;

	if (!ns || WARN_ON(ns == &init_ima_ns))
		return;

	destroy_ima_ns(ns);
}

unsigned long iint_flags(struct integrity_iint_cache *iint,
			 struct ns_status *status)
{
	if (!status)
		return iint->flags;

	return (iint->flags & ~IMA_NS_STATUS_FLAGS) |
	       (status->flags & IMA_NS_STATUS_FLAGS);
}

unsigned long set_iint_flags(struct integrity_iint_cache *iint,
			     struct ns_status *status, unsigned long flags)
{
	iint->flags = flags;
	if (status)
		status->flags = flags & IMA_NS_STATUS_FLAGS;

	return flags;
}

static int __init imans_cache_init(void)
{
	imans_cachep = KMEM_CACHE(ima_namespace, SLAB_PANIC);
	return 0;
}
subsys_initcall(imans_cache_init)
