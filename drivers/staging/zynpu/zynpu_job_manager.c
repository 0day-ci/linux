// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_job_manager.c
 * Job manager module implementation file
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include "zynpu_job_manager.h"
#include "zynpu.h"

static int init_zynpu_job(struct zynpu_job *zynpu_job, struct user_job_desc *desc,
	struct session_job *kern_job, struct zynpu_session *session)
{
	int ret = 0;

	if (!zynpu_job) {
		ret = -EINVAL;
		goto finish;
	}

	if (kern_job)
		zynpu_job->uthread_id = kern_job->uthread_id;

	if (!desc)
		memset(&zynpu_job->desc, 0, sizeof(struct user_job_desc));
	else
		zynpu_job->desc = *desc;
	zynpu_job->session = (struct zynpu_session *)session;
	zynpu_job->session_job = (struct session_job *)kern_job;
	zynpu_job->state = ZYNPU_JOB_STATE_IDLE;
	zynpu_job->exception_flag = ZYNPU_EXCEP_NO_EXCEPTION;
	zynpu_job->valid_flag = ZYNPU_JOB_FLAG_VALID;
	INIT_LIST_HEAD(&zynpu_job->node);

finish:
	return ret;
}

static void destroy_zynpu_job(struct zynpu_job *job)
{
	if (job)
		kfree(job);
}

static void remove_zynpu_job(struct zynpu_job *job)
{
	if (job) {
		list_del(&job->node);
		destroy_zynpu_job(job);
	}

}

static struct zynpu_job *create_zynpu_job(struct user_job_desc *desc,
	struct session_job *kern_job, struct zynpu_session *session)
{
	struct zynpu_job *new_zynpu_job = NULL;

	new_zynpu_job = kzalloc(sizeof(struct zynpu_job), GFP_KERNEL);
	if (init_zynpu_job(new_zynpu_job, desc, kern_job, session) != 0) {
		destroy_zynpu_job(new_zynpu_job);
		new_zynpu_job = NULL;
	}

	return new_zynpu_job;
}

static void zynpu_job_manager_trigger_job_sched(struct zynpu_priv *zynpu, struct zynpu_job *zynpu_job)
{
	if (zynpu && zynpu_job) {
		zynpu_priv_trigger(zynpu, &zynpu_job->desc, zynpu_job->uthread_id);
		if (is_session_job_prof_enabled(zynpu_job->session_job))
			session_job_mark_sched(zynpu_job->session_job);
	}
}


int zynpu_init_job_manager(struct zynpu_job_manager *job_manager, struct device *p_dev, int max_sched_num)
{
	int ret = 0;

	if ((!job_manager) || (!p_dev))
		return -EINVAL;

	if (job_manager->init_done)
		return 0;

	job_manager->scheduled_queue_head = create_zynpu_job(NULL, NULL, NULL);
	job_manager->pending_queue_head = create_zynpu_job(NULL, NULL, NULL);
	if ((!job_manager->pending_queue_head) || (!job_manager->scheduled_queue_head))
		return -ENOMEM;

	job_manager->sched_num = 0;
	job_manager->max_sched_num = max_sched_num;
	spin_lock_init(&job_manager->lock);
	job_manager->dev = p_dev;
	job_manager->init_done = 1;

	return ret;
}

static void delete_queue(struct zynpu_job *head)
{
	struct zynpu_job *cursor = head;
	struct zynpu_job *next = NULL;

	if (head) {
		list_for_each_entry_safe(cursor, next, &head->node, node) {
			remove_zynpu_job(cursor);
		}
	}
}

void zynpu_deinit_job_manager(struct zynpu_job_manager *job_manager)
{
	if (job_manager) {
		delete_queue(job_manager->scheduled_queue_head);
		delete_queue(job_manager->pending_queue_head);
		job_manager->sched_num = 0;
	}
}

