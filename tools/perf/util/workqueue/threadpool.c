// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "debug.h"
#include "asm/bug.h"
#include "threadpool.h"

enum threadpool_status {
	THREADPOOL_STATUS__STOPPED,		/* no threads */
	THREADPOOL_STATUS__ERROR,		/* errors */
	THREADPOOL_STATUS__MAX
};

struct threadpool_struct {
	int			nr_threads;	/* number of threads in the pool */
	struct thread_struct	*threads;	/* array of threads in the pool */
	struct task_struct	*current_task;	/* current executing function */
	enum threadpool_status	status;		/* current status of the pool */
};

struct thread_struct {
	int				idx;	/* idx of thread in pool->threads */
	pid_t				tid;	/* tid of thread */
	struct threadpool_struct	*pool;	/* parent threadpool */
	struct {
		int from[2];			/* messages from thread (acks) */
		int to[2];			/* messages to thread (commands) */
	} pipes;
};

/**
 * init_pipes - initialize all pipes of @thread
 */
static void init_pipes(struct thread_struct *thread)
{
	thread->pipes.from[0] = -1;
	thread->pipes.from[1] = -1;
	thread->pipes.to[0] = -1;
	thread->pipes.to[1] = -1;
}

/**
 * open_pipes - open all pipes of @thread
 */
static int open_pipes(struct thread_struct *thread)
{
	if (pipe(thread->pipes.from)) {
		pr_err("threadpool: failed to create comm pipe 'from': %s\n",
			strerror(errno));
		return -ENOMEM;
	}

	if (pipe(thread->pipes.to)) {
		pr_err("threadpool: failed to create comm pipe 'to': %s\n",
			strerror(errno));
		close(thread->pipes.from[0]);
		thread->pipes.from[0] = -1;
		close(thread->pipes.from[1]);
		thread->pipes.from[1] = -1;
		return -ENOMEM;
	}

	return 0;
}

/**
 * close_pipes - close all communication pipes of @thread
 */
static void close_pipes(struct thread_struct *thread)
{
	if (thread->pipes.from[0] != -1) {
		close(thread->pipes.from[0]);
		thread->pipes.from[0] = -1;
	}
	if (thread->pipes.from[1] != -1) {
		close(thread->pipes.from[1]);
		thread->pipes.from[1] = -1;
	}
	if (thread->pipes.to[0] != -1) {
		close(thread->pipes.to[0]);
		thread->pipes.to[0] = -1;
	}
	if (thread->pipes.to[1] != -1) {
		close(thread->pipes.to[1]);
		thread->pipes.to[1] = -1;
	}
}

/**
 * create_threadpool - create a fixed threadpool with @n_threads threads
 */
struct threadpool_struct *create_threadpool(int n_threads)
{
	int ret, t;
	struct threadpool_struct *pool = malloc(sizeof(*pool));

	if (!pool) {
		pr_err("threadpool: cannot allocate pool: %s\n",
			strerror(errno));
		return NULL;
	}

	if (n_threads <= 0) {
		pr_err("threadpool: invalid number of threads: %d\n",
			n_threads);
		goto out_free_pool;
	}

	pool->nr_threads = n_threads;
	pool->current_task = NULL;

	pool->threads = malloc(n_threads * sizeof(*pool->threads));
	if (!pool->threads) {
		pr_err("threadpool: cannot allocate threads: %s\n",
			strerror(errno));
		goto out_free_pool;
	}

	for (t = 0; t < n_threads; t++) {
		pool->threads[t].idx = t;
		pool->threads[t].tid = -1;
		pool->threads[t].pool = pool;
		init_pipes(&pool->threads[t]);
	}

	for (t = 0; t < n_threads; t++) {
		ret = open_pipes(&pool->threads[t]);
		if (ret)
			goto out_close_pipes;
	}

	pool->status = THREADPOOL_STATUS__STOPPED;

	return pool;

out_close_pipes:
	for (t = 0; t < n_threads; t++)
		close_pipes(&pool->threads[t]);

	free(pool->threads);
out_free_pool:
	free(pool);
	return NULL;
}

/**
 * destroy_threadpool - free the @pool and all its resources
 */
void destroy_threadpool(struct threadpool_struct *pool)
{
	int t;

	if (!pool)
		return;

	WARN_ON(pool->status != THREADPOOL_STATUS__STOPPED
		&& pool->status != THREADPOOL_STATUS__ERROR);

	for (t = 0; t < pool->nr_threads; t++)
		close_pipes(&pool->threads[t]);

	free(pool->threads);
	free(pool);
}

/**
 * threadpool_size - get number of threads in the threadpool
 */
int threadpool_size(struct threadpool_struct *pool)
{
	return pool->nr_threads;
}
