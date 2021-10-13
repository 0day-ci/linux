/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_CONTEXT_H
#define IPE_CONTEXT_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/dcache.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct ipe_context {
	struct ipe_policy __rcu *active_policy;

	bool __rcu success_audit;

	refcount_t refcount;
	/* Protects concurrent writers */
	spinlock_t lock;

	struct list_head policies; /* type: ipe_policy */

	struct dentry *policy_root;

	struct work_struct free_work;
};

int __init ipe_init_ctx(void);
struct ipe_context __rcu **ipe_tsk_ctx(struct task_struct *tsk);
struct ipe_context *ipe_current_ctx(void);
struct ipe_context *ipe_get_ctx_rcu(struct ipe_context __rcu *ctx);
void ipe_put_ctx(struct ipe_context *ctx);
void ipe_add_policy(struct ipe_context *ctx, struct ipe_policy *p);
void ipe_remove_policy(struct ipe_policy *p);
int ipe_replace_policy(struct ipe_policy *old, struct ipe_policy *new);
int ipe_set_active_pol(const struct ipe_policy *p);
bool ipe_is_policy_active(const struct ipe_policy *p);

#endif /* IPE_CONTEXT_H */
