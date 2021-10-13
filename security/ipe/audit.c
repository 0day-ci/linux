// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "eval.h"
#include "hooks.h"
#include "policy.h"
#include "audit.h"
#include "modules/ipe_module.h"

#include <linux/slab.h>
#include <linux/audit.h>
#include <linux/types.h>
#include <crypto/hash.h>

#define NULLSTR(x) ((x) == NULL ? "NULL" : "!NULL")
#define ACTSTR(x) ((x) == ipe_action_allow ? "ALLOW" : "DENY")

#define POLICY_LOAD_FMT "IPE policy_name=%s policy_version=%hu.%hu.%hu "\
			CONFIG_IPE_AUDIT_HASH_ALG "="

static const char *const audit_hook_names[ipe_hook_max] = {
	"EXECVE",
	"MMAP",
	"MPROTECT",
	"KERNEL_READ",
	"KERNEL_LOAD",
};

static const char *const audit_op_names[ipe_operation_max] = {
	"EXECUTE",
	"FIRMWARE",
	"KMODULE",
	"KEXEC_IMAGE",
	"KEXEC_INITRAMFS",
	"IMA_X509_CERT",
	"IMA_POLICY",
};

/**
 * audit_pathname: retrieve the absoute path to a file being evaluated.
 * @f: File to retrieve the absolute path for.
 *
 * This function walks past symlinks and mounts.
 *
 * Return:
 * !IS_ERR - OK
 */
static char *audit_pathname(const struct file *f)
{
	int rc = 0;
	char *pos = NULL;
	char *pathbuf = NULL;
	char *temp_path = NULL;

	if (IS_ERR_OR_NULL(f))
		return ERR_PTR(-ENOENT);

	pathbuf = __getname();
	if (!pathbuf)
		return ERR_PTR(-ENOMEM);

	pos = d_absolute_path(&f->f_path, pathbuf, PATH_MAX);
	if (IS_ERR(pos)) {
		rc = PTR_ERR(pos);
		goto err;
	}

	temp_path = __getname();
	if (!temp_path) {
		rc = -ENOMEM;
		goto err;
	}

	strscpy(temp_path, pos, PATH_MAX);
	__putname(pathbuf);

	return temp_path;
err:
	__putname(pathbuf);
	return ERR_PTR(rc);
}

/**
 * audit_eval_ctx: audit an evaluation context structure.
 * @ab: Supplies a poniter to the audit_buffer to append to.
 * @ctx: Supplies a pointer to the evaluation context to audit
 * @enforce: Supplies a boolean representing the enforcement state
 */
static void audit_eval_ctx(struct audit_buffer *ab,
			   const struct ipe_eval_ctx *const ctx, bool enforce)
{
	char *abspath = NULL;

	audit_log_format(ab, "ctx_pid=%d ", task_tgid_nr(current));
	audit_log_format(ab, "ctx_op=%s ", audit_op_names[ctx->op]);
	audit_log_format(ab, "ctx_hook=%s ", audit_hook_names[ctx->hook]);
	audit_log_format(ab, "ctx_ns_enforce=%d ", enforce);
	audit_log_format(ab, "ctx_comm=");
	audit_log_n_untrustedstring(ab, current->comm, ARRAY_SIZE(current->comm));
	audit_log_format(ab, " ");

	/* best effort */
	if (ctx->file) {
		abspath = audit_pathname(ctx->file);
		if (!IS_ERR(abspath)) {
			audit_log_format(ab, "ctx_pathname=");
			audit_log_n_untrustedstring(ab, abspath, PATH_MAX);
			__putname(abspath);
		}

		audit_log_format(ab, " ctx_ino=%ld ctx_dev=%s",
				 ctx->file->f_inode->i_ino,
				 ctx->file->f_inode->i_sb->s_id);
	}
}

/**
 * audit_rule: audit an IPE policy rule approximation.
 * @ab: Supplies a poniter to the audit_buffer to append to.
 * @r: Supplies a pointer to the ipe_rule to approximate a string form for.
 *
 * This is an approximation because aliases like "KERNEL_READ" will be
 * emitted in their expanded form.
 */
static void audit_rule(struct audit_buffer *ab, const struct ipe_rule *r)
{
	const struct ipe_policy_mod *ptr;

	audit_log_format(ab, "rule=\"op=%s ", audit_op_names[r->op]);

	list_for_each_entry(ptr, &r->modules, next) {
		audit_log_format(ab, "%s=", ptr->mod->name);

		ptr->mod->audit(ab, ptr->mod_value);

		audit_log_format(ab, " ");
	}

	audit_log_format(ab, "action=%s\"", ACTSTR(r->action));
}

