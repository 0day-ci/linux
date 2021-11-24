/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_session.h
 * session module header file
 */

#ifndef _ZYNPU_SESSION_H_
#define _ZYNPU_SESSION_H_

#include <linux/list.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/wait.h>
#include "zynpu_job_manager.h"

enum zynpu_mm_data_type {
	ZYNPU_MM_DATA_TYPE_NONE,
	ZYNPU_MM_DATA_TYPE_TEXT,
	ZYNPU_MM_DATA_TYPE_RO_STACK,
	ZYNPU_MM_DATA_TYPE_STATIC,
	ZYNPU_MM_DATA_TYPE_REUSE,
};

struct buf_desc {
	__u64 pa;
	__u64 dev_offset; /* user space access this area via mapping this offset from the dev file start */
	__u64 bytes;
	__u32 region_id;
};

struct buf_request {
	__u64 bytes;	  /* bytes requested to allocate */
	__u32 align_in_page;  /* alignment requirements (in 4KB) */
	__u32 data_type;      /* type of data in the buffer to allocate */
	__u32 region_id;      /* region ID specified (if applicable) */
	__u32 alloc_flag;     /* Allocation flag: default, strict or compact */
	struct buf_desc desc; /* info of buffer successfully allocated */
	__u32 errcode;
};

struct zynpu_buffer {
	u64 pa;
	void *va;
	u64 bytes;
	u32 region_id;
	u32 type;
};

/**
 * struct waitqueue: maintain the waitqueue for a user thread
 *
 * @uthread_id: user thread owns this waitqueue
 * @ref_cnt: strucr reference count
 * @p_wait: wait queue head for polling
 * @node: list head struct
 */
struct zynpu_thread_wait_queue {
	int uthread_id;
	int ref_cnt;
	wait_queue_head_t p_wait;
	struct list_head node;
};

/**
 * struct session_buf: session private buffer list
 * @desc: buffer descriptor struct
 * @dev_offset: offset of this buffer in device file
 * @type: buffer type: CMA/SRAM/RESERVED
 * @map_num: memory mmapped number
 * @head: list head struct
 */
struct session_buf {
	struct zynpu_buffer desc;
	u64 dev_offset;
	u32 type;
	int map_num;
	struct list_head head;
};

/**
 * struct session_job: session private job list
 * @uthread_id: ID of user thread scheduled this job
 * @desc: job descriptor struct
 * @state: job state
 * @exception_type: type of exception if any
 * @pdata: profiling data struct
 * @head: list head struct
 */
struct session_job {
	int uthread_id;
	struct user_job_desc desc;
	int state;
	int exception_type;
	struct profiling_data pdata;
	struct list_head head;
};

/**
 * struct zynpu_session: private data struct for every file open operation
 * @user_pid: ID of the user thread doing the open operation
 * @sbuf_list: successfully allocated shared buffer of this session
 * @sbuf_lock: mutex lock for sbuf list
 * @job_list: job list of this session
 * @job_lock: spinlock for job list
 * @zynpu_priv: zynpu_priv struct pointer
 * @wait_queue_head: thread waitqueue list head of this session
 * @com_wait: session common waitqueue head
 * @single_thread_poll: flag to indicate the polling method, thread vs. fd
 */
struct zynpu_session {
	int user_pid;
	struct session_buf sbuf_list;
	struct mutex sbuf_lock;
	struct session_job job_list;
	spinlock_t job_lock;
	void *zynpu_priv;
	struct zynpu_thread_wait_queue *wait_queue_head;
	wait_queue_head_t com_wait;
	int single_thread_poll;
};

/*
 * @brief create unique session DS for an open request
 *
 * @param pid: user mode thread pid
 * @param zynpu: zynpu_priv struct pointer
 * @param p_session: session struct pointer
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_create_session(int pid, void *zynpu_priv,
	struct zynpu_session **p_session);
/*
 * @brief destroy an existing session
 *
 * @param session: session pointer
 *
 * @return ZYNPU_KMD_ERR_OK if successful; others if failed.
 */
