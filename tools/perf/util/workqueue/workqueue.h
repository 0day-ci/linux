/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __WORKQUEUE_WORKQUEUE_H
#define __WORKQUEUE_WORKQUEUE_H

#include <stdlib.h>
#include <sys/types.h>
#include <linux/list.h>
#include "threadpool.h"

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
	struct list_head entry;
	work_func_t func;
};

struct workqueue_struct;

extern struct workqueue_struct *create_workqueue(struct threadpool_struct *pool);
extern int destroy_workqueue(struct workqueue_struct *wq);

extern int workqueue_nr_threads(struct workqueue_struct *wq);

extern int queue_work(struct workqueue_struct *wq, struct work_struct *work);

extern int flush_workqueue(struct workqueue_struct *wq);

extern void init_work(struct work_struct *work);

/* parallel_for utility */

typedef void (*parallel_for_func_t)(int i, void *args);

extern int parallel_for(struct workqueue_struct *wq, int from, int to, int work_size,
			parallel_for_func_t func, void *args);

#endif /* __WORKQUEUE_WORKQUEUE_H */
