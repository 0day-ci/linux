/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_PARSER_H
#define IPE_PARSER_H

#include "policy.h"

#include <linux/list.h>
#include <linux/types.h>

/*
 * Struct used to define internal parsers that effect the policy,
 * but do not belong as policy modules, as they are not used to make
 * decisions in the event loop, and only effect the internal workings
 * of IPE.
 *
 * These structures are used in pass2, and policy deallocation.
 */
struct ipe_parser {
	u8 version;
	const char *first_token;

	int (*parse)(const struct ipe_policy_line *line,
		     struct ipe_parsed_policy *pol);
	int (*free)(struct ipe_parsed_policy *pol);
	int (*validate)(const struct ipe_parsed_policy *pol);
};

int ipe_parse_op(const struct ipe_policy_token *tok,
		 enum ipe_operation *op);

int ipe_parse_action(const struct ipe_policy_token *tok,
		     enum ipe_action *action);

/*
 * Optional struct to make structured parsers easier.
 */
struct ipe_token_parser {
	const char *key;
	int (*parse_token)(const struct ipe_policy_token *t,
			   struct ipe_parsed_policy *p);
};

const struct ipe_parser *ipe_lookup_parser(const char *first_token);

int ipe_for_each_parser(int (*view)(const struct ipe_parser *parser,
				    void *ctx),
			void *ctx);

int ipe_register_parser(struct ipe_parser *p);

#define IPE_PARSER(parser)				\
	static struct ipe_parser __ipe_parser_##parser	\
		__used __section(".ipe_parsers")	\
		__aligned(sizeof(unsigned long))

#endif /* IPE_PARSER_MODULE_H */
