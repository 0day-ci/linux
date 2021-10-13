// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"
#include "hooks.h"
#include "ipe_parser.h"
#include "modules/ipe_module.h"
#include "modules.h"

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
	LSM_HOOK_INIT(bprm_check_security, ipe_on_exec),
	LSM_HOOK_INIT(mmap_file, ipe_on_mmap),
	LSM_HOOK_INIT(file_mprotect, ipe_on_mprotect),
	LSM_HOOK_INIT(kernel_read_file, ipe_on_kernel_read),
	LSM_HOOK_INIT(kernel_load_data, ipe_on_kernel_load_data),
};

/**
 * load_parsers: Load all the parsers compiled into IPE. This needs
 *		 to be called prior to the boot policy being loaded.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
static int load_parsers(void)
{
	int rc = 0;
	struct ipe_parser *parser;

	for (parser = __start_ipe_parsers; parser < __end_ipe_parsers; ++parser) {
		rc = ipe_register_parser(parser);
		if (rc) {
			pr_err("failed to initialize '%s'", parser->first_token);
			return rc;
		}

		pr_info("initialized parser module '%s'", parser->first_token);
	}

	return 0;
}

/**
 * load_modules: Load all the modules compiled into IPE. This needs
 *		 to be called prior to the boot policy being loaded.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
static int load_modules(void)
{
	int rc = 0;
	struct ipe_module *m;

	for (m = __start_ipe_modules; m < __end_ipe_modules; ++m) {
		rc = ipe_register_module(m);
		if (rc) {
			pr_err("failed to initialize '%s'", m->name);
			return rc;
		}

		pr_info("initialized module '%s'", m->name);
	}

	return 0;
}

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

	rc = load_parsers();
	if (rc)
		return rc;

	rc = load_modules();
	if (rc)
		return rc;

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
