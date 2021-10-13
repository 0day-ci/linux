/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_CONTEXT_H
#define IPE_CONTEXT_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct ipe_context {
	refcount_t refcount;
	/* Protects concurrent writers */
	spinlock_t lock;

	struct work_struct free_work;
};

int __init ipe_init_ctx(void);
struct ipe_context __rcu **ipe_tsk_ctx(struct task_struct *tsk);
struct ipe_context *ipe_current_ctx(void);
struct ipe_context *ipe_get_ctx_rcu(struct ipe_context __rcu *ctx);
void ipe_put_ctx(struct ipe_context *ctx);

#endif /* IPE_CONTEXT_H */
