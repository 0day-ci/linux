// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#include <kunit/test.h>
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/list.h>
#include "ctx.h"
#include "policy.h"
#include "hooks.h"

struct eval_case {
	const char *const desc;
	const char *const policy;
	int		  errno;

	const struct file *fake_file;
	const struct ipe_bdev *bdev_sec;
	const struct ipe_inode *inode_sec;
	bool initsb;
};

static const u8 fake_digest[] = { 0xDE, 0xAD, 0xBE, 0xEF };

static const struct ipe_bdev fake_bdev_no_data = {};
static const struct ipe_bdev fake_bdev_no_sig = {
	.hash = fake_digest,
	.hashlen = ARRAY_SIZE(fake_digest),
};

static const struct ipe_bdev fake_bdev_signed = {
	.sigdata = fake_digest,
	.siglen = ARRAY_SIZE(fake_digest),
	.hash = fake_digest,
	.hashlen = ARRAY_SIZE(fake_digest),
};

static const struct ipe_inode fake_ino_no_data = {};

static const struct ipe_inode fake_ino_no_sig = {
	.hash = fake_digest,
	.hashlen = ARRAY_SIZE(fake_digest),
};

static const struct ipe_inode fake_ino_signed = {
	.sigdata = fake_digest,
	.siglen = ARRAY_SIZE(fake_digest),
	.hash = fake_digest,
	.hashlen = ARRAY_SIZE(fake_digest),
};

static struct inode fake_inode = {
	.i_flags = S_VERITY
};

static const struct file fake_verity = {
	.f_inode = &fake_inode,
};

