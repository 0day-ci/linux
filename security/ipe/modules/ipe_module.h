/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_MODULE_H
#define IPE_MODULE_H

#include <linux/types.h>
#include <linux/audit.h>
#include "../eval.h"

/**
 * ipe_module: definition of an extensible module for IPE properties.
 *	       These structures are used to implement 'key=value' pairs
 *	       in IPE policy, which will be evaluated on every IPE policy
 *	       evaluation.
 *
 *	       Integrity mechanisms should be define as a module, and modules
 *	       should manage their own dependencies via KConfig. @name is both
 *	       the key half of the key=value pair in the policy, and the unique
 *	       identifier for the module.
 */
struct ipe_module {
	const char			*const name;	/* required */
	u16				version;	/* required */
	int (*parse)(const char *valstr, void **value);	/* required */
	int (*free)(void **value);			/* optional */
	bool (*eval)(const struct ipe_eval_ctx *ctx,	/* required */
		     const void *val);
	void (*audit)(struct audit_buffer *ab, const void *val); /* required */
};

#define IPE_MODULE(parser)				\
	static struct ipe_module __ipe_module_##parser	\
		__used __section(".ipe_modules")	\
		__aligned(sizeof(unsigned long))

#endif /* IPE_MODULE_H */
