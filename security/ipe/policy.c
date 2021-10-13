// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "fs.h"
#include "policy.h"
#include "ipe_parser.h"
#include "modules.h"

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/parser.h>
#include <linux/verification.h>

#define START_COMMENT	'#'
#define KEYVAL_DELIMIT	'='

static inline bool is_quote(char ch)
{
	return ch == '\'' || ch == '\"';
}

/**
 * is_key_char: Determine whether @ch is an acceptable character for a
 *		key type
 * @ch: Supplies the character to evaluate.
 *
 * Return:
 * true - Character is acceptable.
 * false - Character is not acceptable.
 */
static inline bool is_key_char(char ch)
{
	return isalnum(ch) || ch == '_';
}

/**
 * is_val_char: Determine whether @ch is an acceptable character for a
 *		value type
 * @ch: Supplies the character to evaluate.
 *
 * Return:
 * true - Character is acceptable.
 * false - Character is not acceptable.
 */
static inline bool is_val_char(char ch)
{
	return isgraph(ch) || ch == ' ' || ch == '\t';
}

/**
 * free_parser: Callback to invoke, freeing data allocated by parsers.
 * @parser: parser to free data.
 * @ctx: ctx object passed to ipe_for_each_parser.
 *
 * This function is intended to be used with ipe_for_each_parser only.
 *
 * Return:
 * 0 - Always
 */
static int free_parser(const struct ipe_parser *parser, void *ctx)
{
	struct ipe_parsed_policy *pol = ctx;

	if (parser->free)
		parser->free(pol);

	return 0;
}

/**
 * free_rule: free an ipe_rule.
 * @r: Supplies the rule to free.
 *
 * This function is safe to call if @r is NULL or ERR_PTR.
 */
static void free_rule(struct ipe_rule *r)
{
	struct ipe_policy_mod *p, *t;

	if (IS_ERR_OR_NULL(r))
		return;

	list_for_each_entry_safe(p, t, &r->modules, next) {
		if (p->mod->free)
			p->mod->free(p->mod_value);

		kfree(p);
	}

	kfree(r);
}

static void free_parsed_policy(struct ipe_parsed_policy *p)
{
	size_t i = 0;
	struct ipe_rule *pp, *t;

	if (IS_ERR_OR_NULL(p))
		return;

	for (i = 0; i < ARRAY_SIZE(p->rules); ++i)
		list_for_each_entry_safe(pp, t, &p->rules[i].rules, next)
			free_rule(pp);

	(void)ipe_for_each_parser(free_parser, p);
	kfree(p);
}

/**
 * free_parsed_line: free a single parsed line of tokens.
 * @line: Supplies the line to free.
 *
 * This function is safe to call if @line is NULL or ERR_PTR.
 */
static void free_parsed_line(struct ipe_policy_line *line)
{
	struct ipe_policy_token *p, *t;

	if (IS_ERR_OR_NULL(line))
		return;

	list_for_each_entry_safe(p, t, &line->tokens, next)
		kfree(p);
}

/**
 * free_parsed_text: free a 2D list representing a tokenized policy.
 * @parsed: Supplies the policy to free.
 *
 * This function is safe to call if @parsed is NULL or ERR_PTR.
 */
static void free_parsed_text(struct list_head *parsed)
{
	struct ipe_policy_line *p, *t;

	if (IS_ERR_OR_NULL(parsed))
		return;

	list_for_each_entry_safe(p, t, parsed, next)
		free_parsed_line(p);
}

/**
 * trim_quotes: Edit @str to remove a single instance of a trailing and
 *		leading quotes.
 * @str: Supplies the string to edit.
 *
 * If the string is not quoted, @str will be returned. This function is
 * safe to call if @str is NULL.
 *
 * Return:
 * !0 - OK
 * ERR_PTR(-EBADMSG) - Quote mismatch.
 */
static char *trim_quotes(char *str)
{
	char s;
	size_t len;

	if (!str)
		return str;

	s = *str;

	if (is_quote(s)) {
		len = strlen(str) - 1;

		if (str[len] != s)
			return ERR_PTR(-EBADMSG);

		str[len] = '\0';
		++str;
	}

	return str;
}

