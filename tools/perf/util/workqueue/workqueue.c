// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/string.h>
#include "debug.h"
#include <internal/lib.h>
#include "workqueue.h"

enum workqueue_status {
	WORKQUEUE_STATUS__READY,	/* wq is ready to receive work */
	WORKQUEUE_STATUS__ERROR,
	WORKQUEUE_STATUS__MAX
};

static const char * const workqueue_status_tags[] = {
	"ready",
	"error"
};

struct workqueue_struct {
	pthread_mutex_t		lock;		/* locking of the thread_pool */
	pthread_cond_t		idle_cond;	/* all workers are idle cond */
	struct threadpool	*pool;		/* underlying pool */
	int			pool_errno;	/* latest pool error */
	struct task_struct	task;		/* threadpool task */
	struct list_head	busy_list;	/* busy workers */
	struct list_head	idle_list;	/* idle workers */
	struct list_head	pending;	/* pending work items */
	int			msg_pipe[2];	/* main thread comm pipes */
	enum workqueue_status	status;
};

static const char * const workqueue_errno_str[] = {
	"",
	"This operation is not allowed in this workqueue status",
	"Error executing function in threadpool",
	"Error waiting the threadpool",
	"Error sending message to worker",
	"Error receiving message from worker",
	"Received unexpected message from worker",
};

/**
 * worker_thread - worker function executed on threadpool
 */
static void worker_thread(int tidx, struct task_struct *task)
{

	pr_info("Hi from worker %d, executing task %p\n", tidx, task);
}

/**
 * attach_threadpool_to_workqueue - start @wq workers on @pool
 */
static int attach_threadpool_to_workqueue(struct workqueue_struct *wq,
					struct threadpool *pool)
{
	if (!threadpool__is_ready(pool)) {
		pr_debug2("workqueue: cannot attach to pool: pool is not ready\n");
		return -WORKQUEUE_ERROR__NOTALLOWED;
	}

	wq->pool = pool;

	wq->pool_errno = threadpool__execute(pool, &wq->task);
	if (wq->pool_errno)
		return -WORKQUEUE_ERROR__POOLEXE;

	return 0;
}

/**
 * detach_threadpool_from_workqueue - stop @wq workers on @pool
 */
static int detach_threadpool_from_workqueue(struct workqueue_struct *wq)
{
	int ret, err = 0;

	if (wq->status != WORKQUEUE_STATUS__READY) {
		pr_debug2("workqueue: cannot attach to pool: wq is not ready\n");
		return -WORKQUEUE_ERROR__NOTALLOWED;
	}

	ret = threadpool__wait(wq->pool);
	if (ret) {
		pr_debug2("workqueue: error waiting threadpool\n");
		wq->pool_errno = ret;
		err = -WORKQUEUE_ERROR__POOLWAIT;
	}

	wq->pool = NULL;
	return err;
}

/**
 * create_workqueue - create a workqueue associated to @pool
 *
 * Only one workqueue can execute on a pool at a time.
 */
struct workqueue_struct *create_workqueue(struct threadpool *pool)
{
	int ret, err = 0;
	struct workqueue_struct *wq = malloc(sizeof(struct workqueue_struct));

	if (!wq) {
		err = -ENOMEM;
		goto out_return;
	}

	ret = pthread_mutex_init(&wq->lock, NULL);
	if (ret) {
		err = -ret;
		goto out_free_wq;
	}

	ret = pthread_cond_init(&wq->idle_cond, NULL);
	if (ret) {
		err = -ret;
		goto out_destroy_mutex;
	}

	wq->pool = NULL;
	INIT_LIST_HEAD(&wq->busy_list);
	INIT_LIST_HEAD(&wq->idle_list);

	INIT_LIST_HEAD(&wq->pending);

	ret = pipe(wq->msg_pipe);
	if (ret) {
		err = -ENOMEM;
		goto out_destroy_cond;
	}

	wq->task.fn = worker_thread;

	ret = attach_threadpool_to_workqueue(wq, pool);
	if (ret) {
		err = ret;
		goto out_destroy_cond;
	}

	wq->status = WORKQUEUE_STATUS__READY;

