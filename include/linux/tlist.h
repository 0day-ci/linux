/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TLIST_H
#define _LINUX_TLIST_H

#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/list.h>

#define TLIST(T, member) \
struct { \
	struct list_head head; \
	char _member_offset[0][offsetof(T, member) + \
			       BUILD_BUG_ON_ZERO(!__same_type(struct list_head, \
							      typeof_member(T, member)))]; \
	T _type[0]; \
}

#define TLIST_DEFINE(T, member, name) \
	TLIST(T, member) name = { LIST_HEAD_INIT((name).head) }

#define tlist_value_type(list) \
	typeof(*(list)->_type)

#define tlist_member_offset(list) \
	sizeof(*(list)->_member_offset)

#define tlist_head(list) \
	(&(list)->head)

#define tlist_init(list) \
	INIT_LIST_HEAD(tlist_head(list))

#define tlist_is_empty(list) \
	list_empty(tlist_head(list))

#define __tlist_ptroff(base, offset, T) \
	((T *) (((uintptr_t)(base)) + (offset)))

#define tlist_item_to_node(list, item) \
	(__tlist_ptroff((item), tlist_member_offset(list), struct list_head) + \
	 BUILD_BUG_ON_ZERO(!__same_type(*(item), tlist_value_type(list))))

#define tlist_node_to_item(list, node) \
	__tlist_ptroff((node), -tlist_member_offset(list), tlist_value_type(list))

#define tlist_begin(list) \
	tlist_node_to_item((list), tlist_head(list)->next)

#define tlist_end(list) \
	tlist_node_to_item((list), tlist_head(list))

#define tlist_remove(list, item) \
	list_del(tlist_item_to_node((list), (item)))

#define tlist_push_back(list, item) \
	list_add_tail(tlist_head(list), tlist_item_to_node((list), (item)))

#define tlist_item_next(list, item) \
	tlist_node_to_item((list), tlist_item_to_node((list), (item))->next)

#define tlist_for_each(list, iter) \
	for (tlist_value_type(list) *iter = tlist_begin(list); \
	     iter != tlist_end(list); \
	     iter = tlist_item_next(list, iter))

#define __tlist_for_each_safe(list, iter, next_iter) \
	for (tlist_value_type(list) *iter = tlist_begin(list), *next_iter; \
	     iter != tlist_end(list) && (next_iter = tlist_item_next(list, iter), 1); \
	     iter = next_iter)

#define tlist_for_each_safe(list, iter) \
	__tlist_for_each_safe((list), iter, __UNIQUE_ID(tlist_next_))

#endif /* _LINUX_TLIST_H */
