/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <statement.h>
#include <rule.h>
#include <utils.h>


void handle_free(struct handle *h)
{
	xfree(h->table);
	xfree(h->chain);
	xfree(h->set);
}

void handle_merge(struct handle *dst, const struct handle *src)
{
	if (dst->family == 0)
		dst->family = src->family;
	if (dst->table == NULL && src->table != NULL)
		dst->table = xstrdup(src->table);
	if (dst->chain == NULL && src->chain != NULL)
		dst->chain = xstrdup(src->chain);
	if (dst->set == NULL && src->set != NULL)
		dst->set = xstrdup(src->set);
	if (dst->handle == 0)
		dst->handle = src->handle;
}

struct set *set_alloc(const struct location *loc)
{
	struct set *set;

	set = xzalloc(sizeof(*set));
	set->refcnt = 1;
	if (loc != NULL)
		set->location = *loc;
	return set;
}

struct set *set_get(struct set *set)
{
	set->refcnt++;
	return set;
}

void set_free(struct set *set)
{
	if (--set->refcnt > 0)
		return;
	handle_free(&set->handle);
	xfree(set);
}

void set_add_hash(struct set *set, struct table *table)
{
	list_add_tail(&set->list, &table->sets);
}

struct set *set_lookup(const struct table *table, const char *name)
{
	struct set *set;

	list_for_each_entry(set, &table->sets, list) {
		if (!strcmp(set->handle.set, name))
			return set;
	}
	return NULL;
}

void set_print(const struct set *set)
{
	const char *type;

	type = set->flags & SET_F_MAP ? "map" : "set";
	printf("\t%s %s {\n", type, set->handle.set);

	printf("\t\ttype %s", set->keytype->name);
	if (set->flags & SET_F_MAP)
		printf(" => %s", set->datatype->name);
	printf("\n");

	if (set->flags & SET_F_ANONYMOUS)
		printf("\t\tanonymous\n");
	if (set->flags & SET_F_CONSTANT)
		printf("\t\tconstant\n");
	if (set->flags & SET_F_INTERVAL)
		printf("\t\tinterval\n");

	if (set->init != NULL && set->init->size > 0) {
		printf("\t\telements = ");
		expr_print(set->init);
		printf("\n");
	}
	printf("\t}\n");
}

struct rule *rule_alloc(const struct location *loc, const struct handle *h)
{
	struct rule *rule;

	rule = xzalloc(sizeof(*rule));
	rule->location = *loc;
	init_list_head(&rule->list);
	init_list_head(&rule->stmts);
	if (h != NULL)
		rule->handle = *h;
	return rule;
}

void rule_free(struct rule *rule)
{
	stmt_list_free(&rule->stmts);
	handle_free(&rule->handle);
	xfree(rule);
}

void rule_print(const struct rule *rule)
{
	const struct stmt *stmt;

	list_for_each_entry(stmt, &rule->stmts, list) {
		printf(" ");
		stmt->ops->print(stmt);
	}
	printf("\n");
}

struct scope *scope_init(struct scope *scope, const struct scope *parent)
{
	scope->parent = parent;
	init_list_head(&scope->symbols);
	return scope;
}

void scope_release(const struct scope *scope)
{
	struct symbol *sym, *next;

	list_for_each_entry_safe(sym, next, &scope->symbols, list) {
		list_del(&sym->list);
		xfree(sym->identifier);
		expr_free(sym->expr);
		xfree(sym);
	}
}

void symbol_bind(struct scope *scope, const char *identifier, struct expr *expr)
{
	struct symbol *sym;

	sym = xzalloc(sizeof(*sym));
	sym->identifier = xstrdup(identifier);
	sym->expr = expr;

	list_add_tail(&sym->list, &scope->symbols);
}

struct symbol *symbol_lookup(const struct scope *scope, const char *identifier)
{
	struct symbol *sym;

	while (scope != NULL) {
		list_for_each_entry(sym, &scope->symbols, list) {
			if (!strcmp(sym->identifier, identifier))
				return sym;
		}
		scope = scope->parent;
	}
	return NULL;
}

struct chain *chain_alloc(const char *name)
{
	struct chain *chain;

