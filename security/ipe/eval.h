/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#ifndef IPE_EVAL_H
#define IPE_EVAL_H

#include <linux/file.h>

#include "ctx.h"
#include "hooks.h"
#include "policy.h"

struct ipe_eval_ctx {
	enum ipe_hook hook;
	enum ipe_operation op;

	const struct file *file;
	struct ipe_context *ci_ctx;
};

enum ipe_match {
	ipe_match_rule = 0,
	ipe_match_table,
	ipe_match_global,
	ipe_match_max
};

int ipe_process_event(const struct file *file, enum ipe_operation op,
		      enum ipe_hook hook);

#endif /* IPE_EVAL_H */
