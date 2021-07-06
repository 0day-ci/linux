/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FREELIST_H
#define FREELIST_H

#include <linux/slab.h>
#include <linux/atomic.h>

/*
 * lockless queue for kretprobe instances
 *
 * It's an array-based MPMC lockless queue, solely for better scalability
 * and contention mitigation. It's simple in implementation and compact in
 * memory efficiency. The only concern is to retrieve kretprobe instance
 * records as fast as possible, with both order and fairness ignored.
 */

struct freelist_node {
	struct freelist_node    *next;
};
struct freelist_head {
	uint32_t                fh_size;	/* rounded to power of 2 */
	uint32_t                fh_mask;	/* (fh_size - 1) */
	uint16_t                fh_bits;	/* log2(fh_size) */
	uint16_t                fh_step;	/* per-core shift stride */
	uint32_t                fh_used;	/* num of elements in list */
	struct freelist_node  **fh_ents;	/* array for krp instances */
};

static inline int freelist_init(struct freelist_head *list, int max)
{
	uint32_t size, cores = roundup_pow_of_two(num_possible_cpus());

	list->fh_used = 0;
	list->fh_step = ilog2(L1_CACHE_BYTES / sizeof(void *));
	if (max < (cores << list->fh_step))
		list->fh_size = cores << list->fh_step;
	else
		list->fh_size = roundup_pow_of_two(max);
	list->fh_mask = list->fh_size - 1;
	list->fh_bits = (uint16_t)ilog2(list->fh_size);
	list->fh_step = list->fh_bits - (uint16_t)ilog2(cores);
	size = list->fh_size * sizeof(struct freelist_node *);
	list->fh_ents = kzalloc(size, GFP_KERNEL);
	if (!list->fh_ents)
		return -ENOMEM;

	return 0;
}

static inline int freelist_try_add(struct freelist_node *node, struct freelist_head *list)
{
	uint32_t i, hint;

	hint = (list->fh_used << list->fh_step) +
	       (list->fh_used >> (list->fh_bits - list->fh_step));
	for (i = 0; i < list->fh_size; i++) {
		struct freelist_node *item = NULL;
		uint32_t slot = (i + hint) & list->fh_mask;
		if (try_cmpxchg_release(&list->fh_ents[slot], &item, node)) {
			list->fh_used++;
			break;
		}
	}

	return (i >= list->fh_size);
}

static inline int freelist_add(struct freelist_node *node, struct freelist_head *list)
{
	uint32_t hint = raw_smp_processor_id() << list->fh_step;
	uint32_t slot = ((uint32_t) hint) & list->fh_mask;

	do {
		struct freelist_node *item = NULL;
		if (try_cmpxchg_release(&list->fh_ents[slot], &item, node))
			return 0;
		slot = (slot + 1) & list->fh_mask;
	} while (1);

	return -1;
}

static inline struct freelist_node *freelist_try_get(struct freelist_head *list)
{
	struct freelist_node *node = NULL;
	uint32_t i, hint = raw_smp_processor_id() << list->fh_step;

	for (i = 0; i < list->fh_size; i++) {
		uint32_t slot = (hint + i) & list->fh_mask;
		struct freelist_node *item = smp_load_acquire(&list->fh_ents[slot]);
		if (item && try_cmpxchg_release(&list->fh_ents[slot], &item, NULL)) {
			node = item;
			break;
		}
	}

	return node;
}

static inline void freelist_destroy(struct freelist_head *list, void *context,
                                    int (*release)(void *, void *))
{
	uint32_t i;

	if (!list->fh_ents)
		return;

	for (i = 0; i < list->fh_size; i++) {
		uint32_t slot = i & list->fh_mask;
		struct freelist_node *item = smp_load_acquire(&list->fh_ents[slot]);
		while (item) {
			if (try_cmpxchg_release(&list->fh_ents[slot], &item, NULL)) {
				if (release)
					release(context, item);
				break;
			}
		}
	}

	if (list->fh_ents) {
		kfree(list->fh_ents);
		list->fh_ents = NULL;
	}
}
#endif /* FREELIST_H */