int zynpu_destroy_session(struct zynpu_session *session);
/*
 * @brief get pid of this session
 *
 * @param session: session pointer
 *
 * @return id if successful; 0 if failed.
 */
int zynpu_get_session_pid(struct zynpu_session *session);
/*
 * @brief add an allocated buffer of this session
 *
 * @param session: session pointer
 * @param buf_req: request buffer struct pointer
 * @param buf: buffer allocated
 *
 * @return ZYNPU_KMD_ERR_OK if successful; others if failed.
 */
int zynpu_session_add_buf(struct zynpu_session *session, struct buf_request *buf_req,
	struct zynpu_buffer *buf);
/*
 * @brief remove an allocated buffer of this session
 *
 * @param session: session pointer
 * @param buf: buffer to be removed
 *
 * @return ZYNPU_KMD_ERR_OK if successful; others if failed.
 */
int zynpu_session_detach_buf(struct zynpu_session *session, struct buf_desc *buf);
/*
 * @brief mmap an allocated buffer of this session
 *
 * @param session: session pointer
 * @param vma: vm_area_struct
 * @param dev: device struct
 *
 * @return ZYNPU_KMD_ERR_OK if successful; others if failed.
 */
int zynpu_session_mmap_buf(struct zynpu_session *session, struct vm_area_struct *vma, struct device *dev);
/*
 * @brief get first valid buffer descriptor of this session
 *
 * @param session: session pointer
 *
 * @return buffer if successful; NULL if failed.
 */
struct zynpu_buffer * zynpu_get_session_sbuf_head(struct zynpu_session *session);
/*
 * @brief add a job descriptor of this session
 *
 * @param session: session pointer
 * @param user_job: userspace job descriptor pointer
 *
 * @return non-NULL kernel job ptr if successful; NULL if failed.
 */
struct session_job * zynpu_session_add_job(struct zynpu_session *session, struct user_job *user_job);
/*
 * @brief delete all jobs of a session
 *
 * @param session: session pointer
 *
 * @return ZYNPU_KMD_ERR_OK if successful; others if failed.
 */
int zynpu_session_delete_jobs(struct zynpu_session *session);
/*
 * @brief job done interrupt bottom half handler
 *
 * @param session: session pointer
 * @param job: session job pointer
 * @param excep_flag: exception flag
 *
 */
void zynpu_session_job_done(struct zynpu_session *session, struct session_job *job,
	int excep_flag);
/*
 * @brief update bandwidth profiling data after job done
 *
 * @param session: session pointer
 * @param job: session job pointer
 *
 */
//void zynpu_session_job_update_pdata(struct zynpu_session *session, struct session_job *job);
/*
 * @brief check if any scheduled job of the specified thread is done/exception
 *
 * @param session: session pointer
 * @param uthread_id: user thread ID
 *
 * @return 1 if has don job(s); 0 if no.
 */
int zynpu_session_thread_has_end_job(struct zynpu_session *session, int uthread_id);
/*
 * @brief get one or multiple end jobs' status
 *
 * @param session: session pointer
 * @param job_status: job status query struct
 *
 * @return 1 if has don job(s); 0 if no.
 */
int zynpu_session_get_job_status(struct zynpu_session *session, struct job_status_query *job_status);
/*
 * @brief add waitqueue into session thread waitqueue list
 *
 * @param session: session pointer
 * @param filp: file struct from file operation API
 * @param wait: wait struct from poll file operation API
 * @param uthread_id: user thread ID
 *
 */
void zynpu_session_add_poll_wait_queue(struct zynpu_session *session,
    struct file *filp, struct poll_table_struct *wait, int uthread_id);
/*
 * @brief mark the scheduled time of a job
 *
 * @param job: session job pointer
 */
void session_job_mark_sched(struct session_job *job);
/*
 * @brief mark the done time of a job
 *
 * @param job: session job pointer
 */
void session_job_mark_done(struct session_job *job);
/*
 * @brief check if a job is enabled to do profiling
 *
 * @param job: session job pointer
 */
int is_session_job_prof_enabled(struct session_job *job);

#endif //_ZYNPU_SESSION_H_