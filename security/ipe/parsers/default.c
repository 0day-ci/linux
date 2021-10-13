// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "ipe_parser.h"

static int set_op_default(enum ipe_operation op, enum ipe_action act,
			  struct ipe_parsed_policy *pol)
{
	size_t i, remap_len;
	const enum ipe_operation *remap;

	if (!ipe_is_op_alias(op, &remap, &remap_len)) {
		if (pol->rules[op].default_action != ipe_action_max)
			return -EBADMSG;

		pol->rules[op].default_action = act;
		return 0;
	}

	for (i = 0; i < remap_len; ++i) {
		if (pol->rules[remap[i]].default_action != ipe_action_max)
			return -EBADMSG;

		pol->rules[remap[i]].default_action = act;
	}

	return 0;
}

static int parse_default(const struct ipe_policy_line *line,
			 struct ipe_parsed_policy *pol)
{
	int rc = 0;
	size_t idx = 0;
	struct ipe_policy_token *tok = NULL;
	enum ipe_operation op = ipe_operation_max;
	enum ipe_action act = ipe_action_max;

	list_for_each_entry(tok, &line->tokens, next) {
		switch (idx) {
		case 0:
			if (strcmp("DEFAULT", tok->key) || tok->value)
				return -EBADMSG;
			break;
		case 1:
			/* schema 1 - operation, followed by action */
			rc = ipe_parse_op(tok, &op);
			if (!rc) {
				++idx;
				continue;
			}

			if (pol->global_default != ipe_action_max)
				return -EBADMSG;

			/* schema 2 - action */
			rc = ipe_parse_action(tok, &pol->global_default);
			if (!rc)
				return rc;

			return -EBADMSG;
		case 2:
			rc = ipe_parse_action(tok, &act);
			if (rc)
				return rc;

			return set_op_default(op, act, pol);
		default:
			return -EBADMSG;
		}
		++idx;
	}

	/* met no schema */
	return -EBADMSG;
}

static int validate_defaults(const struct ipe_parsed_policy *p)
{
	size_t i = 0;

	if (p->global_default != ipe_action_max)
		return 0;

	for (i = 0; i < ARRAY_SIZE(p->rules); ++i) {
		if (p->rules[i].default_action == ipe_action_max)
			return -EBADMSG;
	}

	return 0;
}

IPE_PARSER(default_decl) = {
	.first_token = "DEFAULT",
	.version = 1,
	.parse = parse_default,
	.free = NULL,
	.validate = validate_defaults,
};
