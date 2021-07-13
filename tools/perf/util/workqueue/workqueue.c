// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <linux/list.h>
#include "debug.h"
#include "sparse.h"
#include "workqueue.h"

enum worker_msg {
	WORKER_MSG__UNDEFINED,
	WORKER_MSG__READY,                          /* from worker: ack */
	WORKER_MSG__WAKE,                           /* to worker: wake up */
	WORKER_MSG__STOP,                           /* to worker: exit */
	WORKER_MSG__ERROR,
	WORKER_MSG__MAX
};

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

struct worker {
	int                         tidx;           /* idx of thread in pool */
	struct list_head	        entry;          /* in idle or busy list */
	struct work_struct	        *current_work;	/* work being processed */
	int                         msg_pipe[2];    /* main thread comm pipes*/
};

#define for_each_busy_worker(wq, m_worker) \
	list_for_each_entry(m_worker, &wq->busy_list, entry)

#define for_each_idle_worker(wq, m_worker) \
	list_for_each_entry(m_worker, &wq->idle_list, entry)

static inline int lock_workqueue(struct workqueue_struct *wq)
__acquires(&wq->lock)
{
	__acquire(&wq->lock);
	return pthread_mutex_lock(&wq->lock);
}

static inline int unlock_workqueue(struct workqueue_struct *wq)
__releases(&wq->lock)
{
	__release(&wq->lock);
	return pthread_mutex_unlock(&wq->lock);
}

/**
 * available_work - check if @wq has work to do
 */
static int available_work(struct workqueue_struct *wq)
__must_hold(&wq->lock)
{
	return !list_empty(&wq->pending);
}

/**
 * dequeue_work - retrieve the next work in @wq to be executed by the worker
 *
 * Called inside worker.
 */
static struct work_struct *dequeue_work(struct workqueue_struct *wq)
__must_hold(&wq->lock)
{
	struct work_struct *work = list_first_entry(&wq->pending, struct work_struct, entry);

	list_del_init(&work->entry);
	return work;
}

/**
 * sleep_worker - worker @w of workqueue @wq goes to sleep
 *
 * Called inside worker.
 * If this was the last idle thread, signal it to the main thread, in case it
 * was flushing the workqueue.
 */
static void sleep_worker(struct workqueue_struct *wq, struct worker *w)
__must_hold(&wq->lock)
{
	list_move(&w->entry, &wq->idle_list);
	if (list_empty(&wq->busy_list))
		pthread_cond_signal(&wq->idle_cond);
}

/**
 * stop_worker - stop worker @w
 *
 * Called from main thread.
 * Send stop message to worker @w.
 */
static int stop_worker(struct worker *w)
{
	int ret;
	enum worker_msg msg;

	msg = WORKER_MSG__STOP;
	ret = write(w->msg_pipe[1], &msg, sizeof(msg));
	if (ret < 0) {
		pr_err("workqueue: error sending stop msg: %s\n",
			strerror(errno));
		return -1;
	}

	return 0;
}

/**
 * init_worker - init @w struct
 * @w: the struct to init
 * @tidx: index of the executing thread inside the threadpool
 */
static int init_worker(struct worker *w, int tidx)
{
	if (pipe(w->msg_pipe)) {
		pr_err("worker[%d]: error opening pipe: %s\n", tidx, strerror(errno));
		return -1;
	}

	w->tidx = tidx;
	w->current_work = NULL;
	INIT_LIST_HEAD(&w->entry);

	return 0;
}

/**
 * fini_worker - deallocate resources used by @w struct
 */
static void fini_worker(struct worker *w)
{
	close(w->msg_pipe[0]);
	w->msg_pipe[0] = -1;
	close(w->msg_pipe[1]);
	w->msg_pipe[1] = -1;
}

/**
 * register_worker - add worker to @wq->idle_list
 */
static void register_worker(struct workqueue_struct *wq, struct worker *w)
__must_hold(&wq->lock)
{
	list_move(&w->entry, &wq->idle_list);
}

