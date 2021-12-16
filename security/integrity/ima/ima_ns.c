// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2021 IBM Corporation
 * Author:
 *  Yuqiong Sun <suny@us.ibm.com>
 *  Stefan Berger <stefanb@linux.vnet.ibm.com>
 */

#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/ima.h>
#include <linux/mount.h>
#include <linux/proc_ns.h>
#include <linux/lsm_hooks.h>

#include "ima.h"

static struct kmem_cache *imans_cachep;

int create_ima_ns(struct user_namespace *user_ns)
{
	struct ima_namespace *ns;
	int err;

	ns = kmem_cache_zalloc(imans_cachep, GFP_KERNEL);
	if (!ns)
		return -ENOMEM;
	pr_debug("NEW     ima_ns: 0x%p\n", ns);

	err = ima_init_namespace(ns);
	if (err)
		goto fail_free;

	user_ns->ima_ns = ns;

	return 0;

fail_free:
	kmem_cache_free(imans_cachep, ns);

	return err;
}

static void destroy_ima_ns(struct ima_namespace *ns)
{
	pr_debug("DESTROY ima_ns: 0x%p\n", ns);
	ima_free_policy_rules(ns);
	free_ns_status_cache(ns);
	kmem_cache_free(imans_cachep, ns);
}

void free_ima_ns(struct user_namespace *user_ns)
{
	struct ima_namespace *ns = user_ns->ima_ns;

	if (WARN_ON(ns == &init_ima_ns))
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
