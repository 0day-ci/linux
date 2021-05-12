/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNP_MSGF_SCHEDULER_H
#define _NNP_MSGF_SCHEDULER_H

#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define MSG_SCHED_MAX_MSG_SIZE 3  /* maximum command message size, i qwords */

/**
 * struct nnp_msched - structure for msg scheduler object
 * @thread: kernel thread which schedules message writes to device
 * @nnpdev: the device the scheduler writes to
 * @queues: list of message queues to schedule from
 * @total_msgs_lock: protects accesses to @total_msgs
 * @mutex: protects modifications to @queues
 * @total_msgs: total count of messages in all queues yet to be written.
 * @slab_cache_ptr: used to allocate entries in msg queue list.
 *
 * We have one msg scheduler object allocated for each NNP-I device,
 * It manages a list of command message queues and a kernel thread
 * which schedules sending the command messages to the device in a
 * round-robin fashion.
 */
struct nnp_msched {
	struct task_struct *thread;
	struct nnp_device  *nnpdev;
	struct list_head   queues;
	spinlock_t         total_msgs_lock;
	struct mutex       mutex;
	unsigned int       total_msgs;
	struct kmem_cache  *slab_cache_ptr;
};

/**
 * struct nnp_msched_queue - structure to hold one list of command messages
 * @scheduler: the scheduler object this queue belongs to
 * @node: node of this element in @queues in msg_sched
 * @msgs: list of command messages
 * @sync_waitq: waitq used for waiting until queue becomes empty
 * @throttled: if true, all messages in the queue should be discarded and no new
 *             messages can be added to it until it will become un-throttled.
 * @msgs_num: number of messages in the queue
 * @list_lock: protects @msgs
 *
 * This structure holds a list of command messages to be queued for submission
 * to the device. Each application holding a channel for command submissions
 * has its own command message queue.
 */
struct nnp_msched_queue {
	struct nnp_msched *scheduler;
	struct list_head  node;
	struct list_head  msgs;
	wait_queue_head_t sync_waitq;
	bool              throttled;
	unsigned int      msgs_num;
	spinlock_t        list_lock;
};

/**
 * nnp_msched_create() - creates msg scheduler object
 * @nnpdev: the device this scheduler writes messages to.
 *
 * This function creates a message scheduler object which can hold
 * multiple message queues and a scheduling thread which pop messages
 * from the different queues and synchronously sends them down to the device
 * for transmission.
 *
 * Return: pointer to allocated scheduler object or NULL on failure
 */
struct nnp_msched *nnp_msched_create(struct nnp_device *nnpdev);

/**
 * nnp_msched_destroy() - destroys a msg scheduler object
 * @sched: pointer to msg scheduler object
 *
 * This function will wait for the scheduler thread to complete
 * and destroys the scheduler object as well as all messages and message
 * queues.
 * NOTE: caller must make sure that no new queues and messages will be added
 * to this scheduler object while this function is in progress! There is no
 * mutex to protect this, should be handled by the caller.
 */
void nnp_msched_destroy(struct nnp_msched *sched);

/**
 * nnp_msched_throttle_all() - Remove all messages and throttle all queues
 * @sched: pointer to msg scheduler object
 *
 * This function removes all messages from all queues and marks all queues
 * as throttled. No new messages can be added to a throttled queue until it
 * becomes unthrottled.
 *
 * This function is called before the device is reset in order to stop sending
 * any more messages to the device. When the reset is complete, the message
 * queues are unthrottled. This is done to make sure that no messages generated
 * before the reset will be sent to the device, also after the reset completes.
 */
void nnp_msched_throttle_all(struct nnp_msched *sched);

/**
 * nnp_msched_queue_create() - create a queue of messages handled by scheduler
 * @scheduler: the msg scheduler object
 *
 * Return: pointer to msg scheduler queue object, NULL on failure.
 */
struct nnp_msched_queue *nnp_msched_queue_create(struct nnp_msched *scheduler);

/**
 * nnp_msched_queue_destroy() - destroy a message queue object
 * @queue: the message queue object to be destroyed.
 *
 * This function destroys a message queue object, if the queue is not empty
 * and still contains messages, the messages will be discarded and not sent to
 * the device.
 *
 * Return: 0 on success.
 */
int nnp_msched_queue_destroy(struct nnp_msched_queue *queue);

/**
 * nnp_msched_queue_sync() - wait for message queue to be empty
 * @queue: the message queue object
 *
 * Return: 0 on success, error value otherwise.
 */
int nnp_msched_queue_sync(struct nnp_msched_queue *queue);

/**
 * nnp_msched_queue_add_msg() - adds a message packet to a message queue
 * @queue: the message queue object
 * @msg: pointer to message content
 * @size: size of message in 64-bit units
 *
 * This function adds a message to the queue. The message will be sent
 * once the scheduler thread drains it from the queue.
 *
 * Return: 0 on success, error value otherwise
 */
int nnp_msched_queue_add_msg(struct nnp_msched_queue *queue, u64 *msg,
			     unsigned int size);

/*
 * Utility macro for calling nnp_msched_queue_add_msg by passing u64 array
 * object which forms the message.
 */
#define nnp_msched_queue_msg(q, m) \
	nnp_msched_queue_add_msg((q), (u64 *)&(m), sizeof((m)) / sizeof(u64))

#endif /* _NNP_MSGF_SCHEDULER_H */
