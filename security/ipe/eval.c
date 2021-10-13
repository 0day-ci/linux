// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"
#include "eval.h"
#include "hooks.h"
#include "policy.h"
#include "modules/ipe_module.h"
#include "audit.h"

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>

/**
 * build_ctx: Build an evaluation context.
 * @file: Supplies a pointer to the file to associated with the evaluation
 * @op: Supplies the IPE policy operation associated with the evaluation
 * @hook: Supplies the LSM hook associated with the evaluation.
 *
 * The current IPE Context will have a reference count increased by one until
 * this is deallocated.
 *
 * Return:
 * !IS_ERR - OK
 */
static struct ipe_eval_ctx *build_ctx(const struct file *file,
				      enum ipe_operation op,
				      enum ipe_hook hook)
{
	struct ipe_eval_ctx *ctx = NULL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->file = file;
	ctx->op = op;
	ctx->hook = hook;
	ctx->ci_ctx = ipe_current_ctx();

	return ctx;
}

/**
 * free_ctx: Deallocate a previously-allocated ipe_eval_ctx
 * @ctx: Supplies a pointer to the evaluation context to free.
 */
static void free_ctx(struct ipe_eval_ctx *ctx)
{
	if (IS_ERR_OR_NULL(ctx))
		return;

	ipe_put_ctx(ctx->ci_ctx);
	kfree(ctx);
}

/**
 * evaluate: Analyze @ctx against the active policy and return the result.
 * @ctx: Supplies a pointer to the context being evaluated.
 *
 * This is the loop where all policy evaluation happens against IPE policy.
 *
 * Return:
 * 0 - OK
 * -EACCES - @ctx did not pass evaluation.
 * !0 - Error
 */
static int evaluate(const struct ipe_eval_ctx *const ctx)
{
	int rc = 0;
	bool match = false;
	bool enforcing = true;
	enum ipe_action action;
	enum ipe_match match_type;
	struct ipe_policy *pol = NULL;
	const struct ipe_rule *rule = NULL;
	const struct ipe_policy_mod *module = NULL;
	const struct ipe_operation_table *rules = NULL;

	pol = ipe_get_policy_rcu(ctx->ci_ctx->active_policy);
	if (!pol)
		goto out;

	rules = &pol->parsed->rules[ctx->op];

	list_for_each_entry(rule, &rules->rules, next) {
		match = true;

		list_for_each_entry(module, &rule->modules, next)
			match = match && module->mod->eval(ctx, module->mod_value);

		if (match)
			break;
	}

	if (match) {
		action = rule->action;
		match_type = ipe_match_rule;
	} else if (rules->default_action != ipe_action_max) {
		action = rules->default_action;
		match_type = ipe_match_table;
	} else {
		action = pol->parsed->global_default;
		match_type = ipe_match_global;
	}

	ipe_audit_match(ctx, match_type, action, rule, enforcing);

	if (action == ipe_action_deny)
		rc = -EACCES;

out:
	ipe_put_policy(pol);
	return rc;
}

/**
 * ipe_process_event: Submit @file for verification against IPE's policy
 * @file: Supplies an optional pointer to the file being submitted.
 * @op: IPE Policy Operation to associate with @file
 * @hook: LSM Hook to associate with @file
 *
 * @file can be NULL and will be submitted for evaluation like a non-NULL
 * file.
 *
 * Return:
 * 0 - OK
 * -EACCES - @file did not pass verification
 * !0 - Error
 */
int ipe_process_event(const struct file *file, enum ipe_operation op,
		      enum ipe_hook hook)
{
	int rc = 0;
	struct ipe_eval_ctx *ctx = NULL;

	ctx = build_ctx(file, op, hook);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	rc = evaluate(ctx);

	free_ctx(ctx);
	return rc;
}
