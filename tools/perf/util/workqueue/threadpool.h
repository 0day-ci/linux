/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __WORKQUEUE_THREADPOOL_H
#define __WORKQUEUE_THREADPOOL_H

struct threadpool;
struct task_struct;

typedef void (*task_func_t)(int tidx, struct task_struct *task);

struct task_struct {
	task_func_t fn;
};

extern struct threadpool *threadpool__new(int n_threads);
extern void threadpool__delete(struct threadpool *pool);

extern int threadpool__start(struct threadpool *pool);
extern int threadpool__stop(struct threadpool *pool);

extern int threadpool__execute(struct threadpool *pool, struct task_struct *task);
extern int threadpool__wait(struct threadpool *pool);

extern int threadpool__size(struct threadpool *pool);

/* Error management */
#define THREADPOOL_STRERR_BUFSIZE (128+STRERR_BUFSIZE)
extern int threadpool__strerror(struct threadpool *pool, int err, char *buf, size_t size);
extern int threadpool__new_strerror(struct threadpool *err_ptr, char *buf, size_t size);

#endif /* __WORKQUEUE_THREADPOOL_H */
