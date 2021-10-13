// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "modules.h"

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/rbtree.h>

static struct rb_root module_root = RB_ROOT;

struct module_container {
	struct rb_node node;
	const struct ipe_module *mod;
};

/**
 * cmp_node: Comparator for a node in the module lookup tree.
 * @n: First node to compare
 * @nn: Second node to compare
 *
 * Return:
 * <0 - @n's key is lexigraphically before @nn.
 * 0 - n's key is identical to @nn
 * >0 - n's key is legxigraphically after @nn
 */
static int cmp_node(struct rb_node *n, const struct rb_node *nn)
{
	const struct module_container *c1;
	const struct module_container *c2;

	c1 = container_of(n, struct module_container, node);
	c2 = container_of(nn, struct module_container, node);

	return strcmp(c1->mod->name, c2->mod->name);
}

/**
 * cmp_key: Comparator to find a module in the tree by key.
 * @key: Supplies a pointer to a null-terminated string key
 * @n: Node to compare @key against
 *
 * Return:
 * <0 - Desired node is to the left of @n
 * 0  - @n is the desired node
 * >0 - Desired node is to the right of @n
 */
static int cmp_key(const void *key, const struct rb_node *n)
{
	struct module_container *mod;

	mod = container_of(n, struct module_container, node);

	return strcmp((const char *)key, mod->mod->name);
}

/**
 * ipe_lookup_module: Attempt to find a ipe_pmodule structure by @key.
 * @key: The key to look for in the tree.
 *
 * Return:
 * !NULL - OK
 * NULL - No property exists under @key
 */
const struct ipe_module *ipe_lookup_module(const char *key)
{
	struct rb_node *n;

	n = rb_find(key, &module_root, cmp_key);
	if (!n)
		return NULL;

	return container_of(n, struct module_container, node)->mod;
}

/**
 * ipe_register_module: Register a policy module to be used in IPE's policy.
 * @m: Module to register.
 *
 * This function allows parsers (policy constructs that represent integrations
 * with other subsystems, to be leveraged in rules) to be leveraged in IPE policy.
 * This must be called prior to any policies being loaded.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_register_module(struct ipe_module *m)
{
	struct rb_node *n = NULL;
	struct module_container *c = NULL;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->mod = m;

	n = rb_find_add(&c->node, &module_root, cmp_node);
	if (n) {
		kfree(c);
		return -EEXIST;
	}

	return 0;
}