static void zynpu_schedule_pending_job_no_lock(struct zynpu_job_manager *job_manager)
{
	struct zynpu_job *curr = NULL;
	struct zynpu_priv *zynpu = container_of(job_manager, struct zynpu_priv, job_manager);

	if (!job_manager) {
		dev_err(job_manager->dev, "invalid input args user_job or kern_job or session to be NULL!");
		return;
	}

	/* 1st pending job should be scheduled if any */
	if ((!list_empty(&job_manager->pending_queue_head->node)) &&
	    (job_manager->sched_num < job_manager->max_sched_num) &&
	    (zynpu_priv_is_idle(zynpu))) {
		/*
		  detach head of pending queue and add it to the tail of scheduled job queue

				      |--->>------->>---|
				      |(real head)      |(tail)
		  --------------------------------    ----------------------------------
		  | j <=> j <=> j <=> j <=> head |    | [empty to fill] <=> j <=> head |
		  --------------------------------    ----------------------------------
			  pending job queue		   scheduled job queue
		*/
		curr = list_next_entry(job_manager->pending_queue_head, node);

		zynpu_job_manager_trigger_job_sched(zynpu, curr);
		curr->state = ZYNPU_JOB_STATE_SCHED;
		list_move_tail(&curr->node, &job_manager->scheduled_queue_head->node);
		job_manager->sched_num++;
	} else {
		/**
		 * do nothing because no pending job needs to be scheduled
		 * or ZYNPU is not available to accept more jobs
		 */
		if (list_empty(&job_manager->pending_queue_head->node)) {
			if (!task_pid_nr(current))
				dev_dbg(job_manager->dev, "[IRQ] no pending job to trigger");
			else
				dev_dbg(job_manager->dev, "[%u] no pending job to trigger", task_pid_nr(current));
		}

		if (job_manager->sched_num >= job_manager->max_sched_num) {
			if (!task_pid_nr(current))
				dev_dbg(job_manager->dev, "[IRQ] ZYNPU busy and do not trigger");
			else
				dev_dbg(job_manager->dev, "[%u] ZYNPU busy and do not trigger", task_pid_nr(current));
		}

	}
}

int zynpu_job_manager_schedule_new_job(struct zynpu_job_manager *job_manager, struct user_job *user_job,
	struct session_job *session_job, struct zynpu_session *session)
{
	int ret = 0;
	struct zynpu_job *zynpu_job = NULL;

	if ((!job_manager) || (!user_job) || (!session_job) || (!session)) {
		if (user_job)
			user_job->errcode = ZYNPU_ERRCODE_INTERNAL_NULLPTR;
		ret = -EINVAL;
		goto finish;
	}

	zynpu_job = create_zynpu_job(&user_job->desc, session_job, session);
	if (!zynpu_job) {
		user_job->errcode = ZYNPU_ERRCODE_CREATE_KOBJ_ERR;
		ret = -EFAULT;
		goto finish;
	}

	/* LOCK */
	spin_lock_irq(&job_manager->lock);

	/* pending the flushed job from userland and try to schedule it */
	zynpu_job->state = ZYNPU_JOB_STATE_PENDING;
	list_add_tail(&zynpu_job->node, &job_manager->pending_queue_head->node);
	zynpu_schedule_pending_job_no_lock(job_manager);

	spin_unlock_irq(&job_manager->lock);
	/* UNLOCK */

	/* success */
	user_job->errcode = 0;

finish:
	return ret;
}

static int zynpu_invalidate_job_no_lock(struct zynpu_job_manager *job_manager,
	struct zynpu_job *job)
{
	//struct zynpu_priv *zynpu = container_of(job_manager, struct zynpu_priv, job_manager);

	if ((!job_manager) || (!job))
		return -EINVAL;

	if (job->state == ZYNPU_JOB_STATE_SCHED) {
			job->valid_flag = 0;
	} else if (job->state == ZYNPU_JOB_STATE_PENDING) {
		remove_zynpu_job(job);
	} else
		return -EINVAL;