/**
 * unregister_worker - remove worker from @wq->idle_list
 */
static void unregister_worker(struct workqueue_struct *wq __maybe_unused,
			struct worker *w)
__must_hold(&wq->lock)
{
	list_del_init(&w->entry);
}

/**
 * worker_thread - worker function executed on threadpool
 */
static void worker_thread(int tidx, struct task_struct *task)
{
	struct workqueue_struct *wq = container_of(task, struct workqueue_struct, task);
	struct worker this_worker;
	enum worker_msg msg;
	int ret, init_err;

	init_err = init_worker(&this_worker, tidx);
	if (init_err) {
		// send error message to main thread
		msg = WORKER_MSG__ERROR;
	} else {
		lock_workqueue(wq);
		register_worker(wq, &this_worker);
		unlock_workqueue(wq);

		// ack worker creation
		msg = WORKER_MSG__READY;
	}

	ret = write(wq->msg_pipe[1], &msg, sizeof(msg));
	if (ret < 0) {
		pr_err("worker[%d]: error sending msg: %s\n",
			tidx, strerror(errno));

		if (init_err)
			return;
		goto out;
	}

	// stop if there have been errors in init
	if (init_err)
		return;

	for (;;) {
		msg = WORKER_MSG__UNDEFINED;
		ret = read(this_worker.msg_pipe[0], &msg, sizeof(msg));
		if (ret < 0 || (msg != WORKER_MSG__WAKE && msg != WORKER_MSG__STOP)) {
			pr_err("worker[%d]: error receiving msg: %s\n",
				tidx, strerror(errno));
			break;
		}

		if (msg == WORKER_MSG__STOP)
			break;

		// main thread takes care of moving to busy list and assigning current_work

		while (this_worker.current_work) {
			this_worker.current_work->func(this_worker.current_work);

			lock_workqueue(wq);
			if (available_work(wq)) {
				this_worker.current_work = dequeue_work(wq);
				pr_debug("worker[%d]: dequeued work\n",
					tidx);
			} else {
				this_worker.current_work = NULL;
				sleep_worker(wq, &this_worker);
				pr_debug("worker[%d]: going to sleep\n",
					tidx);
			}
			unlock_workqueue(wq);
		}
	}

out:
	lock_workqueue(wq);
	unregister_worker(wq, &this_worker);
	unlock_workqueue(wq);

	fini_worker(&this_worker);
}

/**
 * attach_threadpool_to_workqueue - start @wq workers on @pool
 */
static int attach_threadpool_to_workqueue(struct workqueue_struct *wq,
					struct threadpool_struct *pool)
{
	int err, ret, t;
	enum worker_msg msg;

	if (!threadpool_is_ready(pool)) {
		pr_err("workqueue: cannot attach to pool: pool is not ready\n");
		return -1;
	}

	wq->pool = pool;

	err = execute_in_threadpool(pool, &wq->task);
	if (err)
		return -1;


	// wait ack from all threads
	for (t = 0; t < threadpool_size(pool); t++) {
		msg = WORKER_MSG__UNDEFINED;
		ret = read(wq->msg_pipe[0], &msg, sizeof(msg));
		if (ret < 0) {
			pr_err("workqueue: error receiving ack: %s\n",
				strerror(errno));
			return -1;
		}
		if (msg != WORKER_MSG__READY) {
			pr_err("workqueue: received error\n");
			return -1;
		}
	}

	return 0;
}

/**
 * detach_threadpool_from_workqueue - stop @wq workers on @pool
 */
static int detach_threadpool_from_workqueue(struct workqueue_struct *wq)
{
	int ret, err = 0;
	struct worker *w;

	if (wq->status != WORKQUEUE_STATUS__READY) {
		pr_err("workqueue: cannot attach to pool: wq is not ready\n");
		return -1;
	}

	lock_workqueue(wq);
	for_each_idle_worker(wq, w) {
		ret = stop_worker(w);
		if (ret)
			err = -1;
	}
	unlock_workqueue(wq);


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