	return wq;

out_destroy_cond:
	pthread_cond_destroy(&wq->idle_cond);
out_destroy_mutex:
	pthread_mutex_destroy(&wq->lock);
out_free_wq:
	free(wq);
out_return:
	return ERR_PTR(err);
}

/**
 * destroy_workqueue - stop @wq workers and destroy @wq
 */
int destroy_workqueue(struct workqueue_struct *wq)
{
	int err = 0, ret;

	if (IS_ERR_OR_NULL(wq))
		return 0;

	ret = detach_threadpool_from_workqueue(wq);
	if (ret) {
		pr_debug2("workqueue: error detaching from threadpool.\n");
		err = ret;
	}

	ret = pthread_mutex_destroy(&wq->lock);
	if (ret) {
		err = -ret;
		pr_debug2("workqueue: error pthread_mutex_destroy: %s\n",
			strerror(errno));
	}

	ret = pthread_cond_destroy(&wq->idle_cond);
	if (ret) {
		err = -ret;
		pr_debug2("workqueue: error pthread_cond_destroy: %s\n",
			strerror(errno));
	}

	close(wq->msg_pipe[0]);
	wq->msg_pipe[0] = -1;

	close(wq->msg_pipe[1]);
	wq->msg_pipe[1] = -1;

	free(wq);
	return err;
}

/**
 * workqueue_strerror - print message regarding lastest error in @wq
 *
 * Buffer size should be at least WORKQUEUE_STRERR_BUFSIZE bytes.
 */
int workqueue_strerror(struct workqueue_struct *wq, int err, char *buf, size_t size)
{
	int ret;
	char sbuf[THREADPOOL_STRERR_BUFSIZE], *emsg;
	const char *status_str, *errno_str;

	status_str = IS_ERR_OR_NULL(wq) ? "error" : workqueue_status_tags[wq->status];
	errno_str = workqueue_errno_str[-err-WORKQUEUE_ERROR__OFFSET];

	switch (err) {
	case -WORKQUEUE_ERROR__NOTALLOWED:
		return scnprintf(buf, size, "%s (%s).\n",
			errno_str, status_str);
	case -WORKQUEUE_ERROR__POOLEXE:
	case -WORKQUEUE_ERROR__POOLWAIT:
		if (IS_ERR_OR_NULL(wq))
			return scnprintf(buf, size, "%s: unknown.\n",
				errno_str);

		ret = threadpool__strerror(wq->pool, wq->pool_errno, sbuf, sizeof(sbuf));
		if (ret < 0)
			return ret;
		return scnprintf(buf, size, "%s: %s.\n", errno_str, sbuf);
	case -WORKQUEUE_ERROR__WRITEPIPE:
	case -WORKQUEUE_ERROR__READPIPE:
		emsg = str_error_r(errno, sbuf, sizeof(sbuf));
		return scnprintf(buf, size, "%s: %s.\n", errno_str, emsg);
	case -WORKQUEUE_ERROR__INVALIDMSG:
		return scnprintf(buf, size, "%s.\n", errno_str);
	default:
		emsg = str_error_r(err, sbuf, sizeof(sbuf));
		return scnprintf(buf, size, "Error: %s", emsg);
	}
}

/**
 * create_workqueue_strerror - print message regarding @err_ptr
 *
 * Buffer size should be at least WORKQUEUE_STRERR_BUFSIZE bytes.
 */
int create_workqueue_strerror(struct workqueue_struct *err_ptr, char *buf, size_t size)
{
	return workqueue_strerror(err_ptr, PTR_ERR(err_ptr), buf, size);
}

/**
 * destroy_workqueue_strerror - print message regarding @err
 *
 * Buffer size should be at least WORKQUEUE_STRERR_BUFSIZE bytes.
 */
int destroy_workqueue_strerror(int err, char *buf, size_t size)
{
	return workqueue_strerror(NULL, err, buf, size);
}

/**
 * workqueue_nr_threads - get size of threadpool underlying @wq
 */
int workqueue_nr_threads(struct workqueue_struct *wq)
{
	return threadpool__size(wq->pool);
}