static const struct eval_case cases[] = {
	{
		"boot_verified_trust_no_source",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE boot_verified=TRUE action=ALLOW\n"
		"op=KERNEL_READ boot_verified=TRUE action=ALLOW\n",
		-EACCES, NULL, NULL, NULL, false
	},
	{
		"boot_verified_distrust",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE boot_verified=FALSE action=ALLOW\n"
		"op=KERNEL_READ boot_verified=FALSE action=ALLOW\n",
		0, NULL, NULL, NULL, false
	},
	{
		"boot_verified_trust",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE boot_verified=TRUE action=ALLOW\n"
		"op=KERNEL_READ boot_verified=TRUE action=ALLOW\n",
		0, NULL, NULL, NULL, true
	},
	{
		"boot_verified_trust",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE boot_verified=FALSE action=ALLOW\n"
		"op=KERNEL_READ boot_verified=FALSE action=ALLOW\n",
		-EACCES, NULL, NULL, NULL, true
	},
	{
		"dmverity_signature_trust_no_bdev",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_signature=FALSE action=ALLOW\n"
		"op=KERNEL_READ dmverity_signature=FALSE action=ALLOW\n",
		0, NULL, NULL, NULL, true
	},
	{
		"dmverity_signature_distrust_no_bdev",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW\n"
		"op=KERNEL_READ dmverity_signature=TRUE action=ALLOW\n",
		-EACCES, NULL, NULL, NULL, false
	},
	{
		"dmverity_signature_distrust_sigdata",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_signature=FALSE action=ALLOW\n"
		"op=KERNEL_READ dmverity_signature=FALSE action=ALLOW\n",
		-EACCES, NULL, &fake_bdev_signed, &fake_ino_no_data, false
	},
	{
		"dmverity_signature_trust_sigdata",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW\n"
		"op=KERNEL_READ dmverity_signature=TRUE action=ALLOW\n",
		0, NULL, &fake_bdev_signed, &fake_ino_no_data, true
	},
	{
		"dmverity_roothash_trust_no_bdev",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_roothash=DEADBEEF action=ALLOW\n"
		"op=KERNEL_READ dmverity_roothash=DEADBEEF action=ALLOW\n",
		-EACCES, NULL, NULL, NULL, true
	},
	{
		"dmverity_roothash_distrust_no_bdev",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_roothash=deadbeef action=DENY\n"
		"op=KERNEL_READ dmverity_roothash=deadbeef action=DENY\n",
		0, NULL, NULL, NULL, false
	},
	{
		"dmverity_roothash_trust_hash",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_roothash=DEADBEEF action=ALLOW\n"
		"op=KERNEL_READ dmverity_roothash=DEADBEEF action=ALLOW\n",
		0, NULL, &fake_bdev_no_sig, &fake_ino_no_data, false
	},
	{
		"dmverity_roothash_distrust_hash",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE dmverity_roothash=DEADBEEF action=DENY\n"
		"op=KERNEL_READ dmverity_roothash=DEADBEEF action=DENY\n",
		-EACCES, NULL, &fake_bdev_no_sig, &fake_ino_no_data, false
	},
	{
		"dmverity_signature_revoke_hash",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE dmverity_roothash=DEADBEEF action=DENY\n"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW\n"
		"op=KERNEL_READ dmverity_roothash=DEADBEEF action=DENY\n"
		"op=KERNEL_READ dmverity_signature=TRUE action=ALLOW\n",
		-EACCES, NULL, &fake_bdev_signed, &fake_ino_no_data, false
	},
	{
		"fsverity_signature_trust_sigdata",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE fsverity_signature=TRUE action=ALLOW\n"
		"op=KERNEL_READ fsverity_signature=TRUE action=ALLOW\n",
		0, &fake_verity, &fake_bdev_no_data, &fake_ino_signed, false
	},
	{
		"fsverity_signature_distrust_sigdata",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE fsverity_signature=TRUE action=DENY\n"
		"op=KERNEL_READ fsverity_signature=TRUE action=DENY\n",
		-EACCES, &fake_verity, &fake_bdev_no_data, &fake_ino_signed, false
	},
	{
		"fsverity_signature_trust_no_sigdata",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE fsverity_signature=FALSE action=ALLOW\n"
		"op=KERNEL_READ fsverity_signature=FALSE action=ALLOW\n",
		0, &fake_verity, &fake_bdev_signed, &fake_ino_no_sig, true
	},
	{
		"fsverity_signature_distrust_no_sigdata",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=ALLOW\n"
		"op=EXECUTE fsverity_signature=FALSE action=DENY\n"
		"op=KERNEL_READ fsverity_signature=FALSE action=DENY\n",
		-EACCES, &fake_verity, &fake_bdev_signed, &fake_ino_no_sig, true
	},
	{
		"fsverity_digest_trust_hash",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE fsverity_digest=DEADBEEF action=ALLOW\n"
		"op=KERNEL_READ fsverity_digest=DEADBEEF action=ALLOW\n",
		0, &fake_verity, &fake_bdev_signed, &fake_ino_no_sig, true
	},
	{
		"fsverity_digest_revoke_hash",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE fsverity_digest=DEADBEEF action=DENY\n"
		"op=EXECUTE fsverity_signature=TRUE action=ALLOW\n"
		"op=KERNEL_READ fsverity_digest=DEADBEEF action=DENY\n"
		"op=KERNEL_READ fsverity_signature=TRUE action=ALLOW\n",
		-EACCES, &fake_verity, &fake_bdev_signed, &fake_ino_signed, true
	},
	{
		"dmverity_signature_revoke_fsverity_digest",
		"policy_name='Test' policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"op=EXECUTE fsverity_digest=DEADBEEF action=DENY\n"
		"op=EXECUTE dmverity_signature=TRUE action=ALLOW\n"
		"op=KERNEL_READ fsverity_digest=DEADBEEF action=DENY\n"
		"op=KERNEL_READ dmverity_signature=TRUE action=ALLOW\n",
		-EACCES, &fake_verity, &fake_bdev_signed, &fake_ino_signed, false
	},
};

