// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <syscall.h>
#include "debug.h"
#include "asm/bug.h"
#include "threadpool.h"

#ifndef HAVE_GETTID
static inline pid_t gettid(void)
{
	return (pid_t)syscall(__NR_gettid);
}
#endif

enum threadpool_status {
	THREADPOOL_STATUS__STOPPED,		/* no threads */
	THREADPOOL_STATUS__READY,		/* threads are ready but idle */
	THREADPOOL_STATUS__BUSY,		/* threads are busy */
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

enum thread_msg {
	THREAD_MSG__UNDEFINED = 0,
	THREAD_MSG__ACK,		/* from th: create and exit ack */
	THREAD_MSG__WAKE,		/* to th: wake up */
	THREAD_MSG__STOP,		/* to th: exit */
	THREAD_MSG__MAX
};

static const char * const thread_msg_tags[] = {
	"undefined",
	"ack",
	"wake",
	"stop"
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
 * wait_thread - receive ack from thread
 *
 * NB: call only from main thread!
 */
static int wait_thread(struct thread_struct *thread)
{
	int res;
	enum thread_msg msg = THREAD_MSG__UNDEFINED;

	res = read(thread->pipes.from[0], &msg, sizeof(msg));
	if (res < 0) {
		pr_err("threadpool: failed to recv msg from tid=%d: %s\n",
		       thread->tid, strerror(errno));
		return -1;
	}
	if (msg != THREAD_MSG__ACK) {
		pr_err("threadpool: received unexpected msg from tid=%d: %s\n",
		       thread->tid, thread_msg_tags[msg]);
		return -1;
	}

	pr_debug2("threadpool: received ack from tid=%d\n", thread->tid);

	return 0;
}

/**
 * terminate_thread - send stop signal to thread and wait for ack
 *
 * NB: call only from main thread!
 */
static int terminate_thread(struct thread_struct *thread)
{
	int res;
	enum thread_msg msg = THREAD_MSG__STOP;

	res = write(thread->pipes.to[1], &msg, sizeof(msg));
	if (res < 0) {
		pr_err("threadpool: error sending stop msg to tid=%d: %s\n",
			thread->tid, strerror(errno));
		return res;
	}

	res = wait_thread(thread);

	return res;
}

/**
 * wake_thread - send wake msg to @thread
 *
 * This function does not wait for the thread to actually wake
 * NB: call only from main thread!
 */
static int wake_thread(struct thread_struct *thread)
{
	int res;
	enum thread_msg msg = THREAD_MSG__WAKE;

	res = write(thread->pipes.to[1], &msg, sizeof(msg));
	if (res < 0) {
		pr_err("threadpool: error sending wake msg: %s\n", strerror(errno));
		return -1;
	}

	pr_debug2("threadpool: sent wake msg %s to tid=%d\n",
		thread_msg_tags[msg], thread->tid);
	return 0;
}

/**
 * threadpool_thread - function running on thread
 *
 * This function waits for a signal from main thread to start executing
 * a task.
 * On completion, it will go back to sleep, waiting for another signal.
 * Signals are delivered through pipes.
 */
static void *threadpool_thread(void *args)
{
	struct thread_struct *thread = (struct thread_struct *) args;
	enum thread_msg msg;
	int err;

	thread->tid = gettid();

	pr_debug2("threadpool[%d]: started\n", thread->tid);

	for (;;) {
		msg = THREAD_MSG__ACK;
		err = write(thread->pipes.from[1], &msg, sizeof(msg));
		if (err == -1) {
			pr_err("threadpool[%d]: failed to send ack: %s\n",
				thread->tid, strerror(errno));
			break;
		}

		msg = THREAD_MSG__UNDEFINED;
		err = read(thread->pipes.to[0], &msg, sizeof(msg));
		if (err < 0) {
			pr_err("threadpool[%d]: error receiving msg: %s\n",
				thread->tid, strerror(errno));
			break;
		}

		if (msg != THREAD_MSG__WAKE && msg != THREAD_MSG__STOP) {
			pr_err("threadpool[%d]: received unexpected msg: %s\n",
				thread->tid, thread_msg_tags[msg]);
			break;
		}

		if (msg == THREAD_MSG__STOP)
			break;

		if (!thread->pool->current_task) {
			pr_err("threadpool[%d]: received wake without task\n",
				thread->tid);
			break;
		}

		pr_debug("threadpool[%d]: executing task\n", thread->tid);
		thread->pool->current_task->fn(thread->idx, thread->pool->current_task);
	}

	pr_debug2("threadpool[%d]: exit\n", thread->tid);

	msg = THREAD_MSG__ACK;
	err = write(thread->pipes.from[1], &msg, sizeof(msg));
	if (err == -1) {
		pr_err("threadpool[%d]: failed to send ack: %s\n",
			thread->tid, strerror(errno));
		return NULL;
	}

	return NULL;
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

/**
 * __start_threadpool - start all threads in the pool.
 *
 * This function does not change @pool->status.
 */
static int __start_threadpool(struct threadpool_struct *pool)
{
	int t, tt, ret = 0, nr_threads = pool->nr_threads;
	sigset_t full, mask;
	pthread_t handle;
	pthread_attr_t attrs;

	sigfillset(&full);
	if (sigprocmask(SIG_SETMASK, &full, &mask)) {
		pr_err("Failed to block signals on threads start: %s\n",
			strerror(errno));
		return -1;
	}

	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);

	for (t = 0; t < nr_threads; t++) {
		struct thread_struct *thread = &pool->threads[t];

		if (pthread_create(&handle, &attrs, threadpool_thread, thread)) {
			for (tt = 1; tt < t; tt++)
				terminate_thread(thread);
			pr_err("Failed to start threads: %s\n", strerror(errno));
			ret = -1;
			goto out_free_attr;
		}

		if (wait_thread(thread)) {
			for (tt = 1; tt <= t; tt++)
				terminate_thread(thread);
			ret = -1;
			goto out_free_attr;
		}
	}

out_free_attr:
	pthread_attr_destroy(&attrs);

	if (sigprocmask(SIG_SETMASK, &mask, NULL)) {
		pr_err("Failed to unblock signals on threads start: %s\n",
			strerror(errno));
		ret = -1;
	}

	return ret;
}

/**
 * start_threadpool - start all threads in the pool.
 *
 * The function blocks until all threads are up and running.
 */
int start_threadpool(struct threadpool_struct *pool)
{
	int err;

	if (pool->status != THREADPOOL_STATUS__STOPPED) {
		pr_err("threadpool: starting not stopped pool\n");
		return -1;
	}

	err = __start_threadpool(pool);
	pool->status = err ? THREADPOOL_STATUS__ERROR : THREADPOOL_STATUS__READY;
	return err;
}

/**
 * stop_threadpool - stop all threads in the pool.
 *
 * This function blocks waiting for ack from all threads.
 * If the pool was busy, it will first wait for the task to finish.
 */
int stop_threadpool(struct threadpool_struct *pool)
{
	int t, ret, err = 0;

	err = wait_threadpool(pool);
	if (err)
		return err;

	if (pool->status != THREADPOOL_STATUS__READY) {
		pr_err("threadpool: stopping not ready pool\n");
		return -1;
	}

	for (t = 0; t < pool->nr_threads; t++) {
		ret = terminate_thread(&pool->threads[t]);
		if (ret && !err)
			err = -1;
	}

	pool->status = err ? THREADPOOL_STATUS__ERROR : THREADPOOL_STATUS__STOPPED;

	return err;
}

/**
 * threadpool_is_ready - check if the threads are running
 */
bool threadpool_is_ready(struct threadpool_struct *pool)
{
	return pool->status == THREADPOOL_STATUS__READY;
}

/**
 * execute_in_threadpool - execute @task on all threads of the @pool
 *
 * The task will run asynchronously wrt the main thread.
 * The task can be waited with wait_threadpool.
 *
 * NB: make sure the pool is ready before calling this, since no queueing is
 *     performed. If you need queueing, have a look at the workqueue.
 */
int execute_in_threadpool(struct threadpool_struct *pool, struct task_struct *task)
{
	int t, err;

	WARN_ON(pool->status != THREADPOOL_STATUS__READY);

	pool->current_task = task;

	for (t = 0; t < pool->nr_threads; t++) {
		err = wake_thread(&pool->threads[t]);

		if (err) {
			pool->status = THREADPOOL_STATUS__ERROR;
			return err;
		}
	}

	pool->status = THREADPOOL_STATUS__BUSY;
	return 0;
}

/**
 * wait_threadpool - wait until all threads in @pool are done
 *
 * This function will wait for all threads to finish execution and send their
 * ack message.
 *
 * NB: call only from main thread!
 */
int wait_threadpool(struct threadpool_struct *pool)
{
	int t, err = 0, ret;

	if (pool->status != THREADPOOL_STATUS__BUSY)
		return 0;

	for (t = 0; t < pool->nr_threads; t++) {
		ret = wait_thread(&pool->threads[t]);
		if (ret) {
			pool->status = THREADPOOL_STATUS__ERROR;
			err = -1;
		}
	}

	pool->status = err ? THREADPOOL_STATUS__ERROR : THREADPOOL_STATUS__READY;
	pool->current_task = NULL;
	return err;
}

/**
 * threadpool_is_busy - check if the pool is busy
 */
int threadpool_is_busy(struct threadpool_struct *pool)
{
	return pool->status == THREADPOOL_STATUS__BUSY;
}
