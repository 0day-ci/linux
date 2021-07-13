// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <linux/list.h>
#include "debug.h"
#include "workqueue.h"

enum workqueue_status {
	WORKQUEUE_STATUS__READY,	/* wq is ready to receive work */
	WORKQUEUE_STATUS__ERROR,
	WORKQUEUE_STATUS__MAX
};

struct workqueue_struct {
	pthread_mutex_t		lock;		/* locking of the thread_pool */
	pthread_cond_t		idle_cond;	/* all workers are idle cond */
	struct threadpool_struct *pool;		/* underlying pool */
	struct task_struct	task;		/* threadpool task */
	struct list_head	busy_list;	/* busy workers */
	struct list_head	idle_list;	/* idle workers */
	struct list_head	pending;	/* pending work items */
	int			msg_pipe[2];	/* main thread comm pipes */
	enum workqueue_status	status;
};

/**
 * worker_thread - worker function executed on threadpool
 */
static void worker_thread(int tidx, struct task_struct *task)
{
	struct workqueue_struct *wq = container_of(task, struct workqueue_struct, task);

	pr_debug("hi from worker %d. Pool is in status %d\n", tidx, wq->status);
}

/**
 * attach_threadpool_to_workqueue - start @wq workers on @pool
 */
static int attach_threadpool_to_workqueue(struct workqueue_struct *wq,
					struct threadpool_struct *pool)
{
	int err;

	if (!threadpool_is_ready(pool)) {
		pr_err("workqueue: cannot attach to pool: pool is not ready\n");
		return -1;
	}

	wq->pool = pool;

	err = execute_in_threadpool(pool, &wq->task);
	if (err)
		return -1;

	return 0;
}

/**
 * detach_threadpool_from_workqueue - stop @wq workers on @pool
 */
static int detach_threadpool_from_workqueue(struct workqueue_struct *wq)
{
	int ret, err = 0;

	if (wq->status != WORKQUEUE_STATUS__READY) {
		pr_err("workqueue: cannot attach to pool: wq is not ready\n");
		return -1;
	}

	ret = wait_threadpool(wq->pool);
	if (ret) {
		pr_err("workqueue: error waiting threadpool\n");
		err = -1;
	}

	wq->pool = NULL;
	return err;
}

/**
 * create_workqueue - create a workqueue associated to @pool
 *
 * Only one workqueue can execute on a pool at a time.
 */
struct workqueue_struct *create_workqueue(struct threadpool_struct *pool)
{
	int err;
	struct workqueue_struct *wq = malloc(sizeof(struct workqueue_struct));


	err = pthread_mutex_init(&wq->lock, NULL);
	if (err)
		goto out_free_wq;

	err = pthread_cond_init(&wq->idle_cond, NULL);
	if (err)
		goto out_destroy_mutex;

	wq->pool = NULL;
	INIT_LIST_HEAD(&wq->busy_list);
	INIT_LIST_HEAD(&wq->idle_list);

	INIT_LIST_HEAD(&wq->pending);

	err = pipe(wq->msg_pipe);
	if (err)
		goto out_destroy_cond;

	wq->task.fn = worker_thread;

	err = attach_threadpool_to_workqueue(wq, pool);
	if (err)
		goto out_destroy_cond;

	wq->status = WORKQUEUE_STATUS__READY;

	return wq;

out_destroy_cond:
	pthread_cond_destroy(&wq->idle_cond);
out_destroy_mutex:
	pthread_mutex_destroy(&wq->lock);
out_free_wq:
	free(wq);
	return NULL;
}

/**
 * destroy_workqueue - stop @wq workers and destroy @wq
 */
int destroy_workqueue(struct workqueue_struct *wq)
{
	int err = 0, ret;

	ret = detach_threadpool_from_workqueue(wq);
	if (ret) {
		pr_err("workqueue: error detaching from threadpool.\n");
		err = -1;
	}

	ret = pthread_mutex_destroy(&wq->lock);
	if (ret) {
		err = -1;
		pr_err("workqueue: error pthread_mutex_destroy: %s\n",
			strerror(errno));
	}

	ret = pthread_cond_destroy(&wq->idle_cond);
	if (ret) {
		err = -1;
		pr_err("workqueue: error pthread_cond_destroy: %s\n",
			strerror(errno));
	}

	ret = close(wq->msg_pipe[0]);
	if (ret) {
		err = -1;
		pr_err("workqueue: error close msg_pipe[0]: %s\n",
			strerror(errno));
	}

	ret = close(wq->msg_pipe[1]);
	if (ret) {
		err = -1;
		pr_err("workqueue: error close msg_pipe[1]: %s\n",
			strerror(errno));
	}

	free(wq);

	return err;
}

/**
 * workqueue_nr_threads - get size of threadpool underlying @wq
 */
int workqueue_nr_threads(struct workqueue_struct *wq)
{
	return threadpool_size(wq->pool);
}