/**
 * parse_token: Parse a string into a proper token representation.
 * @token: Supplies the token string to parse.
 *
 * @token will be edited destructively. Pass a copy if you wish to retain
 * the state of the original.
 *
 * This function will emit an error to pr_err when a parsing error occurs.
 *
 * Return:
 * !0 - OK
 * ERR_PTR(-EBADMSG) - An invalid character was encountered while parsing.
 * ERR_PTR(-ENOMEM) - No Memory
 */
static struct ipe_policy_token *parse_token(char *token)
{
	size_t i, len = 0;
	char *key = token;
	char *value = NULL;
	struct ipe_policy_token *local = NULL;

	len = strlen(token);

	for (i = 0; (i < len) && token[i] != KEYVAL_DELIMIT; ++i)
		if (!is_key_char(token[i]))
			return ERR_PTR(-EBADMSG);

	token[i] = '\0';
	++i;

	/* there is a value */
	if (i < len) {
		value = trim_quotes(&token[i]);
		if (IS_ERR(value))
			return ERR_PTR(-EBADMSG);

		len = strlen(value);

		for (i = 0; i < len; ++i)
			if (!is_val_char(value[i]))
				return ERR_PTR(-EBADMSG);
	}

	local = kzalloc(sizeof(*local), GFP_KERNEL);
	if (!local)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&local->next);
	local->key = key;
	local->value = value;

	return local;
}

/**
 * append_token: Parse and append a token into an ipe_policy_line structure.
 * @p: Supplies the ipe_policy_line structure to append to.
 * @token: Supplies the token to parse and append to.
 *
 * @token will be edited during the parsing destructively. Pass a copy if you
 * wish to retain the original.
 *
 * Return:
 * 0 - OK
 * -EBADMSG - Parsing error of @token
 */
static int append_token(struct ipe_policy_line *p, char *token)
{
	struct ipe_policy_token *t = NULL;

	t = parse_token(token);
	if (IS_ERR(t))
		return PTR_ERR(t);

	list_add_tail(&t->next, &p->tokens);

	return 0;
}

/**
 * alloc_line: Allocate an ipe_policy_line structure.
 *
 * Return:
 * !0 - OK
 * -EBADMSG - Parsing error of @token
 */
static struct ipe_policy_line *alloc_line(void)
{
	struct ipe_policy_line *l = NULL;

	l = kzalloc(sizeof(*l), GFP_KERNEL);
	if (!l)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&l->next);
	INIT_LIST_HEAD(&l->tokens);

	return l;
}

/**
 * insert_token: Append a token to @line.
 * @token: Supplies the token to append to @line.
 * @line: Supplies a pointer to the ipe_policy_line structure to append to.
 *
 * If @line is NULL, it will be allocated on behalf of the caller.
 *
 * Return:
 * 0 - OK
 * -ENOMEM - No Memory
 * -EBADMSG - Parsing error of @token
 */
static int insert_token(char *token, struct ipe_policy_line **line)
{
	int rc = 0;
	struct ipe_policy_line *local = *line;

	if (!local) {
		local = alloc_line();
		if (IS_ERR(local))
			return PTR_ERR(local);

		*line = local;
	}

	rc = append_token(local, token);
	if (rc)
		return rc;

	return 0;
}

/**
 * ipe_tokenize_line: Parse a line of text into a list of token structures.
 * @line: Supplies the line to parse.
 *
 * The final result can be NULL, which represents no tokens were parsed.
 *
 * Return:
 * !0 - OK
 * NULL - OK, no tokens were parsed.
 * ERR_PTR(-EBADMSG) - Invalid policy syntax
 * ERR_PTR(-ENOMEM) - No Memory
 */
static struct ipe_policy_line *tokenize_line(char *line)
{
	int rc = 0;
	size_t i = 0;
	size_t len = 0;
	char *tok = NULL;
	char quote = '\0';
	struct ipe_policy_line *p = NULL;

	/* nullterm guaranteed by strsep */
	len = strlen(line);

	for (i = 0; i < len; ++i) {
		if (quote == '\0' && is_quote(line[i])) {
			quote = line[i];
			continue;
		}

		if (quote != '\0' && line[i] == quote) {
			quote = '\0';
			continue;
		}

		if (quote == '\0' && line[i] == START_COMMENT) {
			tok = NULL;
			break;
		}

		if (isgraph(line[i]) && !tok)
			tok = &line[i];

		if (quote == '\0' && isspace(line[i])) {
			line[i] = '\0';

			if (!tok)
				continue;

			rc = insert_token(tok, &p);
			if (rc)
				goto err;

			tok = NULL;
		}
	}

