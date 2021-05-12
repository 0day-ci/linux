// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019-2021 Intel Corporation */

/*
 * message scheduler implementation.
 *
 * That implements a scheduler object which is used to serialize
 * command submission to an NNP-I device.
 * It manages a list of message queues which hold command messages
 * to be submitted to the card.
 * It also implements a kernel thread which schedules draining
 * the message queues in round-robin fashion.
 *
 * An instance of this object is created for each NNP-I device.
 * A message queue is created for each user created channel as well
 * as one message queue which is used by the kernel driver itself.
 */

#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "device.h"
#include "msg_scheduler.h"

/**
 * struct msg_entry - struct to hold a single command message
 * @msg: command message payload
 * @size: size in 64-bit words
 * @node: node to be included in list of command messages.
 */
struct msg_entry {
	u64              msg[MSG_SCHED_MAX_MSG_SIZE];
	unsigned int     size;
	struct list_head node;
};

/**
 * do_sched() - fetch and write a message from one message queue.
 * @sched: the scheduler
 * @q: the queue to handle
 *
 * This function is called from the main scheduler thread to handle single
 * message queue. It fetches one message from the queue and send it to the
 * NNP-I device.
 *
 * The function should be called when the scheduler mutex is held to prevent
 * the queue from being destroyed.
 */
static void do_sched(struct nnp_msched *sched, struct nnp_msched_queue *q)
{
	struct nnp_device *nnpdev = sched->nnpdev;
	struct msg_entry *msg;
	unsigned int left_msgs;

	lockdep_assert_held(&sched->mutex);

	/* Fetch one message from the queue */
	spin_lock(&q->list_lock);
	if (list_empty(&q->msgs)) {
		spin_unlock(&q->list_lock);
		return;
	}

	msg = list_first_entry(&q->msgs, struct msg_entry, node);
	list_del(&msg->node);
	q->msgs_num--;
	left_msgs = q->msgs_num;
	spin_lock(&sched->total_msgs_lock);
	sched->total_msgs--;
	spin_unlock(&sched->total_msgs_lock);
	spin_unlock(&q->list_lock);

	/*
	 * Write the fetched message out.
	 * Note that cmdq_write_mesg function may sleep.
	 */
	nnpdev->ops->cmdq_write_mesg(nnpdev, msg->msg, msg->size);

	kmem_cache_free(sched->slab_cache_ptr, msg);

	/*
	 * Wake any waiting sync thread if the queue just
	 * became empty
	 */
	if (!left_msgs)
		wake_up_all(&q->sync_waitq);
}

/**
 * msg_sched_thread() - the main function of the scheduler thread.
 * @data: pointer to the msg scheduler object.
 *
 * This is the main function of the scheduler kernel thread.
 * It loops in round-robin fashion on all queues, pulls one message
 * each time and send it to the NNP-I device.
 * For each application created channel, a different queue of
 * command messages is allocated. This thread schedules and serializes
 * accesses to the NNP-I device's command queue.
 *
 * Return: 0 when thread is stopped
 */