	return 0;
}

static void zynpu_invalidate_canceled_jobs_no_lock(struct zynpu_job_manager *job_manager,
	struct zynpu_job *head, struct zynpu_session *session)
{
	struct zynpu_job *cursor = NULL;
	struct zynpu_job *next = NULL;

	if ((!job_manager) || (!head) || (!session))
		return;

	list_for_each_entry_safe(cursor, next, &head->node, node) {
		if (zynpu_get_session_pid(cursor->session) == zynpu_get_session_pid(session))
			zynpu_invalidate_job_no_lock(job_manager, cursor);
	}
}

int zynpu_job_manager_cancel_session_jobs(struct zynpu_job_manager *job_manager,
	struct zynpu_session *session)
{
	int ret = 0;

	if (!session) {
		ret = -EINVAL;
		goto finish;
	}

	/* LOCK */
	spin_lock_irq(&job_manager->lock);

	/**
	 * invalidate all active jobs of this session in job manager
	 */
	zynpu_invalidate_canceled_jobs_no_lock(job_manager, job_manager->pending_queue_head, session);
	zynpu_invalidate_canceled_jobs_no_lock(job_manager, job_manager->scheduled_queue_head, session);

	spin_unlock_irq(&job_manager->lock);
	/* UNLOCK */

	/* delete all session_job */
	zynpu_session_delete_jobs(session);

finish:
	return ret;
}

static int zynpu_invalidate_timeout_job_no_lock(struct zynpu_job_manager *job_manager,
	struct zynpu_job *head, int job_id)
{
	int ret = -EINVAL;
	struct zynpu_job *cursor = NULL;
	struct zynpu_job *next = NULL;

	if ((!job_manager) || (!head))
		return -EINVAL;

	list_for_each_entry_safe(cursor, next, &head->node, node) {
		if ((cursor->uthread_id == task_pid_nr(current)) &&
			(cursor->desc.job_id == job_id)) {
			ret = zynpu_invalidate_job_no_lock(job_manager, cursor);
			break;
		}
	}

	return ret;
}

int zynpu_invalidate_timeout_job(struct zynpu_job_manager *job_manager, int job_id)
{
	int ret = 0;

	if (!job_manager)
		return -EINVAL;

	/* LOCK */
	spin_lock_irq(&job_manager->lock);
	ret = zynpu_invalidate_timeout_job_no_lock(job_manager, job_manager->pending_queue_head, job_id);
	if (ret) {
		ret = zynpu_invalidate_timeout_job_no_lock(job_manager, job_manager->scheduled_queue_head, job_id);
		pr_debug("Timeout job invalidated from sched queue.");
	} else {
		pr_debug("Timeout job invalidated from pending queue.");
	}

	spin_unlock_irq(&job_manager->lock);
	/* UNLOCK */

	return ret;
}

void zynpu_job_manager_update_job_state_irq(void *zynpu_priv, int exception_flag)
{
	struct zynpu_job *curr = NULL;
	struct zynpu_priv *zynpu = (struct zynpu_priv *)zynpu_priv;
	struct zynpu_job_manager *job_manager = &zynpu->job_manager;

	/* LOCK */
	spin_lock(&job_manager->lock);
	list_for_each_entry(curr, &job_manager->scheduled_queue_head->node, node) {
		if (curr->state == ZYNPU_JOB_STATE_SCHED) {
			curr->state = ZYNPU_JOB_STATE_END;
			curr->exception_flag = exception_flag;

			if (curr->exception_flag)
				pr_debug("[IRQ] job 0x%x of thread %u EXCEPTION",
					curr->desc.job_id, curr->uthread_id);
			else
				pr_debug("[IRQ] job 0x%x of thread %u DONE",
					curr->desc.job_id, curr->uthread_id);


			if (is_session_job_prof_enabled(curr->session_job))
				session_job_mark_done(curr->session_job);

			if (job_manager->sched_num)
				job_manager->sched_num--;
			break;
		}
	}

	/* schedule a new pending job */
	zynpu_schedule_pending_job_no_lock(job_manager);
	spin_unlock(&job_manager->lock);
	/* UNLOCK */
}

