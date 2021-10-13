// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"
#include "hooks.h"

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/rcupdate.h>
#include <linux/lsm_hooks.h>

struct lsm_blob_sizes ipe_blobs __lsm_ro_after_init = {
	.lbs_task = sizeof(struct ipe_context __rcu *),
};

static struct security_hook_list ipe_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(task_alloc, ipe_task_alloc),
	LSM_HOOK_INIT(task_free, ipe_task_free),
};

/**
 * ipe_init: Entry point of IPE.
 *
 * This is called at LSM init, which happens occurs early during kernel
 * start up. During this phase, IPE loads the properties compiled into
 * the kernel, and register's IPE's hooks. The boot policy is loaded
 * later, during securityfs init, at which point IPE will start
 * enforcing its policy.
 *
 * Return:
 * 0 - OK
 * -ENOMEM - Context creation failed.
 */
static int __init ipe_init(void)
{
	int rc = 0;

	rc = ipe_init_ctx();
	if (rc)
		return rc;

	security_add_hooks(ipe_hooks, ARRAY_SIZE(ipe_hooks), "ipe");

	return rc;
}

DEFINE_LSM(ipe) = {
	.name = "ipe",
	.init = ipe_init,
	.blobs = &ipe_blobs,
};
