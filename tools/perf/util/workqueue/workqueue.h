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

extern struct workqueue_struct *create_workqueue(struct threadpool *pool);
extern int destroy_workqueue(struct workqueue_struct *wq);

extern int workqueue_nr_threads(struct workqueue_struct *wq);

extern int queue_work(struct workqueue_struct *wq, struct work_struct *work);

extern int flush_workqueue(struct workqueue_struct *wq);

extern void init_work(struct work_struct *work);

#define WORKQUEUE_STRERR_BUFSIZE (128+THREADPOOL_STRERR_BUFSIZE)
#define WORKQUEUE_ERROR__OFFSET 512
enum {
	WORKQUEUE_ERROR__NOTALLOWED = WORKQUEUE_ERROR__OFFSET,
	WORKQUEUE_ERROR__POOLEXE,
	WORKQUEUE_ERROR__POOLWAIT,
	WORKQUEUE_ERROR__WRITEPIPE,
	WORKQUEUE_ERROR__READPIPE,
	WORKQUEUE_ERROR__INVALIDMSG,
};
extern int workqueue_strerror(struct workqueue_struct *wq, int err, char *buf, size_t size);
extern int create_workqueue_strerror(struct workqueue_struct *err_ptr, char *buf, size_t size);
extern int destroy_workqueue_strerror(int err, char *buf, size_t size);
#endif /* __WORKQUEUE_WORKQUEUE_H */