/**
 * ipe_audit_match: audit a match for IPE policy.
 * @ctx: Supplies a poniter to the evaluation context that was used in the
 *	 evaluation.
 * @match_type: Supplies the scope of the match: rule, operation default,
 *		global default.
 * @act: Supplies the IPE's evaluation decision, deny or allow.
 * @r: Supplies a pointer to the rule that was matched, if possible.
 * @enforce: Supplies the enforcement/permissive state at the point
 *	     the enforcement decision was made.
 */
void ipe_audit_match(const struct ipe_eval_ctx *const ctx,
		     enum ipe_match match_type,
		     enum ipe_action act, const struct ipe_rule *const r,
		     bool enforce)
{
	struct audit_buffer *ab;
	bool success_audit;

	rcu_read_lock();
	success_audit = READ_ONCE(ctx->ci_ctx->success_audit);
	rcu_read_unlock();

	if (act != ipe_action_deny && !success_audit)
		return;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_TRUST_RESULT);
	if (!ab)
		return;

	audit_log_format(ab, "IPE ");
	audit_eval_ctx(ab, ctx, enforce);
	audit_log_format(ab, " ");

	if (match_type == ipe_match_rule)
		audit_rule(ab, r);
	else if (match_type == ipe_match_table)
		audit_log_format(ab, "rule=\"DEFAULT op=%s action=%s\"",
				 audit_op_names[ctx->op], ACTSTR(act));
	else
		audit_log_format(ab, "rule=\"DEFAULT action=%s\"",
				 ACTSTR(act));

	audit_log_end(ab);
}

/**
 * audit_policy: Audit a policy's name, version and thumprint to @ab
 * @ab: Supplies a pointer to the audit buffer to append to.
 * @p: Supplies a pointer to the policy to audit
 */
static void audit_policy(struct audit_buffer *ab,
			 const struct ipe_policy *const p)
{
	u8 *digest = NULL;
	struct crypto_shash *tfm;
	SHASH_DESC_ON_STACK(desc, tfm);

	tfm = crypto_alloc_shash(CONFIG_IPE_AUDIT_HASH_ALG, 0, 0);
	if (IS_ERR(tfm))
		return;

	desc->tfm = tfm;

	digest = kzalloc(crypto_shash_digestsize(tfm), GFP_KERNEL);
	if (!digest)
		goto out;

	if (crypto_shash_init(desc))
		goto out;

	if (crypto_shash_update(desc, p->pkcs7, p->pkcs7len))
		goto out;

	if (crypto_shash_final(desc, digest))
		goto out;

	audit_log_format(ab, POLICY_LOAD_FMT, p->parsed->name,
			 p->parsed->version.major, p->parsed->version.minor,
			 p->parsed->version.rev);
	audit_log_n_hex(ab, digest, crypto_shash_digestsize(tfm));

out:
	kfree(digest);
	crypto_free_shash(tfm);
}

/**
 * ipe_audit_policy_activation: Audit a policy being made the active policy.
 * @p: Supplies a pointer to the policy to audit
 */
void ipe_audit_policy_activation(const struct ipe_policy *const p)
{
	struct audit_buffer *ab;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_TRUST_POLICY_ACTIVATE);
	if (!ab)
		return;

	audit_policy(ab, p);

	audit_log_end(ab);
}

/**
 * ipe_audit_policy_load: Audit a policy being loaded into the kernel.
 * @p: Supplies a pointer to the policy to audit
 */
void ipe_audit_policy_load(const struct ipe_policy *const p)
{
	struct audit_buffer *ab;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_TRUST_POLICY_LOAD);
	if (!ab)
		return;

	audit_policy(ab, p);

	audit_log_end(ab);
}

/**
 * ipe_audit_enforce: Audit a change in IPE's enforcement state
 * @ctx: Supplies a pointer to the contexts whose state changed.
 */
void ipe_audit_enforce(const struct ipe_context *const ctx)
{
	struct audit_buffer *ab;
	bool enforcing = false;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_TRUST_STATUS);
	if (!ab)
		return;

	rcu_read_lock();
	enforcing = READ_ONCE(ctx->enforce);
	rcu_read_unlock();

	audit_log_format(ab, "IPE enforce=%d", enforcing);

	audit_log_end(ab);
}

/**
 * emit_enforcement: Emit the enforcement state of IPE started with.
 *
 * Return:
 * 0 - Always
 */
static int emit_enforcement(void)
{
	struct ipe_context *ctx = NULL;

	ctx = ipe_current_ctx();
	ipe_audit_enforce(ctx);
	ipe_put_ctx(ctx);
	return 0;
}

late_initcall(emit_enforcement);