static void case_to_desc(const struct eval_case *c, char *desc)
{
	strncpy(desc, c->desc, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ipe_eval, cases, case_to_desc);

/**
 * fake_free_ctx: Fake function to deallocate a context structure.
 */
static void fake_free_ctx(struct ipe_context *ctx)
{
	struct ipe_policy *p = NULL;

	list_for_each_entry(p, &ctx->policies, next)
		ipe_put_policy(p);

	kfree(ctx);
}

/**
 * create_fake_ctx: Build a fake ipe_context for use
 *		    in a test.
 * Return:
 * !IS_ERR - OK
 */
static struct ipe_context *create_fake_ctx(void)
{
	struct ipe_context *ctx = NULL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ctx->policies);
	refcount_set(&ctx->refcount, 1);
	spin_lock_init(&ctx->lock);
	WRITE_ONCE(ctx->enforce, true);

	return ctx;
}

/**
 * ipe_ctx_eval_test: Parse a policy, and run a mock through the
 *		      evaluation loop to check the functional result.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_eval_test(struct kunit *test)
{
	int rc = 0;
	enum ipe_operation i = ipe_operation_exec;
	struct ipe_policy *pol = NULL;
	struct ipe_context *ctx = NULL;
	struct ipe_eval_ctx eval = { 0 };
	const struct eval_case *t = test->param_value;

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	pol = ipe_new_policy(t->policy, strlen(t->policy), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pol);

	ipe_add_policy(ctx, pol);
	KUNIT_ASSERT_EQ(test, 0, ipe_set_active_pol(pol));
	KUNIT_EXPECT_EQ(test, refcount_read(&pol->refcount), 2);
	KUNIT_EXPECT_PTR_EQ(test, pol->policyfs, NULL);
	KUNIT_EXPECT_PTR_EQ(test, pol->pkcs7, NULL);

	eval.hook = ipe_hook_max;
	eval.ipe_bdev = t->bdev_sec;
	eval.ipe_inode = t->inode_sec;
	eval.from_init_sb = t->initsb;
	eval.ci_ctx = ctx;
	eval.file = t->fake_file;

	for (i = ipe_operation_exec; i < ipe_operation_max; ++i) {
		eval.op = i;
		rc = evaluate(&eval);
		KUNIT_EXPECT_EQ(test, rc, t->errno);
	}

	fake_free_ctx(ctx);
	ipe_put_policy(pol);
}

/**
 * ipe_ctx_eval_permissive_test: Parse a policy, and run a mock through the
 *				 evaluation loop to with permissive on,
 *				 checking the functional result.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_eval_permissive_test(struct kunit *test)
{
	int rc = 0;
	enum ipe_operation i = ipe_operation_exec;
	struct ipe_policy *pol = NULL;
	struct ipe_context *ctx = NULL;
	struct ipe_eval_ctx eval = { 0 };
	const struct eval_case *t = test->param_value;

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	WRITE_ONCE(ctx->enforce, false);

	pol = ipe_new_policy(t->policy, strlen(t->policy), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pol);

	ipe_add_policy(ctx, pol);
	KUNIT_ASSERT_EQ(test, 0, ipe_set_active_pol(pol));
	KUNIT_EXPECT_EQ(test, refcount_read(&pol->refcount), 2);
	KUNIT_EXPECT_PTR_EQ(test, pol->policyfs, NULL);
	KUNIT_EXPECT_PTR_EQ(test, pol->pkcs7, NULL);

	eval.hook = ipe_hook_max;
	eval.ipe_bdev = t->bdev_sec;
	eval.ipe_inode = t->inode_sec;
	eval.from_init_sb = t->initsb;
	eval.ci_ctx = ctx;
	eval.file = t->fake_file;

	for (i = ipe_operation_exec; i < ipe_operation_max; ++i) {
		eval.op = i;
		rc = evaluate(&eval);
		KUNIT_EXPECT_EQ(test, rc, 0);
	}

	fake_free_ctx(ctx);
	ipe_put_policy(pol);
}

/**
 * ipe_ctx_default_eval_test: Ensure an operation-level default
 *			      is taken over a global-level default.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_default_eval_test(struct kunit *test)
{
	int rc = 0;
	struct ipe_policy *pol = NULL;
	struct ipe_context *ctx = NULL;
	struct ipe_eval_ctx eval = { 0 };
	const char *const policy =
		"policy_name=Test policy_version=0.0.0\n"
		"DEFAULT action=DENY\n"
		"DEFAULT op=EXECUTE action=ALLOW";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	pol = ipe_new_policy(policy, strlen(policy), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pol);
	KUNIT_EXPECT_EQ(test, pol->parsed->global_default, ipe_action_deny);
	KUNIT_EXPECT_EQ(test, pol->parsed->rules[ipe_operation_exec].default_action,
			ipe_action_allow);

	ipe_add_policy(ctx, pol);
	KUNIT_ASSERT_EQ(test, 0, ipe_set_active_pol(pol));
	KUNIT_EXPECT_EQ(test, refcount_read(&pol->refcount), 2);
	KUNIT_EXPECT_PTR_EQ(test, pol->policyfs, NULL);
	KUNIT_EXPECT_PTR_EQ(test, pol->pkcs7, NULL);

	eval.hook = ipe_hook_max;
	eval.ipe_bdev = NULL;
	eval.ipe_inode = NULL;
	eval.from_init_sb = NULL;
	eval.ci_ctx = ctx;
	eval.file = NULL;
	eval.op = ipe_operation_exec;

	rc = evaluate(&eval);
	KUNIT_EXPECT_EQ(test, rc, 0);

	eval.op = ipe_operation_kexec_image;
	rc = evaluate(&eval);
	KUNIT_EXPECT_EQ(test, rc, -EACCES);

	fake_free_ctx(ctx);
	ipe_put_policy(pol);
}

/**
 * ipe_ctx_replace_policy - Associate a policy with a context, then replace it.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_replace_policy(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_policy *pp = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.0\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t policy_version=0.0.1\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);
	p2 = ipe_new_policy(policy2, strlen(policy2), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p2);

	ipe_add_policy(ctx, p1);
	KUNIT_EXPECT_TRUE(test, list_is_singular(&ctx->policies));

	pp = list_first_entry(&ctx->policies, struct ipe_policy, next);
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);

	ipe_replace_policy(p1, p2);
	KUNIT_EXPECT_TRUE(test, list_is_singular(&ctx->policies));
	pp = list_first_entry(&ctx->policies, struct ipe_policy, next);
	KUNIT_EXPECT_PTR_EQ(test, pp, p2);

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

/**
 * ipe_ctx_replace_policy - Associate a policy with a context, mark the policy active,
 *			    then replace it.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_replace_active_policy(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_policy *pp = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.0\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t policy_version=0.0.1\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);
	p2 = ipe_new_policy(policy2, strlen(policy2), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p2);

	ipe_add_policy(ctx, p1);
	KUNIT_ASSERT_EQ(test, 0, ipe_set_active_pol(p1));

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);
	ipe_put_policy(pp);

	ipe_replace_policy(p1, p2);

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p2);
	ipe_put_policy(pp);

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

/**
 * ipe_ctx_update_policy - Associate a policy with a context, then update it.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness. This function differs from replace above,
 * as it performs additional error checking.
 */
