/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#ifndef IPE_AUDIT_H
#define IPE_AUDIT_H

#include "ipe.h"
#include "eval.h"

#ifdef CONFIG_AUDIT
void ipe_audit_match(const struct ipe_eval_ctx *const ctx,
		     enum ipe_match match_type,
		     enum ipe_action act, const struct ipe_rule *const r,
		     bool enforce);
void ipe_audit_policy_load(const struct ipe_policy *const p);
void ipe_audit_policy_activation(const struct ipe_policy *const p);
void ipe_audit_enforce(const struct ipe_context *const ctx);
#else
static inline void ipe_audit_match(const struct ipe_eval_ctx *const ctx,
				   enum ipe_match match_type,
				   enum ipe_action act, const struct ipe_rule *const r,
				   bool enforce)
{
}

static inline void ipe_audit_policy_load(const struct ipe_policy *const p)
{
}

static inline void ipe_audit_policy_activation(const struct ipe_policy *const p)
{
}

static inline void ipe_audit_enforce(const struct ipe_context *const ctx)
{
}
#endif /* CONFIG_AUDIT */

#endif /* IPE_AUDIT_H */
