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
#include <linux/kernel.h>
#include "debug.h"
#include <internal/lib.h>
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
	WORKQUEUE_STATUS__STOPPING,	/* wq is being destructed */
	WORKQUEUE_STATUS__ERROR,
	WORKQUEUE_STATUS__MAX
};

static const char * const workqueue_status_tags[] = {
	"ready",
	"stopping",
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

struct worker {
	int				tidx;		/* idx of thread in pool */
	struct list_head		entry;		/* in idle or busy list */
	struct work_struct		*current_work;	/* work being processed */
	int				msg_pipe[2];	/* main thread comm pipes*/
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
 * wake_worker - wake worker @w of workqueue @wq assigning @work to do
 *
 * Called from main thread.
 * Moves worker from idle to busy list, assigns @work to it and sends it a
 * wake up message.
 *
 * NB: this function releases the lock to be able to send the notification
 * outside the critical section.
 */
static int wake_worker(struct workqueue_struct *wq, struct worker *w,
			struct work_struct *work)
__must_hold(&wq->lock)
__releases(&wq->lock)
{
	enum worker_msg msg = WORKER_MSG__WAKE;
	int ret;

	list_move(&w->entry, &wq->busy_list);
	w->current_work = work;
	unlock_workqueue(wq);

	// send wake msg outside critical section to reduce time spent inside it
	ret = writen(w->msg_pipe[1], &msg, sizeof(msg));
	if (ret < 0) {
		pr_debug2("wake_worker[%d]: error seding msg: %s\n",
			w->tidx, strerror(errno));
		return -WORKQUEUE_ERROR__WRITEPIPE;
	}

	return 0;
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
	ret = writen(w->msg_pipe[1], &msg, sizeof(msg));
	if (ret < 0) {
		pr_debug2("workqueue: error sending stop msg: %s\n",
			strerror(errno));
		return -WORKQUEUE_ERROR__WRITEPIPE;
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
		pr_debug2("worker[%d]: error opening pipe: %s\n", tidx, strerror(errno));
		return -ENOMEM;
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
	int ret, init_err = init_worker(&this_worker, tidx);

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

	ret = writen(wq->msg_pipe[1], &msg, sizeof(msg));
	if (ret < 0) {
		pr_debug("worker[%d]: error sending msg: %s\n",
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
		ret = readn(this_worker.msg_pipe[0], &msg, sizeof(msg));
		if (ret < 0 || (msg != WORKER_MSG__WAKE && msg != WORKER_MSG__STOP)) {
			pr_debug("worker[%d]: error receiving msg: %s\n",
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
				pr_debug2("worker[%d]: dequeued work\n",
					tidx);
			} else {
				this_worker.current_work = NULL;
				sleep_worker(wq, &this_worker);
				pr_debug2("worker[%d]: going to sleep\n",
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
					struct threadpool *pool)
{
	int ret, t;
	enum worker_msg msg;

	if (!threadpool__is_ready(pool)) {
		pr_debug2("workqueue: cannot attach to pool: pool is not ready\n");
		return -WORKQUEUE_ERROR__NOTALLOWED;
	}

	wq->pool = pool;

	wq->pool_errno = threadpool__execute(pool, &wq->task);
	if (wq->pool_errno)
		return -WORKQUEUE_ERROR__POOLEXE;


	// wait ack from all threads
	for (t = 0; t < threadpool__size(pool); t++) {
		msg = WORKER_MSG__UNDEFINED;
		ret = readn(wq->msg_pipe[0], &msg, sizeof(msg));
		if (ret < 0) {
			pr_debug("workqueue: error receiving ack: %s\n",
				strerror(errno));
			return -WORKQUEUE_ERROR__READPIPE;
		}
		if (msg != WORKER_MSG__READY) {
			pr_debug2("workqueue: received error\n");
			return -WORKQUEUE_ERROR__INVALIDMSG;
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
		pr_debug2("workqueue: cannot attach to pool: wq is not ready\n");
		return -WORKQUEUE_ERROR__NOTALLOWED;
	}

	wq->status = WORKQUEUE_STATUS__STOPPING;
	ret = flush_workqueue(wq);
	if (ret)
		return ret;

	lock_workqueue(wq);
	for_each_idle_worker(wq, w) {
		ret = stop_worker(w);
		if (ret)
			err = ret;
	}
	unlock_workqueue(wq);


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

/**
 * queue_work - add @work to @wq internal queue
 *
 * If there are idle threads, one of these will be woken up.
 * Otherwise, the work is added to the pending list.
 */
int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	struct worker *chosen_worker;
	int ret = 0;

	// in particular, this can fail if workqueue is marked to be stopping
	if (wq->status != WORKQUEUE_STATUS__READY) {
		pr_debug2("workqueue: trying to queue but workqueue is not ready\n");
		return -WORKQUEUE_ERROR__NOTALLOWED;
	}

	lock_workqueue(wq);
	if (list_empty(&wq->idle_list)) {
		list_add_tail(&work->entry, &wq->pending);
		unlock_workqueue(wq);
		pr_debug("workqueue: queued new work item\n");
	} else {
		chosen_worker = list_first_entry(&wq->idle_list, struct worker, entry);
		ret = wake_worker(wq, chosen_worker, work);
		pr_debug("workqueue: woke worker %d\n", chosen_worker->tidx);
	}

	if (ret) {
		wq->status = WORKQUEUE_STATUS__ERROR;
		return ret;
	}
	return 0;
}

/**
 * flush_workqueue - wait for all currently executed and pending work to finish
 *
 * This function blocks until all threads become idle.
 */
int flush_workqueue(struct workqueue_struct *wq)
{
	int err = 0, ret;

	lock_workqueue(wq);
	while (!list_empty(&wq->busy_list)) {
		ret = pthread_cond_wait(&wq->idle_cond, &wq->lock);
		if (ret) {
			pr_debug2("%s: error in pthread_cond_wait\n", __func__);
			err = -ret;
			break;
		}
	}
	unlock_workqueue(wq);

	return err;
}

/**
 * init_work - initialize the @work struct
 */
void init_work(struct work_struct *work)
{
	INIT_LIST_HEAD(&work->entry);
}

/* Parallel-for utility */

struct parallel_for_work {
	struct work_struct work;	/* work item that is queued */
	parallel_for_func_t func;	/* function to execute for each item */
	void *args;			/* additional args to pass to func */
	int start;			/* first item to execute */
	int num;			/* number of items to execute */
};

/**
 * parallel_for_work_fn - execute parallel_for_work.func in parallel
 *
 * This function will be executed by workqueue's workers.
 */
static void parallel_for_work_fn(struct work_struct *work)
{
	struct parallel_for_work *pfw = container_of(work, struct parallel_for_work, work);
	int i;

	for (i = 0; i < pfw->num; i++)
		pfw->func(pfw->start+i, pfw->args);
}

static inline void init_parallel_for_work(struct parallel_for_work *pfw,
					parallel_for_func_t func, void *args,
					int start, int num)
{
	init_work(&pfw->work);
	pfw->work.func = parallel_for_work_fn;
	pfw->func = func;
	pfw->args = args;
	pfw->start = start;
	pfw->num = num;

	pr_debug2("pfw: start=%d, num=%d\n", start, num);
}

/**
 * parallel_for - execute @func in parallel over indexes between @from and @to
 * @wq: workqueue that will run @func in parallel
 * @from: first index
 * @to: last index (excluded)
 * @work_size: number of indexes to handle on the same work item.
 *             ceil((to-from)/work_size) work items will be added to @wq
 *             NB: this is only a hint. The function will reduce the size of
 *                 the work items to fill all workers.
 * @func: function to execute in parallel
 * @args: additional arguments to @func
 *
 * This function is equivalent to:
 * for (i = from; i < to; i++) {
 *     // parallel
 *     func(i, args);
 * }
 * // sync
 *
 * This function takes care of:
 *  - creating balanced work items to submit to workqueue
 *  - submitting the work items to the workqueue
 *  - waiting for completion of the work items
 *  - cleanup of the work items
 */
int parallel_for(struct workqueue_struct *wq, int from, int to, int work_size,
		parallel_for_func_t func, void *args)
{
	int n = to-from;
	int n_work_items;
	int nr_threads = workqueue_nr_threads(wq);
	int i, j, start, num, m, base, num_per_item;
	struct parallel_for_work *pfw_array;
	int ret, err = 0;

	if (work_size <= 0) {
		pr_debug("workqueue parallel-for: work_size must be >0\n");
		return -EINVAL;
	}

	if (to < from) {
		pr_debug("workqueue parallel-for: to must be >= from\n");
		return -EINVAL;
	} else if (to == from) {
		pr_debug2("workqueue parallel-for: skip since from == to\n");
		return 0;
	}

	n_work_items = DIV_ROUND_UP(n, work_size);
	if (n_work_items < nr_threads)
		n_work_items = min(n, nr_threads);

	pfw_array = calloc(n_work_items, sizeof(*pfw_array));

	if (!pfw_array) {
		pr_debug2("%s: error allocating pfw_array\n", __func__);
		return -ENOMEM;
	}

	num_per_item = n / n_work_items;
	m = n % n_work_items;

	for (i = 0; i < m; i++) {
		num = num_per_item + 1;
		start = i * num;
		init_parallel_for_work(&pfw_array[i], func, args, start, num);
		ret = queue_work(wq, &pfw_array[i].work);
		if (ret) {
			err = ret;
			goto out;
		}
	}
	if (i != 0)
		base = pfw_array[i-1].start + pfw_array[i-1].num;
	else
		base = 0;
	for (j = i; j < n_work_items; j++) {
		num = num_per_item;
		start = base + (j - i) * num;
		init_parallel_for_work(&pfw_array[j], func, args, start, num);
		ret = queue_work(wq, &pfw_array[j].work);
		if (ret) {
			err = ret;
			goto out;
		}
	}

out:
	ret = flush_workqueue(wq);
	if (ret)
		err = ret;

	free(pfw_array);
	return err;
}