static void ipe_ctx_update_policy(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_policy *pp = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.0\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t policy_version=0.0.1\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);

	ipe_add_policy(ctx, p1);
	ipe_set_active_pol(p1);

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);
	ipe_put_policy(pp);

	p2 = ipe_update_policy(p1, policy2, strlen(policy2), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p2);

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p2);
	ipe_put_policy(pp);

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

/**
 * ipe_ctx_update_wrong_policy - Associate a policy with a context, then
 *				 attempt update it with the wrong policy.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_update_wrong_policy(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_policy *pp = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.0\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t2 policy_version=0.0.0\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);

	ipe_add_policy(ctx, p1);
	ipe_set_active_pol(p1);

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);
	ipe_put_policy(pp);

	p2 = ipe_update_policy(p1, policy2, strlen(policy2), NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(p2), -EINVAL);

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);
	ipe_put_policy(pp);

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

/**
 * ipe_ctx_update_wrong_policy - Associate a policy with a context, mark it active,
 *				 then attempt update it with a stale policy.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_update_rollback_policy(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_policy *pp = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.1\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t policy_version=0.0.0\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);

	ipe_add_policy(ctx, p1);
	KUNIT_ASSERT_EQ(test, 0, ipe_set_active_pol(p1));

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);
	ipe_put_policy(pp);

	p2 = ipe_update_policy(p1, policy2, strlen(policy2), NULL, 0);
	KUNIT_EXPECT_EQ(test, PTR_ERR(p2), -EINVAL);

	rcu_read_lock();
	pp = ipe_get_policy_rcu(ctx->active_policy);
	rcu_read_unlock();
	KUNIT_EXPECT_PTR_EQ(test, pp, p1);
	ipe_put_policy(pp);

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

/**
 * ipe_ctx_rollback - Associate two policies with a context, then
 *		      attempt rollback the active policy.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_rollback(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.1\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t2 policy_version=0.0.0\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);
	ipe_add_policy(ctx, p1);
	KUNIT_ASSERT_EQ(test, 0, ipe_set_active_pol(p1));

	p2 = ipe_new_policy(policy2, strlen(policy2), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p2);
	ipe_add_policy(ctx, p2);
	KUNIT_ASSERT_EQ(test, -EINVAL, ipe_set_active_pol(p2));

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

/**
 * ipe_ctx_update_rollback_inactive - Associate a policy with a context, then
 *				      attempt update it with a stale policy.
 * @test: Supplies a pointer to a kunit structure.
 *
 * This is called by the kunit harness.
 */
