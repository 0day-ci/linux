// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "policy.h"
#include "ipe_parser.h"

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/rbtree.h>

static struct rb_root ipe_parser_root = RB_ROOT;

struct parser_container {
	struct rb_node node;
	const struct ipe_parser *parser;
};

/**
 * cmp_key: Comparator for the nodes within the parser tree by key
 * @key: Supplies a the key to evaluate nodes against
 * @n: Supplies a pointer to the node to compare.
 *
 * Return:
 * <0 - @key is to the left of @n
 * 0 - @key identifies @n
 * >0 - @key is to the right of @n
 */
static int cmp_key(const void *key, const struct rb_node *n)
{
	const struct parser_container *node;

	node = container_of(n, struct parser_container, node);

	return strcmp((const char *)key, node->parser->first_token);
}

/**
 * cmp_node: Comparator for the nodes within the parser tree
 * @n: Supplies a pointer to the node to compare
 * @nn: Supplies a pointer to the another node to compare.
 *
 * Return:
 * <0 - @n is lexigraphically before @nn
 * 0 - @n is identical @nn
 * >0 - @n is lexigraphically after @nn
 */
static int cmp_node(struct rb_node *n, const struct rb_node *nn)
{
	const struct parser_container *c1;
	const struct parser_container *c2;

	c1 = container_of(n, struct parser_container, node);
	c2 = container_of(nn, struct parser_container, node);

	return strcmp(c1->parser->first_token, c2->parser->first_token);
}

/**
 * ipe_lookup_parser: Attempt to find a ipe_property structure by @first_token.
 * @first_token: The key to look for in the tree.
 *
 * Return:
 * !NULL - OK
 * NULL - No property exists under @key
 */
const struct ipe_parser *ipe_lookup_parser(const char *first_token)
{
	struct rb_node *n;

	n = rb_find(first_token, &ipe_parser_root, cmp_key);
	if (!n)
		return NULL;

	return container_of(n, struct parser_container, node)->parser;
}

/**
 * ipe_for_each_parser: Iterate over all currently-registered parsers
 *			calling @fn on the values, and providing @view @ctx.
 * @view: The function to call for each property. This is given the property
 *	  structure as the first argument, and @ctx as the second.
 * @ctx: caller-specified context that is passed to the function. Can be NULL.
 *
 * Return:
 * 0 - OK
 * !0 - Proper errno as returned by @view.
 */
int ipe_for_each_parser(int (*view)(const struct ipe_parser *parser,
				    void *ctx),
			void *ctx)
{
	int rc = 0;
	struct rb_node *node;
	struct parser_container *val;

	for (node = rb_first(&ipe_parser_root); node; node = rb_next(node)) {
		val = container_of(node, struct parser_container, node);

		rc = view(val->parser, ctx);
		if (rc)
			return rc;
	}

	return rc;
}

/**
 * ipe_register_parser: Register a parser to be used in IPE's policy.
 * @p: Parser to register.
 *
 * This function allows parsers (policy constructs that effect IPE's
 * internal functionality) to be leveraged in IPE policy. This must
 * be called prior to any policies being loaded.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_register_parser(struct ipe_parser *p)
{
	struct rb_node *n = NULL;
	struct parser_container *c = NULL;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->parser = p;

	n = rb_find_add(&c->node, &ipe_parser_root, cmp_node);
	if (n) {
		kfree(c);
		return -EEXIST;
	}

	return 0;
}
