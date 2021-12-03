// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2018 IBM Corporation
 * Author:
 *   Yuqiong Sun <suny@us.ibm.com>
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#include <linux/export.h>
#include <linux/user_namespace.h>
#include <linux/ima.h>
#include <linux/proc_ns.h>
#include <linux/slab.h>

#include "ima.h"

int ima_init_namespace(struct ima_namespace *ns)
{
	ns->ns_status_tree = RB_ROOT;
	rwlock_init(&ns->ns_status_lock);
	ns->ns_status_cache = KMEM_CACHE(ns_status, SLAB_PANIC);
	if (!ns->ns_status_cache)
		return -ENOMEM;

#ifdef CONFIG_IMA_QUEUE_EARLY_BOOT_KEYS
	INIT_DELAYED_WORK(&ns->ima_keys_delayed_work, ima_keys_handler);
	ns->ima_key_queue_timeout = 300000;
	ns->timer_expired = false;
	if (ns == &init_ima_ns)
		ima_init_key_queue(ns);
#endif

	INIT_LIST_HEAD(&ns->ima_default_rules);
	INIT_LIST_HEAD(&ns->ima_policy_rules);
	INIT_LIST_HEAD(&ns->ima_temp_rules);
	ns->ima_rules = (struct list_head __rcu *)(&ns->ima_default_rules);
	ns->ima_policy_flag = 0;

	atomic_long_set(&ns->ima_htable.len, 0);
	atomic_long_set(&ns->ima_htable.violations, 0);
	memset(&ns->ima_htable.queue, 0, sizeof(ns->ima_htable.queue));
	INIT_LIST_HEAD(&ns->ima_measurements);
	if (IS_ENABLED(CONFIG_IMA_KEXEC) && ns == &init_ima_ns)
		ns->binary_runtime_size = 0;
	else
		ns->binary_runtime_size = ULONG_MAX;

	return 0;
}

int __init ima_ns_init(void)
{
	return ima_init_namespace(&init_ima_ns);
}

struct ima_namespace init_ima_ns = {
	.kref = KREF_INIT(1),
	.user_ns = &init_user_ns,
#ifdef CONFIG_IMA_QUEUE_EARLY_BOOT_KEYS
	.ima_process_keys = false,
	.ima_keys_lock = __MUTEX_INITIALIZER(init_ima_ns.ima_keys_lock),
	.ima_keys = LIST_HEAD_INIT(init_ima_ns.ima_keys),
#endif
};
EXPORT_SYMBOL(init_ima_ns);