static int msg_sched_thread(void *data)
{
	struct nnp_msched *dev_sched = data;
	struct nnp_msched_queue *q;
	bool need_sched;

	while (!kthread_should_stop()) {
		mutex_lock(&dev_sched->mutex);
		list_for_each_entry(q, &dev_sched->queues, node)
			do_sched(dev_sched, q);

		/*
		 * Wait for new messages to be available in some queue
		 * if no messages are known to exist
		 */
		spin_lock(&dev_sched->total_msgs_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		need_sched = !dev_sched->total_msgs;
		spin_unlock(&dev_sched->total_msgs_lock);
		mutex_unlock(&dev_sched->mutex);
		if (need_sched)
			schedule();
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

struct nnp_msched_queue *nnp_msched_queue_create(struct nnp_msched *scheduler)
{
	struct nnp_msched_queue *queue;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return NULL;

	INIT_LIST_HEAD(&queue->msgs);
	spin_lock_init(&queue->list_lock);
	queue->msgs_num = 0;
	queue->scheduler = scheduler;
	init_waitqueue_head(&queue->sync_waitq);

	mutex_lock(&scheduler->mutex);
	list_add_tail(&queue->node, &scheduler->queues);
	mutex_unlock(&scheduler->mutex);

	return queue;
}

int nnp_msched_queue_destroy(struct nnp_msched_queue *queue)
{
	struct msg_entry *msg;

	/* detach the queue from list of scheduled queues */
	mutex_lock(&queue->scheduler->mutex);
	list_del(&queue->node);
	mutex_unlock(&queue->scheduler->mutex);

	/* destroy all the messages of the queue */
	spin_lock(&queue->list_lock);
	while (!list_empty(&queue->msgs)) {
		msg = list_first_entry(&queue->msgs, struct msg_entry, node);
		list_del(&msg->node);
		kmem_cache_free(queue->scheduler->slab_cache_ptr, msg);
	}
	spin_unlock(&queue->list_lock);

	kfree(queue);

	return 0;
}

static inline bool is_queue_empty(struct nnp_msched_queue *queue)
{
	bool ret;

	spin_lock(&queue->list_lock);
	ret = list_empty(&queue->msgs);
	spin_unlock(&queue->list_lock);

	return ret;
}

int nnp_msched_queue_sync(struct nnp_msched_queue *queue)
{
	int ret;

	/* Wait for the queue to be empty */
	ret = wait_event_interruptible(queue->sync_waitq, is_queue_empty(queue));

	return ret;
}

int nnp_msched_queue_add_msg(struct nnp_msched_queue *queue, u64 *msg,
			     unsigned int size)
{
	unsigned int i;
	struct msg_entry *m;
	bool throttled;

	if (size > MSG_SCHED_MAX_MSG_SIZE)
		return -EINVAL;

	m = kmem_cache_alloc(queue->scheduler->slab_cache_ptr, GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	for (i = 0; i < size; i++)
		m->msg[i] = msg[i];

	m->size = size;

	spin_lock(&queue->list_lock);
	throttled = queue->throttled;
	if (!throttled) {
		list_add_tail(&m->node, &queue->msgs);
		queue->msgs_num++;
		spin_lock(&queue->scheduler->total_msgs_lock);
		queue->scheduler->total_msgs++;
		spin_unlock(&queue->scheduler->total_msgs_lock);
	}
	spin_unlock(&queue->list_lock);

	/* if queue flagged as throttled - silently ignore the message */
	if (throttled) {
		kmem_cache_free(queue->scheduler->slab_cache_ptr, m);
		return 0;
	}

	wake_up_process(queue->scheduler->thread);

	return 0;
}

struct nnp_msched *nnp_msched_create(struct nnp_device *nnpdev)
{
	struct nnp_msched *dev_sched;

	dev_sched = kzalloc(sizeof(*dev_sched), GFP_KERNEL);
	if (!dev_sched)
		return NULL;

	dev_sched->slab_cache_ptr = kmem_cache_create("msg_sched_slab",
						      sizeof(struct msg_entry),
						      0, 0, NULL);
	if (!dev_sched->slab_cache_ptr) {
		kfree(dev_sched);
		return NULL;
	}

	INIT_LIST_HEAD(&dev_sched->queues);

	spin_lock_init(&dev_sched->total_msgs_lock);
	mutex_init(&dev_sched->mutex);
	dev_sched->nnpdev = nnpdev;

	dev_sched->thread = kthread_run(msg_sched_thread, dev_sched,
					"msg_sched_thread");
	if (!dev_sched->thread) {
		kmem_cache_destroy(dev_sched->slab_cache_ptr);
		kfree(dev_sched);
		return NULL;
	}

	return dev_sched;
}

void nnp_msched_destroy(struct nnp_msched *sched)
{
	struct nnp_msched_queue *q, *tmp;

	nnp_msched_throttle_all(sched);

	kthread_stop(sched->thread);

	mutex_lock(&sched->mutex);
	list_for_each_entry_safe(q, tmp, &sched->queues, node) {
		/* destroy the queue */
		list_del(&q->node);
		kfree(q);
	}
	mutex_unlock(&sched->mutex);

	kmem_cache_destroy(sched->slab_cache_ptr);

	kfree(sched);
}

void nnp_msched_throttle_all(struct nnp_msched *sched)
{
	struct nnp_msched_queue *q;
	struct msg_entry *msg, *tmp;

	/*
	 * For each queue:
	 * 1) throttle the queue, so that no more messages will be inserted
	 * 2) delete all existing messages
	 */
	mutex_lock(&sched->mutex);
	list_for_each_entry(q, &sched->queues, node) {
		spin_lock(&q->list_lock);
		q->throttled = true;
		list_for_each_entry_safe(msg, tmp, &q->msgs, node) {
			list_del(&msg->node);
			kmem_cache_free(sched->slab_cache_ptr, msg);
		}
		q->msgs_num = 0;
		spin_unlock(&q->list_lock);
		wake_up_all(&q->sync_waitq);
	}
	mutex_unlock(&sched->mutex);
}
