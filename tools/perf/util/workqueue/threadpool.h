/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __WORKQUEUE_THREADPOOL_H
#define __WORKQUEUE_THREADPOOL_H

struct threadpool_struct;
struct task_struct;

typedef void (*task_func_t)(int tidx, struct task_struct *task);

struct task_struct {
	task_func_t fn;
};

extern struct threadpool_struct *create_threadpool(int n_threads);
extern void destroy_threadpool(struct threadpool_struct *pool);

extern int threadpool_size(struct threadpool_struct *pool);

#endif /* __WORKQUEUE_THREADPOOL_H */