	chain = xzalloc(sizeof(*chain));
	init_list_head(&chain->rules);
	if (name != NULL)
		chain->handle.chain = xstrdup(name);
	return chain;
}

void chain_free(struct chain *chain)
{
	struct rule *rule, *next;

	list_for_each_entry_safe(rule, next, &chain->rules, list)
		rule_free(rule);
	handle_free(&chain->handle);
	scope_release(&chain->scope);
	xfree(chain);
}

void chain_add_hash(struct chain *chain, struct table *table)
{
	list_add_tail(&chain->list, &table->chains);
}

struct chain *chain_lookup(const struct table *table, const struct handle *h)
{
	struct chain *chain;

	list_for_each_entry(chain, &table->chains, list) {
		if (!strcmp(chain->handle.chain, h->chain))
			return chain;
	}
	return NULL;
}

static void chain_print(const struct chain *chain)
{
	struct rule *rule;

	printf("\tchain %s {\n", chain->handle.chain);
	list_for_each_entry(rule, &chain->rules, list) {
		printf("\t\t");
		rule_print(rule);
	}
	printf("\t}\n");
}

struct table *table_alloc(void)
{
	struct table *table;

	table = xzalloc(sizeof(*table));
	init_list_head(&table->chains);
	init_list_head(&table->sets);
	return table;
}

void table_free(struct table *table)
{
	struct chain *chain, *next;

	list_for_each_entry_safe(chain, next, &table->chains, list)
		chain_free(chain);
	handle_free(&table->handle);
	scope_release(&table->scope);
	xfree(table);
}

static LIST_HEAD(table_list);

void table_add_hash(struct table *table)
{
	list_add_tail(&table->list, &table_list);
}

struct table *table_lookup(const struct handle *h)
{
	struct table *table;

	list_for_each_entry(table, &table_list, list) {
		if (table->handle.family == h->family &&
		    !strcmp(table->handle.table, h->table))
			return table;
	}
	return NULL;
}

static void table_print(const struct table *table)
{
	struct chain *chain;
	struct set *set;
	const char *delim = "";

	printf("table %s {\n", table->handle.table);
	list_for_each_entry(set, &table->sets, list) {
		if (set->flags & SET_F_ANONYMOUS)
			continue;
		printf("%s", delim);
		set_print(set);
		delim = "\n";
	}
	list_for_each_entry(chain, &table->chains, list) {
		printf("%s", delim);
		chain_print(chain);
		delim = "\n";
	}
	printf("}\n");
}

struct cmd *cmd_alloc(enum cmd_ops op, enum cmd_obj obj,
		      const struct handle *h, void *data)
{
	struct cmd *cmd;

	cmd = xzalloc(sizeof(*cmd));
	cmd->op     = op;
	cmd->obj    = obj;
	cmd->handle = *h;
	cmd->data   = data;
	return cmd;
}

void cmd_free(struct cmd *cmd)
{
	handle_free(&cmd->handle);
	if (cmd->data != NULL) {
		switch (cmd->obj) {
		case CMD_OBJ_SETELEM:
			expr_free(cmd->expr);
			break;
		case CMD_OBJ_SET:
			set_free(cmd->set);
			break;
		case CMD_OBJ_RULE:
			rule_free(cmd->rule);
			break;
		case CMD_OBJ_CHAIN:
			chain_free(cmd->chain);
			break;
		case CMD_OBJ_TABLE:
			table_free(cmd->table);
			break;
		default:
			BUG();
		}
	}
	xfree(cmd);
}

#include <netlink.h>

static int do_add_chain(struct netlink_ctx *ctx, const struct handle *h,
			struct chain *chain)
{
	struct rule *rule;

	if (netlink_add_chain(ctx, h, chain) < 0)
		return -1;
	if (chain != NULL) {
		list_for_each_entry(rule, &chain->rules, list) {
			if (netlink_add_rule(ctx, &rule->handle, rule) < 0)
				return -1;
		}
	}
	return 0;
}

static int do_add_setelems(struct netlink_ctx *ctx, const struct handle *h,
			   const struct expr *expr)
{
	if (netlink_add_setelems(ctx, h, expr) < 0)
		return -1;
	return 0;
}