	if (quote != '\0') {
		rc = -EBADMSG;
		goto err;
	}

	if (tok) {
		rc = insert_token(tok, &p);
		if (rc)
			goto err;
	}

	return p;

err:
	free_parsed_line(p);
	return ERR_PTR(rc);
}

/**
 * parse_pass1: parse @policy into a 2D list, representing tokens on each line.
 * @policy: Supplies the policy to parse. Must be nullterminated, and is
 *	    edited.
 *
 * In pass1 of the parser, the policy is tokenized. Minor structure checks
 * are done (mismatching quotes, invalid characters).
 *
 * Caller must maintain the lifetime of @policy while the return value is
 * alive.
 *
 * Return:
 * !0 - OK
 * ERR_PTR(-ENOMEM) - Out of Memory
 * ERR_PTR(-EBADMSG) - Parsing Error
 */
static int parse_pass1(char *policy, struct list_head *tokens)
{
	int rc = 0;
	char *p = NULL;

	while ((p = strsep(&policy, "\n\0")) != NULL) {
		struct ipe_policy_line *t = NULL;

		t = tokenize_line(p);
		if (IS_ERR(t)) {
			rc = PTR_ERR(t);
			goto err_free_parsed;
		}

		if (!t)
			continue;

		list_add_tail(&t->next, tokens);
	}

	return 0;

err_free_parsed:
	free_parsed_text(tokens);
	return rc;
}

/**
 * parse_pass2: Take the 2D list of tokens generated from pass1, and transform
 *		it into a partial ipe_policy.
 * @parsed: Supplies the list of tokens generated from pass1.
 * @p: Policy to manipulate with parsed tokens.
 *
 * This function is where various declarations and references are parsed into
 * policy. All declarations and references required to parse rules should be
 * done here as a parser, and then in pass3 these can be utilized.
 *
 * Return:
 * !0 - OK
 * -EBADMSG - Syntax Parsing Errors
 * -ENOENT - No handler for a token.
 * -ENOMEM - Out of memory
 */
static int parse_pass2(struct list_head *parsed, struct ipe_parsed_policy *pol)
{
	int rc = 0;
	const struct ipe_parser *p = NULL;
	struct ipe_policy_line *line = NULL;
	const struct ipe_policy_token *token = NULL;

	list_for_each_entry(line, parsed, next) {
		token = list_first_entry(&line->tokens, struct ipe_policy_token, next);
		p = ipe_lookup_parser(token->key);
		if (!p)
			continue;

		rc = p->parse(line, pol);
		if (rc)
			return rc;

		line->consumed = true;
	}

	return rc;
}

/**
 * ipe_parse_op: parse a token to an operation value.
 * @tok: Token to parse
 * @op: Operation Parsed.
 *
 * Return:
 * 0 - OK
 * -EINVAL - Invalid key or value.
 */
int ipe_parse_op(const struct ipe_policy_token *tok,
		 enum ipe_operation *op)
{
	substring_t match[MAX_OPT_ARGS] = { 0 };
	const match_table_t ops = {
		{ ipe_op_alias_max, NULL },
	};

	if (strcmp(tok->key, "op") || !tok->value)
		return -EINVAL;

	*op = match_token((char *)tok->value, ops, match);
	if ((*op) == (int)ipe_op_alias_max)
		return -ENOENT;

	return 0;
}

/**
 * ipe_parse_action: parse a token to an operation value.
 * @tok: Token to parse
 * @action: action parsed.
 *
 * Return:
 * 0 - OK
 * -EINVAL - Invalid key or value.
 */
int ipe_parse_action(const struct ipe_policy_token *tok,
		     enum ipe_action *action)
{
	substring_t match[MAX_OPT_ARGS] = { 0 };
	const match_table_t actions = {
		{ ipe_action_allow, "ALLOW" },
		{ ipe_action_deny, "DENY" },
		{ ipe_action_max, NULL },
	};

	if (strcmp(tok->key, "action") || !tok->value)
		return -EINVAL;

	*action = match_token((char *)tok->value, actions, match);

	if (*action == ipe_action_max)
		return -EINVAL;

	return 0;
}