static void ipe_ctx_update_rollback_inactive(struct kunit *test)
{
	struct ipe_policy *p1 = NULL;
	struct ipe_policy *p2 = NULL;
	struct ipe_context *ctx = NULL;
	const char *const policy1 = "policy_name=t policy_version=0.0.1\n"
				    "DEFAULT action=ALLOW";
	const char *const policy2 = "policy_name=t policy_version=0.0.0\n"
				    "DEFAULT action=DENY\n";

	ctx = create_fake_ctx();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	p1 = ipe_new_policy(policy1, strlen(policy1), NULL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p1);

	ipe_add_policy(ctx, p1);

	p2 = ipe_update_policy(p1, policy2, strlen(policy2), NULL, 0);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, p2);

	fake_free_ctx(ctx);
	ipe_put_policy(p1);
	ipe_put_policy(p2);
}

static struct kunit_case ipe_ctx_test_cases[] = {
	KUNIT_CASE_PARAM(ipe_ctx_eval_test, ipe_eval_gen_params),
	KUNIT_CASE_PARAM(ipe_ctx_eval_permissive_test, ipe_eval_gen_params),
	KUNIT_CASE(ipe_ctx_default_eval_test),
	KUNIT_CASE(ipe_ctx_replace_active_policy),
	KUNIT_CASE(ipe_ctx_replace_policy),
	KUNIT_CASE(ipe_ctx_update_policy),
	KUNIT_CASE(ipe_ctx_update_wrong_policy),
	KUNIT_CASE(ipe_ctx_update_rollback_policy),
	KUNIT_CASE(ipe_ctx_update_rollback_inactive),
	KUNIT_CASE(ipe_ctx_rollback),
};

static struct kunit_suite ipe_ctx_test_suite = {
	.name = "ipe-context",
	.test_cases = ipe_ctx_test_cases,
};

kunit_test_suite(ipe_ctx_test_suite);
