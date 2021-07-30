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
#include <asm/bug.h>
#include <linux/zalloc.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <internal/lib.h>
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

static const char * const threadpool_status_tags[] = {
	"stopped",
	"ready",
	"busy",
	"error"
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

enum threadpool_msg {
	THREADPOOL_MSG__UNDEFINED = 0,
	THREADPOOL_MSG__ACK,		/* from th: create and exit ack */
	THREADPOOL_MSG__WAKE,		/* to th: wake up */
	THREADPOOL_MSG__STOP,		/* to th: exit */
	THREADPOOL_MSG__MAX
};

static const char * const threadpool_msg_tags[] = {
	"undefined",
	"ack",
	"wake",
	"stop"
};

static const char * const threadpool_errno_str[] = {
	"Error calling sigprocmask",
	"Error receiving message from thread",
	"Error sending message to thread",
	"Thread sent unexpected message",
	"This operation is not allowed in this state"
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
 * threadpool__wait_thread - receive ack from thread
 *
 * NB: call only from main thread!
 */
static int threadpool__wait_thread(struct threadpool_entry *thread)
{
	int res;
	enum threadpool_msg msg = THREADPOOL_MSG__UNDEFINED;

	res = readn(thread->pipes.ack[0], &msg, sizeof(msg));
	if (res < 0) {
		pr_debug2("threadpool: failed to recv msg from tid=%d: %s\n",
		       thread->tid, strerror(errno));
		return -THREADPOOL_ERROR__READPIPE;
	}
	if (msg != THREADPOOL_MSG__ACK) {
		pr_debug2("threadpool: received unexpected msg from tid=%d: %s\n",
		       thread->tid, threadpool_msg_tags[msg]);
		return -THREADPOOL_ERROR__INVALIDMSG;
	}

	pr_debug2("threadpool: received ack from tid=%d\n", thread->tid);

	return 0;
}

/**
 * threadpool__terminate_thread - send stop signal to thread and wait for ack
 *
 * NB: call only from main thread!
 */
static int threadpool__terminate_thread(struct threadpool_entry *thread)
{
	int res;
	enum threadpool_msg msg = THREADPOOL_MSG__STOP;

	res = writen(thread->pipes.cmd[1], &msg, sizeof(msg));
	if (res < 0) {
		pr_debug2("threadpool: error sending stop msg to tid=%d: %s\n",
			thread->tid, strerror(errno));
		return -THREADPOOL_ERROR__WRITEPIPE;
	}

	return threadpool__wait_thread(thread);
}

/**
 * threadpool__wake_thread - send wake msg to @thread
 *
 * This function does not wait for the thread to actually wake
 * NB: call only from main thread!
 */
static int threadpool__wake_thread(struct threadpool_entry *thread)
{
	int res;
	enum threadpool_msg msg = THREADPOOL_MSG__WAKE;

	res = writen(thread->pipes.cmd[1], &msg, sizeof(msg));
	if (res < 0) {
		pr_debug2("threadpool: error sending wake msg: %s\n", strerror(errno));
		return -THREADPOOL_ERROR__WRITEPIPE;
	}

	pr_debug2("threadpool: sent wake msg %s to tid=%d\n",
		threadpool_msg_tags[msg], thread->tid);
	return 0;
}

/**
 * threadpool_entry__function - send ack to main thread
 */
static int threadpool_entry__send_ack(struct threadpool_entry *thread)
{
	enum threadpool_msg msg = THREADPOOL_MSG__ACK;
	int ret = writen(thread->pipes.ack[1], &msg, sizeof(msg));

	if (ret < 0) {
		pr_debug("threadpool[%d]: failed to send ack: %s\n",
			thread->tid, strerror(errno));
		return -THREADPOOL_ERROR__WRITEPIPE;
	}

	return 0;
}

/**
 * threadpool_entry__recv_cmd - receive command from main thread
 */
static int threadpool_entry__recv_cmd(struct threadpool_entry *thread,
					enum threadpool_msg *cmd)
{
	int ret;

	*cmd = THREADPOOL_MSG__UNDEFINED;
	ret = readn(thread->pipes.cmd[0], cmd, sizeof(*cmd));
	if (ret < 0) {
		pr_debug("threadpool[%d]: error receiving command: %s\n",
			thread->tid, strerror(errno));
		return -THREADPOOL_ERROR__READPIPE;
	}

	if (*cmd != THREADPOOL_MSG__WAKE && *cmd != THREADPOOL_MSG__STOP) {
		pr_debug("threadpool[%d]: received unexpected command: %s\n",
			thread->tid, threadpool_msg_tags[*cmd]);
		return -THREADPOOL_ERROR__INVALIDMSG;
	}

	return 0;
}

/**
 * threadpool_entry__function - function running on thread
 *
 * This function waits for a signal from main thread to start executing
 * a task.
 * On completion, it will go back to sleep, waiting for another signal.
 * Signals are delivered through pipes.
 */
static void *threadpool_entry__function(void *args)
{
	struct threadpool_entry *thread = (struct threadpool_entry *) args;
	enum threadpool_msg cmd;

	thread->tid = gettid();

	pr_debug2("threadpool[%d]: started\n", thread->tid);

	for (;;) {
		if (threadpool_entry__send_ack(thread))
			break;

		if (threadpool_entry__recv_cmd(thread, &cmd))
			break;

		if (cmd == THREADPOOL_MSG__STOP)
			break;

		if (!thread->pool->current_task) {
			pr_debug("threadpool[%d]: received wake without task\n",
				thread->tid);
			break;
		}

		pr_debug("threadpool[%d]: executing task\n", thread->tid);
		thread->pool->current_task->fn(thread->idx, thread->pool->current_task);
	}

	pr_debug2("threadpool[%d]: exit\n", thread->tid);

	threadpool_entry__send_ack(thread);

	return NULL;
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
int threadpool__strerror(struct threadpool *pool, int err, char *buf, size_t size)
{
	char sbuf[STRERR_BUFSIZE], *emsg;
	const char *status_str, *errno_str;

	status_str = IS_ERR_OR_NULL(pool) ? "error" : threadpool_status_tags[pool->status];

	switch (err) {
	case -THREADPOOL_ERROR__SIGPROCMASK:
	case -THREADPOOL_ERROR__READPIPE:
	case -THREADPOOL_ERROR__WRITEPIPE:
		emsg = str_error_r(errno, sbuf, sizeof(sbuf));
		errno_str = threadpool_errno_str[-err-THREADPOOL_ERROR__OFFSET];
		return scnprintf(buf, size, "%s: %s.\n", errno_str, emsg);
	case -THREADPOOL_ERROR__INVALIDMSG:
		errno_str = threadpool_errno_str[-err-THREADPOOL_ERROR__OFFSET];
		return scnprintf(buf, size, "%s.\n", errno_str);
	case -THREADPOOL_ERROR__NOTALLOWED:
		return scnprintf(buf, size, "%s (%s).\n",
			threadpool_errno_str[-err], status_str);
	default:
		emsg = str_error_r(err, sbuf, sizeof(sbuf));
		return scnprintf(buf, size, "Error: %s", emsg);
	}
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

/**
 * __threadpool__start - start all threads in the pool.
 *
 * NB: use threadpool_start. This function does not change @pool->status.
 */
static int __threadpool__start(struct threadpool *pool)
{
	int t, tt, ret, err = 0, nr_threads = pool->nr_threads;
	sigset_t full, mask;
	pthread_t handle;
	pthread_attr_t attrs;

	sigfillset(&full);
	if (sigprocmask(SIG_SETMASK, &full, &mask)) {
		pr_debug2("Failed to block signals on threads start: %s\n", strerror(errno));
		return -THREADPOOL_ERROR__SIGPROCMASK;
	}

	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);

	for (t = 0; t < nr_threads; t++) {
		struct threadpool_entry *thread = &pool->threads[t];

		ret = pthread_create(&handle, &attrs, threadpool_entry__function, thread);
		if (ret) {
			err = ret;
			pr_debug2("Failed to start threads: %s\n", strerror(errno));
			break;
		}
	}

	/**
	 * Wait for all threads that we managed to run.
	 * In case of further errors, continue to terminate possibly not
	 * failing threads.
	 */
	for (tt = 0; tt < t; tt++) {
		struct threadpool_entry *thread = &pool->threads[tt];

		ret = threadpool__wait_thread(thread);
		if (ret)
			err = ret;
	}

	/**
	 * In case of errors, terminate all threads that we managed to run.
	 */
	if (err) {
		for (tt = 0; tt < t; tt++)
			threadpool__terminate_thread(&pool->threads[tt]);
	}

	pthread_attr_destroy(&attrs);

	if (sigprocmask(SIG_SETMASK, &mask, NULL)) {
		pr_debug2("Failed to unblock signals on threads start: %s\n", strerror(errno));
		err = -THREADPOOL_ERROR__SIGPROCMASK;
	}

	return err;
}

/**
 * threadpool__start - start all threads in the pool.
 *
 * The function blocks until all threads are up and running.
 */
int threadpool__start(struct threadpool *pool)
{
	int ret;

	if (pool->status != THREADPOOL_STATUS__STOPPED) {
		pr_debug2("threadpool: starting not stopped pool\n");
		return -THREADPOOL_ERROR__NOTALLOWED;
	}

	ret = __threadpool__start(pool);
	if (ret) {
		pool->status = THREADPOOL_STATUS__ERROR;
		return ret;
	}
	pool->status = THREADPOOL_STATUS__READY;
	return 0;
}

/**
 * __threadpool__stop - stop all threads in the pool.
 *
 * NB: use threadpool_stop. This function does not change @pool->status.
 */
static int __threadpool__stop(struct threadpool *pool)
{
	int t, ret, err = 0;

	for (t = 0; t < pool->nr_threads; t++) {
		/**
		 * Even if a termination fails, we should continue to terminate
		 * all other threads.
		 */
		ret = threadpool__terminate_thread(&pool->threads[t]);
		if (ret)
			err = ret;
	}

	return err;
}

/**
 * threadpool__stop - stop all threads in the pool.
 *
 * This function blocks waiting for ack from all threads.
 * If the pool was busy, it will first wait for the task to finish.
 */
int threadpool__stop(struct threadpool *pool)
{
	int ret = threadpool__wait(pool);

	if (ret)
		return ret;

	if (pool->status != THREADPOOL_STATUS__READY) {
		pr_debug2("threadpool: stopping not ready pool\n");
		return -THREADPOOL_ERROR__NOTALLOWED;
	}

	ret = __threadpool__stop(pool);
	if (ret) {
		pool->status = THREADPOOL_STATUS__ERROR;
		return ret;
	}
	pool->status = THREADPOOL_STATUS__STOPPED;
	return 0;
}

/**
 * threadpool__is_ready - check if the threads are running
 */
bool threadpool__is_ready(struct threadpool *pool)
{
	return pool->status == THREADPOOL_STATUS__READY;
}

/**
 * __threadpool__execute - execute @task on all threads of the @pool
 *
 * NB: use threadpool__execute. This function does not change @pool->status.
 */
static int __threadpool__execute(struct threadpool *pool, struct task_struct *task)
{
	int t, ret;

	pool->current_task = task;

	for (t = 0; t < pool->nr_threads; t++) {
		ret = threadpool__wake_thread(&pool->threads[t]);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * threadpool__execute - execute @task on all threads of the @pool
 *
 * The task will run asynchronously wrt the main thread.
 * The task can be waited with threadpool__wait.
 *
 * NB: make sure the pool is ready before calling this, since no queueing is
 *     performed. If you need queueing, have a look at the workqueue.
 */
int threadpool__execute(struct threadpool *pool, struct task_struct *task)
{
	int ret;

	if (pool->status != THREADPOOL_STATUS__READY) {
		pr_debug2("threadpool: executing on not ready pool\n");
		return -THREADPOOL_ERROR__NOTALLOWED;
	}

	ret = __threadpool__execute(pool, task);
	if (ret) {
		pool->status = THREADPOOL_STATUS__ERROR;
		return ret;
	}
	pool->status = THREADPOOL_STATUS__BUSY;
	return 0;
}

/**
 * __threadpool__wait - wait until all threads in @pool are done
 *
 * NB: use threadpool__wait. This function does not change @pool->status.
 */
static int __threadpool__wait(struct threadpool *pool)
{
	int t, err = 0, ret;

	for (t = 0; t < pool->nr_threads; t++) {
		ret = threadpool__wait_thread(&pool->threads[t]);
		if (ret)
			err = ret;
	}

	pool->current_task = NULL;
	return err;
}

/**
 * threadpool__wait - wait until all threads in @pool are done
 *
 * This function will wait for all threads to finish execution and send their
 * ack message.
 *
 * NB: call only from main thread!
 */
int threadpool__wait(struct threadpool *pool)
{
	int ret;

	switch (pool->status) {
	case THREADPOOL_STATUS__BUSY:
		break;
	case THREADPOOL_STATUS__READY:
		return 0;
	default:
	case THREADPOOL_STATUS__STOPPED:
	case THREADPOOL_STATUS__ERROR:
	case THREADPOOL_STATUS__MAX:
		return -THREADPOOL_ERROR__NOTALLOWED;
	}

	ret = __threadpool__wait(pool);
	if (ret) {
		pool->status = THREADPOOL_STATUS__ERROR;
		return ret;
	}
	pool->status = THREADPOOL_STATUS__READY;
	return 0;
}

/**
 * threadpool__is_busy - check if the pool is busy
 */
int threadpool__is_busy(struct threadpool *pool)
{
	return pool->status == THREADPOOL_STATUS__BUSY;
}