static int do_add_set(struct netlink_ctx *ctx, const struct handle *h,
		      struct set *set)
{
	if (netlink_add_set(ctx, h, set) < 0)
		return -1;
	if (set->init != NULL) {
		if (set->flags & SET_F_INTERVAL)
			set_to_intervals(set->init);
		if (do_add_setelems(ctx, &set->handle, set->init) < 0)
			return -1;
	}
	return 0;
}

static int do_add_table(struct netlink_ctx *ctx, const struct handle *h,
			struct table *table)
{
	struct chain *chain;
	struct set *set;

	if (netlink_add_table(ctx, h, table) < 0)
		return -1;
	if (table != NULL) {
		list_for_each_entry(set, &table->sets, list) {
			handle_merge(&set->handle, &table->handle);
			if (do_add_set(ctx, &set->handle, set) < 0)
				return -1;
		}
		list_for_each_entry(chain, &table->chains, list) {
			if (do_add_chain(ctx, &chain->handle, chain) < 0)
				return -1;
		}
	}
	return 0;
}

static int do_command_add(struct netlink_ctx *ctx, struct cmd *cmd)
{
	switch (cmd->obj) {
	case CMD_OBJ_TABLE:
		return do_add_table(ctx, &cmd->handle, cmd->table);
	case CMD_OBJ_CHAIN:
		return do_add_chain(ctx, &cmd->handle, cmd->chain);
	case CMD_OBJ_RULE:
		return netlink_add_rule(ctx, &cmd->handle, cmd->rule);
	case CMD_OBJ_SET:
		return do_add_set(ctx, &cmd->handle, cmd->set);
	case CMD_OBJ_SETELEM:
		return do_add_setelems(ctx, &cmd->handle, cmd->expr);
	default:
		BUG();
	}
	return 0;
}

static int do_command_delete(struct netlink_ctx *ctx, struct cmd *cmd)
{
	switch (cmd->obj) {
	case CMD_OBJ_TABLE:
		return netlink_delete_table(ctx, &cmd->handle);
	case CMD_OBJ_CHAIN:
		return netlink_delete_chain(ctx, &cmd->handle);
	case CMD_OBJ_RULE:
		return netlink_delete_rule(ctx, &cmd->handle);
	case CMD_OBJ_SET:
		return netlink_delete_set(ctx, &cmd->handle);
	case CMD_OBJ_SETELEM:
		return netlink_delete_setelems(ctx, &cmd->handle, cmd->expr);
	default:
		BUG();
	}
}

static int do_list_sets(struct netlink_ctx *ctx, struct table *table)
{
	struct set *set, *nset;

	if (netlink_list_sets(ctx, &table->handle) < 0)
		return -1;

	list_for_each_entry_safe(set, nset, &ctx->list, list) {
		if (set->flags & SET_F_ANONYMOUS &&
		    netlink_get_setelems(ctx, &set->handle, set) < 0)
			return -1;
		list_move_tail(&set->list, &table->sets);
	}
	return 0;
}

static int do_command_list(struct netlink_ctx *ctx, struct cmd *cmd)
{
	struct table *table;
	struct chain *chain;
	struct rule *rule, *nrule;
	struct set *set, *nset;

	table = table_alloc();
	handle_merge(&table->handle, &cmd->handle);
	table_add_hash(table);

	switch (cmd->obj) {
	case CMD_OBJ_TABLE:
		if (do_list_sets(ctx, table) < 0)
			return -1;
		if (netlink_list_table(ctx, &cmd->handle) < 0)
			return -1;
		break;
	case CMD_OBJ_CHAIN:
		if (do_list_sets(ctx, table) < 0)
			return -1;
		if (netlink_list_chain(ctx, &cmd->handle) < 0)
			return -1;
		break;
	case CMD_OBJ_SETS:
		if (netlink_list_sets(ctx, &cmd->handle) < 0)
			return -1;
		list_for_each_entry_safe(set, nset, &ctx->list, list)
			list_move_tail(&set->list, &table->sets);
		break;
	case CMD_OBJ_SET:
		if (netlink_get_set(ctx, &cmd->handle) < 0)
			return -1;
		list_for_each_entry(set, &ctx->list, list) {
			if (netlink_get_setelems(ctx, &cmd->handle, set) < 0)
				return -1;
			set_print(set);
		}
		return 0;
	default:
		BUG();
	}

	list_for_each_entry_safe(rule, nrule, &ctx->list, list) {
		table = table_lookup(&rule->handle);
		chain = chain_lookup(table, &rule->handle);
		if (chain == NULL) {
			chain = chain_alloc(rule->handle.chain);
			chain_add_hash(chain, table);
		}

		list_move_tail(&rule->list, &chain->rules);
	}

	table_print(table);
	return 0;
}