/**
 * parse_mod_to_rule: Parse a module token and append the values to the
 *		      provided rule.
 * @t: Supplies the token to parse.
 * @r: Supplies the rule to modify with the result.
 *
 * Return:
 * 0 - OK
 * -ENOENT - No such module to handle @t.
 * -ENOMEM - No memory.
 * Others - Module defined errors.
 */
static int parse_mod_to_rule(const struct ipe_policy_token *t, struct ipe_rule *r)
{
	int rc = 0;
	struct ipe_policy_mod *p = NULL;
	const struct ipe_module *m = NULL;

	m = ipe_lookup_module(t->key);
	if (IS_ERR_OR_NULL(m)) {
		rc = (m) ? PTR_ERR(m) : -ENOENT;
		goto err;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		rc = -ENOMEM;
		goto err;
	}
	INIT_LIST_HEAD(&p->next);
	p->mod = m;

	rc = m->parse(t->value, &p->mod_value);
	if (rc)
		goto err2;

	list_add_tail(&p->next, &r->modules);
	return 0;
err2:
	kfree(p);
err:
	return rc;
}

/**
 * parse_rule: Parse a policy line into an ipe_rule structure.
 * @line: Supplies the line to parse.
 *
 * Return:
 * Valid ipe_rule - OK
 * ERR_PTR(-ENOMEM) - Out of Memory
 * ERR_PTR(-ENOENT) - No such module to handle a token
 * ERR_PTR(-EINVAL) - An unacceptable value has been encountered.
 * ERR_PTR(...) - Module defined errors.
 */
static struct ipe_rule *parse_rule(const struct ipe_policy_line *line)
{
	int rc = 0;
	struct ipe_rule *r = NULL;
	const struct list_head *node = NULL;

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r) {
		rc = -ENOMEM;
		goto err;
	}

	INIT_LIST_HEAD(&r->next);
	INIT_LIST_HEAD(&r->modules);
	r->op = (int)ipe_op_alias_max;
	r->action = ipe_action_max;

	list_for_each(node, &line->tokens) {
		const struct ipe_policy_token *token = NULL;

		token = container_of(node, struct ipe_policy_token, next);

		if (list_is_first(node, &line->tokens)) {
			enum ipe_operation op;

			rc = ipe_parse_op(token, &op);
			if (rc)
				goto err;

			r->op = op;
			continue;
		}

		if (list_is_last(node, &line->tokens)) {
			enum ipe_action action;

			rc = ipe_parse_action(token, &action);
			if (rc)
				goto err;

			r->action = action;
			continue;
		}

		rc = parse_mod_to_rule(token, r);
		if (rc)
			goto err;
	}

	if (r->action == ipe_action_max || r->op == (int)ipe_op_alias_max) {
		rc = -EBADMSG;
		goto err;
	}

	return r;
err:
	free_rule(r);
	return ERR_PTR(rc);
}

/**
 * parse_pass3: Take the partially parsed list of tokens from pass 1 and the
 *		parial policy from pass 2, and finalize the policy.
 * @parsed: Supplies the tokens parsed from pass 1.
 * @p: Supplies the partial policy from pass 2.
 *
 * This function finalizes the IPE policy by parsing all rules in the
 * policy. This must occur in pass3, as in pass2, references are resolved
 * that can be used in pass3.
 *
 * Return:
 * 0 - OK
 * !0 - Standard errno
 */
static int parse_pass3(struct list_head *parsed,
		       struct ipe_parsed_policy *p)
{
	int rc = 0;
	size_t i = 0;
	size_t remap_len = 0;
	struct ipe_rule *rule = NULL;
	struct ipe_policy_line *line = NULL;
	const enum ipe_operation *remap;

	list_for_each_entry(line, parsed, next) {
		if (line->consumed)
			continue;

		rule = parse_rule(line);
		if (IS_ERR(rule)) {
			rc = PTR_ERR(rule);
			goto err;
		}

		if (ipe_is_op_alias(rule->op, &remap, &remap_len)) {
			for (i = 0; i < remap_len; ++i) {
				rule->op = remap[i];
				list_add_tail(&rule->next, &p->rules[rule->op].rules);
				rule = parse_rule(line);
			}

			free_rule(rule);
		} else {
			list_add_tail(&rule->next, &p->rules[rule->op].rules);
		}

		line->consumed = true;
	}

