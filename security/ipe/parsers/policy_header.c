// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ipe_parser.h"

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "ipe_parser.h"

static int parse_name(const struct ipe_policy_token *t,
		      struct ipe_parsed_policy *p)
{
	if (p->name)
		return -EBADMSG;

	p->name = kstrdup_const(t->value, GFP_KERNEL);
	if (!p->name)
		return -ENOMEM;

	return 0;
}

static int parse_ver(const struct ipe_policy_token *t,
		     struct ipe_parsed_policy *p)
{
	int rc = 0;
	char *dup = NULL;
	char *dup2 = NULL;
	char *token = NULL;
	size_t sep_count = 0;
	u16 *const cv[] = { &p->version.major, &p->version.minor, &p->version.rev };

	dup = kstrdup(t->value, GFP_KERNEL);
	if (!dup) {
		rc = -ENOMEM;
		goto err;
	}

	dup2 = dup;

	while ((token = strsep(&dup, ".\n")) != NULL) {
		/* prevent overflow */
		if (sep_count >= ARRAY_SIZE(cv)) {
			rc = -EBADMSG;
			goto err;
		}

		rc = kstrtou16(token, 10, cv[sep_count]);
		if (rc)
			goto err;

		++sep_count;
	}

	/* prevent underflow */
	if (sep_count != ARRAY_SIZE(cv))
		rc = -EBADMSG;

err:
	kfree(dup2);
	return rc;
}

static const struct ipe_token_parser parsers[] = {
	{ .key = "policy_name", .parse_token = parse_name },
	{ .key = "policy_version", .parse_token = parse_ver },
};

static int parse_policy_hdr(const struct ipe_policy_line *line,
			    struct ipe_parsed_policy *pol)
{
	int rc = 0;
	size_t idx = 0;
	struct ipe_policy_token *tok = NULL;
	const struct ipe_token_parser *p = NULL;

	list_for_each_entry(tok, &line->tokens, next) {
		if (!tok->value || idx >= sizeof(parsers)) {
			rc = -EBADMSG;
			goto err;
		}

		p = &parsers[idx];

		if (strcmp(p->key, tok->key)) {
			rc = -EBADMSG;
			goto err;
		}

		rc = p->parse_token(tok, pol);
		if (rc)
			goto err;

		++idx;
	}

	return 0;

err:
	return rc;
}

static int free_policy_hdr(struct ipe_parsed_policy *pol)
{
	kfree(pol->name);
	return 0;
}

static int validate_policy_hdr(const struct ipe_parsed_policy *p)
{
	return !p->name ? -EBADMSG : 0;
}

IPE_PARSER(policy_header) = {
	.first_token = "policy_name",
	.version = 1,
	.parse = parse_policy_hdr,
	.free = free_policy_hdr,
	.validate = validate_policy_hdr,
};