static int do_command_flush(struct netlink_ctx *ctx, struct cmd *cmd)
{
	switch (cmd->obj) {
	case CMD_OBJ_TABLE:
		return netlink_flush_table(ctx, &cmd->handle);
	case CMD_OBJ_CHAIN:
		return netlink_flush_chain(ctx, &cmd->handle);
	default:
		BUG();
	}
	return 0;
}

int do_command(struct netlink_ctx *ctx, struct cmd *cmd)
{
	switch (cmd->op) {
	case CMD_ADD:
		return do_command_add(ctx, cmd);
	case CMD_DELETE:
		return do_command_delete(ctx, cmd);
	case CMD_LIST:
		return do_command_list(ctx, cmd);
	case CMD_FLUSH:
		return do_command_flush(ctx, cmd);
	default:
		BUG();
	}
}

static int payload_match_stmt_cmp(const void *p1, const void *p2)
{
	const struct stmt *s1 = *(struct stmt * const *)p1;
	const struct stmt *s2 = *(struct stmt * const *)p2;
	const struct expr *e1 = s1->expr, *e2 = s2->expr;
	int d;

	d = e1->left->payload.base - e2->left->payload.base;
	if (d != 0)
		return d;
	return e1->left->payload.offset - e2->left->payload.offset;
}

static void payload_do_merge(struct stmt *sa[], unsigned int n)
{
	struct expr *last, *this, *expr;
	struct stmt *stmt;
	unsigned int i;

	qsort(sa, n, sizeof(sa[0]), payload_match_stmt_cmp);

	last = sa[0]->expr;
	for (i = 1; i < n; i++) {
		stmt = sa[i];
		this = stmt->expr;

		if (!payload_is_adjacent(last->left, this->left) ||
		    last->op != this->op) {
			last = this;
			continue;
		}

		expr = payload_expr_join(last->left, this->left);
		expr_free(last->left);
		last->left = expr;

		expr = constant_expr_join(last->right, this->right);
		expr_free(last->right);
		last->right = expr;

		list_del(&stmt->list);
		stmt_free(stmt);
	}
}

/**
 * payload_try_merge - try to merge consecutive payload match statements
 *
 * @rule:	nftables rule
 *
 * Locate sequences of payload match statements referring to adjacent
 * header locations and merge those using only equality relations.
 *
 * As a side-effect, payload match statements are ordered in ascending
 * order according to the location of the payload.
 */
static void payload_try_merge(const struct rule *rule)
{
	struct stmt *sa[rule->num_stmts];
	struct stmt *stmt, *next;
	unsigned int idx = 0;

	list_for_each_entry_safe(stmt, next, &rule->stmts, list) {
		/* Must not merge across other statements */
		if (stmt->ops->type != STMT_EXPRESSION)
			goto do_merge;

		if (stmt->expr->ops->type != EXPR_RELATIONAL)
			continue;
		if (stmt->expr->left->ops->type != EXPR_PAYLOAD)
			continue;
		if (stmt->expr->right->ops->type != EXPR_VALUE)
			continue;
		switch (stmt->expr->op) {
		case OP_EQ:
		case OP_NEQ:
			break;
		default:
			continue;
		}

		sa[idx++] = stmt;
		continue;
do_merge:
		if (idx < 2)
			continue;
		payload_do_merge(sa, idx);
		idx = 0;
	}

	if (idx > 1)
		payload_do_merge(sa, idx);
}

struct error_record *rule_postprocess(struct rule *rule)
{
	payload_try_merge(rule);
	rule_print(rule);
	return NULL;
}