	return 0;
err:
	free_rule(rule);
	return rc;
}

/**
 * parser_validate: Callback to invoke, validating parsers as necessary
 * @parser: parser to call to validate data.
 * @ctx: ctx object passed to ipe_for_each_parser.
 *
 * This function is intended to be used with ipe_for_each_parser only.
 *
 * Return:
 * 0 - OK
 * !0 - Validation failed.
 */
static int parser_validate(const struct ipe_parser *parser, void *ctx)
{
	int rc = 0;
	const struct ipe_parsed_policy *pol = ctx;

	if (parser->validate)
		rc = parser->validate(pol);

	return rc;
}

/**
 * validate_policy: Given a policy structure that was just parsed, validate
 *		    that all necessary fields are present, initialized
 *		    correctly, and all lines parsed are have been consumed.
 * @parsed: Supplies the policy lines that were parsed in pass1.
 * @policy: Supplies the fully parsed policy.
 *
 * A parsed policy can be an invalid state for use (a default was undefined,
 * a header was undefined) by just parsing the policy.
 *
 * Return:
 * 0 - OK
 * -EBADMSG - Policy is invalid.
 */
static int validate_policy(const struct list_head *parsed,
			   const struct ipe_parsed_policy *p)
{
	int rc = 0;
	const struct ipe_policy_line *line = NULL;

	list_for_each_entry(line, parsed, next) {
		if (!line->consumed)
			return -EBADMSG;
	}

	rc = ipe_for_each_parser(parser_validate,
				 (struct ipe_parsed_policy *)p);

	return rc;
}

/**
 * new_parsed_policy: Allocate and initialize a parsed policy to its default
 *		      values.
 *
 * Return:
 * !IS_ERR - OK
 */
static struct ipe_parsed_policy *new_parsed_policy(void)
{
	size_t i = 0;
	struct ipe_parsed_policy *p = NULL;
	struct ipe_operation_table *t = NULL;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	p->global_default = ipe_action_max;

	for (i = 0; i < ARRAY_SIZE(p->rules); ++i) {
		t = &p->rules[i];

		t->default_action = ipe_action_max;
		INIT_LIST_HEAD(&t->rules);
	}

	return p;
}

/**
 * parse_policy: Given a string, parse the string into an IPE policy
 *		     structure.
 * @p: partially filled ipe_policy structure to populate with the result.
 *
 * @p must have text and textlen set.
 *
 * Return:
 * Valid ipe_policy structure - OK
 * ERR_PTR(-EBADMSG) - Invalid Policy Syntax (Unrecoverable)
 * ERR_PTR(-ENOMEM) - Out of Memory
 */
