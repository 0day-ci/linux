/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_POLICY_H
#define IPE_POLICY_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/refcount.h>

struct ipe_policy_token {
	struct list_head next;		/* type: policy_token */

	const char *key;
	const char *value;
};

struct ipe_policy_line {
	struct list_head next;		/* type: policy_line */
	struct list_head tokens;	/* type: policy_token */

	bool consumed;
};

struct ipe_module;

enum ipe_operation {
	ipe_operation_exec = 0,
	ipe_operation_firmware,
	ipe_operation_kernel_module,
	ipe_operation_kexec_image,
	ipe_operation_kexec_initramfs,
	ipe_operation_ima_policy,
	ipe_operation_ima_x509,
	ipe_operation_max
};

/*
 * Extension to ipe_operation, representing operations
 * that are just one or more operations under the hood
 */
enum ipe_op_alias {
	ipe_op_alias_kernel_read = ipe_operation_max,
	ipe_op_alias_max,
};

enum ipe_action {
	ipe_action_allow = 0,
	ipe_action_deny,
	ipe_action_max,
};

struct ipe_policy_mod {
	const struct ipe_module *mod;
	void			*mod_value;

	struct list_head next;
};

struct ipe_rule {
	enum ipe_operation op;
	enum ipe_action action;

	struct list_head modules;

	struct list_head next;
};

struct ipe_operation_table {
	struct list_head rules;
	enum ipe_action default_action;
};

struct ipe_parsed_policy {
	const char *name;
	struct {
		u16 major;
		u16 minor;
		u16 rev;
	} version;

	enum ipe_action global_default;

	struct ipe_operation_table rules[ipe_operation_max];
};

struct ipe_policy {
	const char     *pkcs7;
	size_t		pkcs7len;

	const char     *text;
	size_t		textlen;

	struct ipe_parsed_policy *parsed;

	refcount_t	refcount;

	struct dentry *policyfs;
	struct list_head next;		/* type: ipe_policy */
	struct ipe_context __rcu *ctx;
};

struct ipe_policy *ipe_new_policy(const char *text, size_t textlen,
				  const char *pkcs7, size_t pkcs7len);
struct ipe_policy *ipe_update_policy(struct ipe_policy *old, const char *text,
				     size_t textlen, const char *pkcs7,
				     size_t pkcs7len);
void ipe_put_policy(struct ipe_policy *pol);
bool ipe_is_op_alias(int op, const enum ipe_operation **map, size_t *size);
struct ipe_policy *ipe_get_policy_rcu(struct ipe_policy __rcu *p);

#endif /* IPE_POLICY_H */
