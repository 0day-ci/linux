// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/leds.h>
#include <linux/list.h>
#include <linux/mutex.h>


/*
 *
 *	The list of block device LED triggers
 *
 */

struct blk_ledtrig {
	struct led_trigger	trigger;
	struct list_head	list_node;
	struct mutex		refcount_mutex;
	int			refcount;
	char			name[];
};

LIST_HEAD(blk_ledtrig_list);
DEFINE_MUTEX(blk_ledtrig_list_mutex);

static inline
struct blk_ledtrig *blk_ledtrig_from_node(struct list_head *const node)
{
	return container_of(node, struct blk_ledtrig, list_node);
}

// Caller must hold blk_ledtrig_list_mutex
static struct blk_ledtrig *blk_ledtrig_find(const char *const name,
					    const size_t len)
{
	struct blk_ledtrig *t;
	struct list_head *n;

	list_for_each(n, &blk_ledtrig_list) {
		t = blk_ledtrig_from_node(n);
		if (strlen(t->name) == len && memcmp(name, t->name, len) == 0)
			return t;
	}

	return NULL;
}