static int parse_policy(struct ipe_policy *p)
{
	int rc = 0;
	char *dup = NULL;
	LIST_HEAD(parsed);
	struct ipe_parsed_policy *pp = NULL;

	if (!p->textlen)
		return -EBADMSG;

	dup = kmemdup_nul(p->text, p->textlen, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	pp = new_parsed_policy();
	if (IS_ERR(pp)) {
		rc = PTR_ERR(pp);
		goto out;
	}

	rc = parse_pass1(dup, &parsed);
	if (rc)
		goto err;

	rc = parse_pass2(&parsed, pp);
	if (rc)
		goto err;

	rc = parse_pass3(&parsed, pp);
	if (rc)
		goto err;

	rc = validate_policy(&parsed, pp);
	if (rc)
		goto err;

	p->parsed = pp;

	goto out;
err:
	free_parsed_policy(pp);
out:
	free_parsed_text(&parsed);
	kfree(dup);

	return rc;
}

/**
 * ipe_is_op_alias: Determine if @op is an alias for one or more operations
 * @op: Supplies the operation to check. Should be either ipe_operation or
 *	ipe_op_alias.
 * @map: Supplies a pointer to populate with the mapping if @op is an alias
 * @size: Supplies the size of @map if @op is an alias.
 *
 * Return:
 * true - @op is an alias
 * false - @op is not an alias
 */
bool ipe_is_op_alias(int op, const enum ipe_operation **map, size_t *size)
{
	switch (op) {
	default:
		return false;
	}
}

/**
 * ipe_free_policy: Deallocate a given IPE policy.
 * @p: Supplies the policy to free.
 *
 * Safe to call on IS_ERR/NULL.
 */
void ipe_put_policy(struct ipe_policy *p)
{
	if (IS_ERR_OR_NULL(p) || !refcount_dec_and_test(&p->refcount))
		return;

	ipe_del_policyfs_node(p);
	securityfs_remove(p->policyfs);
	free_parsed_policy(p->parsed);
	if (!p->pkcs7)
		kfree(p->text);
	kfree(p->pkcs7);
	kfree(p);
}

/**
 * ipe_get_policy_rcu: Dereference rcu-protected @p and increase the reference
 *		       count.
 * @p: rcu-protected pointer to dereference
 *
 * Not safe to call on IS_ERR.
 *
 * Return:
 * !NULL - reference count of @p was valid, and increased by one.
 * NULL - reference count of @p is not valid.
 */
struct ipe_policy *ipe_get_policy_rcu(struct ipe_policy __rcu *p)
{
	struct ipe_policy *rv = NULL;

	rcu_read_lock();

	rv = rcu_dereference(p);
	if (!rv || !refcount_inc_not_zero(&rv->refcount))
		rv = NULL;

	rcu_read_unlock();

	return rv;
}

static int set_pkcs7_data(void *ctx, const void *data, size_t len,
			  size_t asn1hdrlen)
{
	struct ipe_policy *p = ctx;

	p->text = (const char *)data;
	p->textlen = len;

	return 0;
}

/**
 * ipe_update_policy: parse a new policy and replace @old with it.
 * @old: Supplies a pointer to the policy to replace
 * @text: Supplies a pointer to the plain text policy
 * @textlen: Supplies the length of @text
 * @pkcs7: Supplies a pointer to a buffer containing a pkcs7 message.
 * @pkcs7len: Supplies the length of @pkcs7len
 *
 * @text/@textlen is mutually exclusive with @pkcs7/@pkcs7len - see
 * ipe_new_policy.
 *
 * Return:
 * !IS_ERR - OK
 */
struct ipe_policy *ipe_update_policy(struct ipe_policy *old,
				     const char *text, size_t textlen,
				     const char *pkcs7, size_t pkcs7len)
{
	int rc = 0;
	struct ipe_policy *new;

	new = ipe_new_policy(text, textlen, pkcs7, pkcs7len);
	if (IS_ERR(new)) {
		rc = PTR_ERR(new);
		goto err;
	}

	if (strcmp(new->parsed->name, old->parsed->name)) {
		rc = -EINVAL;
		goto err;
	}

	rc = ipe_replace_policy(old, new);
err:
	ipe_put_policy(new);
	return (rc < 0) ? ERR_PTR(rc) : new;
}

/**
 * ipe_new_policy: allocate and parse an ipe_policy structure.
 *
 * @text: Supplies a pointer to the plain-text policy to parse.
 * @textlen: Supplies the length of @text.
 * @pkcs7: Supplies a pointer to a pkcs7-signed IPE policy.
 * @pkcs7len: Supplies the length of @pkcs7.
 *
 * @text/@textlen Should be NULL/0 if @pkcs7/@pkcs7len is set.
 *
 * The result will still need to be associated with a context via
 * ipe_add_policy.
 *
 * Return:
 * !IS_ERR - Success
 */
struct ipe_policy *ipe_new_policy(const char *text, size_t textlen,
				  const char *pkcs7, size_t pkcs7len)
{
	int rc = 0;
	struct ipe_policy *new = NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);

	refcount_set(&new->refcount, 1);
	INIT_LIST_HEAD(&new->next);

	if (!text) {
		new->pkcs7len = pkcs7len;
		new->pkcs7 = kmemdup(pkcs7, pkcs7len, GFP_KERNEL);
		if (!new->pkcs7) {
			rc = -ENOMEM;
			goto err;
		}

		rc = verify_pkcs7_signature(NULL, 0, new->pkcs7, pkcs7len, NULL,
					    VERIFYING_UNSPECIFIED_SIGNATURE,
					    set_pkcs7_data, new);
		if (rc)
			goto err;
	} else {
		new->textlen = textlen;
		new->text = kstrndup(text, textlen, GFP_KERNEL);
		if (!new->text) {
			rc = -ENOMEM;
			goto err;
		}
	}

	rc = parse_policy(new);
	if (rc)
		goto err;

	return new;
err:
	ipe_put_policy(new);
	return ERR_PTR(rc);
}