void zynpu_job_manager_update_job_queue_done_irq(struct zynpu_job_manager *job_manager)
{
	struct zynpu_job *curr = NULL;
	struct zynpu_job *next = NULL;

	/* LOCK */
	spin_lock(&job_manager->lock);
	list_for_each_entry_safe(curr, next, &job_manager->scheduled_queue_head->node, node) {
		if (ZYNPU_JOB_STATE_END != curr->state)
			continue;

		/*
		   DO NOT call session API for invalid job because
		   session struct probably not exist on this occasion
		*/
		if (ZYNPU_JOB_FLAG_VALID == curr->valid_flag) {
			pr_debug("[BH] handling job 0x%x of thread %u...",
				curr->desc.job_id, curr->uthread_id);
			zynpu_session_job_done(curr->session, curr->session_job,
			    curr->exception_flag);
		} else {
			pr_debug("[BH] this done job has been cancelled by user.");
		}

		list_del(&curr->node);
		destroy_zynpu_job(curr);
		curr = NULL;
		/* DO NOT minus sched_num here because upper half has done that */
	}
	spin_unlock(&job_manager->lock);
	/* UNLOCK */
}

static int print_job_info(char *buf, int buf_size, struct zynpu_job *job)
{
	int ret = 0;
	char state_str[20];
	char excep_str[10];

	if ((!buf) || (!job))
		return ret;

	if (job->state == ZYNPU_JOB_STATE_PENDING)
		snprintf(state_str, 20, "Pending");
	else if (job->state == ZYNPU_JOB_STATE_SCHED)
		snprintf(state_str, 20, "Executing");
	else if (job->state == ZYNPU_JOB_STATE_END)
		snprintf(state_str, 20, "Done");

	if (job->exception_flag)
		snprintf(excep_str, 10, "Y");
	else
		snprintf(excep_str, 10, "N");

	return snprintf(buf, buf_size, "%-*d0x%-*x%-*s%-*s\n", 12, job->uthread_id, 10,
		job->desc.job_id, 10, state_str, 5, excep_str);
}

int zynpu_job_manager_sysfs_job_show(struct zynpu_job_manager *job_manager, char *buf)
{
	int ret = 0;
	int tmp_size = 1024;
	char tmp[1024];
	struct zynpu_job *curr = NULL;
	int number = 0;

	if (!buf)
		return ret;

	ret += snprintf(tmp, 1024, "-------------------------------------------\n");
	strcat(buf, tmp);
	ret += snprintf(tmp, 1024, "%-*s%-*s%-*s%-*s\n", 12, "Thread ID", 12, "Job ID",
		10, "State", 5, "Exception");
	strcat(buf, tmp);
	ret += snprintf(tmp, 1024, "-------------------------------------------\n");
	strcat(buf, tmp);

	/* LOCK */
	spin_lock_irq(&job_manager->lock);
	list_for_each_entry(curr, &job_manager->pending_queue_head->node, node) {
		ret += print_job_info(tmp, tmp_size, curr);
		strcat(buf, tmp);
		number++;
	}
	curr = NULL;
	list_for_each_entry(curr, &job_manager->scheduled_queue_head->node, node) {
		ret += print_job_info(tmp, tmp_size, curr);
		strcat(buf, tmp);
		number++;
	}
	spin_unlock_irq(&job_manager->lock);
	/* UNLOCK */

	if (!number) {
		ret += snprintf(tmp, tmp_size, "No job.\n");
		strcat(buf, tmp);
	}

	ret += snprintf(tmp, 1024, "-------------------------------------------\n");
	strcat(buf, tmp);

	return ret;
}