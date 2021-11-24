/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_job_manager.h
 * Job manager module header file
 */

#ifndef _ZYNPU_JOB_MANAGER_H_
#define _ZYNPU_JOB_MANAGER_H_

#include <linux/list.h>
#include <linux/spinlock.h>

#define ZYNPU_EXCEP_NO_EXCEPTION   0
#ifdef __KERNEL__

#define ZYNPU_JOB_STATE_IDLE      0
#define ZYNPU_JOB_STATE_PENDING   1
#define ZYNPU_JOB_STATE_SCHED     2
#define ZYNPU_JOB_STATE_END       3

#define ZYNPU_JOB_FLAG_INVALID    0
#define ZYNPU_JOB_FLAG_VALID      1
#endif

#define ZYNPU_JOB_STATE_DONE      0x1
#define ZYNPU_JOB_STATE_EXCEPTION 0x2

struct profiling_data {
	ktime_t sched_kt;
	ktime_t done_kt;
};

struct job_status_desc {
	__u32 job_id;
	__u32 thread_id;
	__u32 state;
	struct profiling_data pdata;
};

struct job_status_query {
	__u32 max_cnt;
	__u32 get_single_job;
	__u32 job_id;
	struct job_status_desc *status;
	__u32 poll_cnt;
	__u32 errcode;
};

struct user_job_desc {
	__u64 start_pc_addr;
	__u64 intr_handler_addr;
	__u64 data_0_addr;
	__u64 data_1_addr;
	__u64 static_addr;
	__u64 reuse_addr;
	__u32 job_id;
	__u32 code_size;
	__u32 rodata_size;
	__u32 stack_size;
	__u32 static_size;
	__u32 reuse_size;
	__u32 enable_prof;
	__u32 enable_asid;
};

struct user_job {
	struct user_job_desc desc;
	__u32 errcode;
};

/**
 * struct zynpu_job - job element struct describing a job under scheduling in job manager
 *	Job status will be tracked as soon as interrupt or user evenets come in.
 *
 * @uthread_id: ID of user thread scheduled this job
 * @desc: job desctiptor from userland
 * @session: session pointer refernece of this job
 * @session_job: corresponding job object in session
 * @state: job state
 * @exception_flag: exception flag
 * @valid_flag: valid flag, indicating this job canceled by user or not
 * @node: list head struct
 */
 struct zynpu_job {
	int uthread_id;
	struct user_job_desc desc;
	struct zynpu_session *session;
	struct session_job *session_job;
	int state;
	int exception_flag;
	int valid_flag;
	struct list_head node;
};

/**
 * struct zynpu_job_manager - job manager
 *	Maintain all jobs and update their status
 *
 * @scheduled_queue_head: scheduled job queue head
 * @pending_queue_head: pending job queue head
 * @sched_num: number of jobs have been scheduled
 * @max_sched_num: maximum allowed scheduled job number
 * @lock: spinlock
 * @dev: device struct pointer
 */
struct zynpu_job_manager {
	struct zynpu_job *scheduled_queue_head;
	struct zynpu_job *pending_queue_head;
	int sched_num;
	int max_sched_num;
	int init_done;
	spinlock_t lock;
	struct device *dev;
};

/**
 * @brief initialize an existing job manager struct during driver probe phase
 *
 * @param job_manager: job_manager struct pointer allocated from user;
 * @param p_dev: zynpu device struct pointer
 * @param max_sched_num: maximum allowed scheduled job number;
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_init_job_manager(struct zynpu_job_manager *job_manager, struct device *p_dev, int max_sched_num);
/**
 * @brief de-init job manager
 *
 * @param job_manager: job_manager struct pointer allocated from user;
 *
 * @return void
 */
void zynpu_deinit_job_manager(struct zynpu_job_manager *job_manager);
/**
 * @brief schedule new job flushed from userland
 *
 * @param job_manager: job_manager struct pointer;
 * @param user_job: user_job struct;
 * @param kern_job: session job;
 * @param session: session pointer refernece of this job;
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_job_manager_schedule_new_job(struct zynpu_job_manager *job_manager, struct user_job *user_job,
    struct session_job *kern_job, struct zynpu_session *session);
/**
 * @brief update job state and indicating if exception happens
 *
 * @param zynpu_priv: zynpu private struct
 * @param exception_flag: exception flag
 *
 * @return void
 */
void zynpu_job_manager_update_job_state_irq(void *zynpu_priv, int exception_flag);
/**
 * @brief done interrupt handler for job manager
 *
 * @param job_manager: job_manager struct pointer;
 *
 * @return void
 */
void zynpu_job_manager_update_job_queue_done_irq(struct zynpu_job_manager *job_manager);
/**
 * @brief cancel all jobs flushed by a user thread
 *
 * @param job_manager: job_manager struct pointer allocated from user;
 * @param session: session serviced for that user thread
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_job_manager_cancel_session_jobs(struct zynpu_job_manager *job_manager,
	struct zynpu_session *session);
/**
 * @brief invalidate/kill a timeout job
 *
 * @param job_manager: job_manager struct pointer allocated from user;
 * @param job_id: job ID
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_invalidate_timeout_job(struct zynpu_job_manager *job_manager, int job_id);
/**
 * @brief show KMD job info via sysfs
 *
 * @param job_manager: job_manager struct pointer allocated from user;
 * @param buf: userspace buffer for KMD to fill the job info
 *
 * @return buf written bytes number;
 */
int zynpu_job_manager_sysfs_job_show(struct zynpu_job_manager *job_manager, char *buf);

#endif /* _ZYNPU_JOB_MANAGER_H_ */