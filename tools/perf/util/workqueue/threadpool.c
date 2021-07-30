// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "debug.h"
#include <asm/bug.h>
#include <linux/zalloc.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include "threadpool.h"

enum threadpool_status {
	THREADPOOL_STATUS__STOPPED,		/* no threads */
	THREADPOOL_STATUS__ERROR,		/* errors */
	THREADPOOL_STATUS__MAX
};

struct threadpool {
	int			nr_threads;	/* number of threads in the pool */
	struct threadpool_entry	*threads;	/* array of threads in the pool */
	struct task_struct	*current_task;	/* current executing function */
	enum threadpool_status	status;		/* current status of the pool */
};

struct threadpool_entry {
	int				idx;	/* idx of thread in pool->threads */
	pid_t				tid;	/* tid of thread */
	struct threadpool		*pool;	/* parent threadpool */
	struct {
		int ack[2];			/* messages from thread (acks) */
		int cmd[2];			/* messages to thread (commands) */
	} pipes;
};

/**
 * threadpool_entry__init_pipes - initialize all pipes of @thread
 */
static void threadpool_entry__init_pipes(struct threadpool_entry *thread)
{
	thread->pipes.ack[0] = -1;
	thread->pipes.ack[1] = -1;
	thread->pipes.cmd[0] = -1;
	thread->pipes.cmd[1] = -1;
}

/**
 * threadpool_entry__open_pipes - open all pipes of @thread
 */
static int threadpool_entry__open_pipes(struct threadpool_entry *thread)
{
	if (pipe(thread->pipes.ack)) {
		pr_debug2("threadpool: failed to create comm pipe 'from': %s\n",
			strerror(errno));
		return -ENOMEM;
	}

	if (pipe(thread->pipes.cmd)) {
		pr_debug2("threadpool: failed to create comm pipe 'to': %s\n",
			strerror(errno));
		close(thread->pipes.ack[0]);
		thread->pipes.ack[0] = -1;
		close(thread->pipes.ack[1]);
		thread->pipes.ack[1] = -1;
		return -ENOMEM;
	}

	return 0;
}

/**
 * threadpool_entry__close_pipes - close all communication pipes of @thread
 */
static void threadpool_entry__close_pipes(struct threadpool_entry *thread)
{
	if (thread->pipes.ack[0] != -1) {
		close(thread->pipes.ack[0]);
		thread->pipes.ack[0] = -1;
	}
	if (thread->pipes.ack[1] != -1) {
		close(thread->pipes.ack[1]);
		thread->pipes.ack[1] = -1;
	}
	if (thread->pipes.cmd[0] != -1) {
		close(thread->pipes.cmd[0]);
		thread->pipes.cmd[0] = -1;
	}
	if (thread->pipes.cmd[1] != -1) {
		close(thread->pipes.cmd[1]);
		thread->pipes.cmd[1] = -1;
	}
}

/**
 * threadpool__new - create a fixed threadpool with @n_threads threads
 */
struct threadpool *threadpool__new(int n_threads)
{
	int ret, err, t;
	struct threadpool *pool = malloc(sizeof(*pool));

	if (!pool) {
		pr_debug2("threadpool: cannot allocate pool: %s\n",
			strerror(errno));
		err = -ENOMEM;
		goto out_return;
	}

	if (n_threads <= 0) {
		pr_debug2("threadpool: invalid number of threads: %d\n",
			n_threads);
		err = -EINVAL;
		goto out_free_pool;
	}

	pool->nr_threads = n_threads;
	pool->current_task = NULL;

	pool->threads = calloc(n_threads, sizeof(*pool->threads));
	if (!pool->threads) {
		pr_debug2("threadpool: cannot allocate threads: %s\n",
			strerror(errno));
		err = -ENOMEM;
		goto out_free_pool;
	}

	for (t = 0; t < n_threads; t++) {
		pool->threads[t].idx = t;
		pool->threads[t].tid = -1;
		pool->threads[t].pool = pool;
		threadpool_entry__init_pipes(&pool->threads[t]);
	}

	for (t = 0; t < n_threads; t++) {
		ret = threadpool_entry__open_pipes(&pool->threads[t]);
		if (ret) {
			err = -ret;
			goto out_close_pipes;
		}
	}

	pool->status = THREADPOOL_STATUS__STOPPED;

	return pool;

out_close_pipes:
	for (t = 0; t < n_threads; t++)
		threadpool_entry__close_pipes(&pool->threads[t]);

	zfree(&pool->threads);
out_free_pool:
	free(pool);
out_return:
	return ERR_PTR(err);
}

/**
 * threadpool__strerror - print message regarding given @err in @pool
 *
 * Buffer size should be at least THREADPOOL_STRERR_BUFSIZE bytes.
 */
int threadpool__strerror(struct threadpool *pool __maybe_unused, int err, char *buf, size_t size)
{
	char sbuf[STRERR_BUFSIZE], *emsg;

	emsg = str_error_r(err, sbuf, sizeof(sbuf));
	return scnprintf(buf, size, "Error: %s.\n", emsg);
}

/**
 * threadpool__new_strerror - print message regarding @err_ptr
 *
 * Buffer size should be at least THREADPOOL_STRERR_BUFSIZE bytes.
 */
int threadpool__new_strerror(struct threadpool *err_ptr, char *buf, size_t size)
{
	return threadpool__strerror(err_ptr, PTR_ERR(err_ptr), buf, size);
}

/**
 * threadpool__delete - free the @pool and all its resources
 */
void threadpool__delete(struct threadpool *pool)
{
	int t;

	if (IS_ERR_OR_NULL(pool))
		return;

	WARN_ON(pool->status != THREADPOOL_STATUS__STOPPED
		&& pool->status != THREADPOOL_STATUS__ERROR);

	for (t = 0; t < pool->nr_threads; t++)
		threadpool_entry__close_pipes(&pool->threads[t]);

	zfree(&pool->threads);
	free(pool);
}

/**
 * threadpool__size - get number of threads in the threadpool
 */
int threadpool__size(struct threadpool *pool)
{
	return pool->nr_threads;
}
