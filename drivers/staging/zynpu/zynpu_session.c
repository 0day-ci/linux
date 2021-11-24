// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_session.c
 * Implementation of session module
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/string.h>
#include "zynpu_session.h"
#include "zynpu_mm.h"
#include "zynpu.h"

static void init_session_buf(struct session_buf *buf,
	struct zynpu_buffer *desc, u64 dev_offset)
{
	if (buf) {
		if (!desc)
			memset(&buf->desc, 0, sizeof(struct zynpu_buffer));
		else {
			buf->desc = *desc;
			buf->type = desc->type;
		}
		buf->dev_offset = dev_offset;
		buf->map_num = 0;
		INIT_LIST_HEAD(&buf->head);
	}
}

static struct session_buf *create_session_buf(struct zynpu_buffer *desc,
	u64 dev_offset)
{
	struct session_buf *sbuf = NULL;

	if (!desc) {
		pr_err("descriptor is needed while creating new session buf!");
		goto finish;
	}

	sbuf = kzalloc(sizeof(struct session_buf), GFP_KERNEL);
	init_session_buf(sbuf, desc, dev_offset);

finish:
	return sbuf;
}

static int destroy_session_buffer(struct session_buf *buf)
{
	int ret = 0;

	if (buf)
		kfree(buf);
	else {
		pr_err("invalid null buf args or list not empty!");
		ret = -EINVAL;
	}

	return ret;
}

static void init_session_job(struct session_job *job, struct user_job_desc *desc)
{
    if (job) {
	    /* init job->desc to be all 0 if desc == NULL */
	    job->uthread_id = task_pid_nr(current);
	    if (!desc)
		    memset(&job->desc, 0, sizeof(struct user_job_desc));
	    else
		    job->desc = *desc;
	    job->state = 0;
	    job->exception_type = ZYNPU_EXCEP_NO_EXCEPTION;
	    INIT_LIST_HEAD(&job->head);
    }
}

static struct session_job *create_session_job(struct user_job_desc *desc)
{
	struct session_job *new_job = NULL;

	if (!desc) {
		pr_err("descriptor is needed while creating new session job!");
		goto finish;
	}

	new_job = kzalloc(sizeof(struct session_job), GFP_KERNEL);
	init_session_job(new_job, desc);

finish:
	return new_job;
}

static int destroy_session_job(struct session_job *job)
{
	int ret = 0;

	if (job)
		kfree(job);
	else {
		pr_err("invalid null job args or list not empty!");
		ret = -EINVAL;
	}

	return ret;
}

static int is_session_all_jobs_end(struct zynpu_session *session)
{
	return (!session) ? 1 : list_empty(&session->job_list.head);
}

static int is_session_all_buffers_freed(struct zynpu_session *session)
{
	return (!session) ? 1 : list_empty(&session->sbuf_list.head);
}

static struct session_buf *find_buffer_bydesc_no_lock(struct zynpu_session *session,
	struct buf_desc *buf_desc)
{
	struct session_buf *target_buf  = NULL;
	struct session_buf *session_buf = NULL;
	struct list_head *node = NULL;

	if ((!session) || (!buf_desc)) {
		pr_err("invalid input session or buf_desc args to be null!");
		goto finish;
	}

	list_for_each(node, &session->sbuf_list.head) {
		session_buf = list_entry(node, struct session_buf, head);

		if (session_buf &&
		    (session_buf->desc.pa == buf_desc->pa) &&
		    (session_buf->desc.bytes == buf_desc->bytes)) {
			target_buf = session_buf;
			pr_info("found matching buffer to be deleted.");
			goto finish;
		}
	}

finish:
	return target_buf;
}

static struct session_buf *find_buffer_byoffset_no_lock(struct zynpu_session *session,
    u64 offset, int len)
{
	struct session_buf *target_buf = NULL;
	struct session_buf *session_buf = NULL;
	struct list_head *node = NULL;

	if (!session) {
		pr_err("invalid input session args to be null!");
		goto finish;
	}

	list_for_each(node, &session->sbuf_list.head) {
		session_buf = list_entry(node, struct session_buf, head);
		if (session_buf &&
		    (session_buf->dev_offset == offset) &&
		    (len <= session_buf->desc.bytes)) {
			target_buf = session_buf;
			goto finish;
		}
	}

finish:
	return target_buf;
}

/*
 * @brief get requested waitqueue for a user thread
 *
 * @param head: wait queue head
 * @uthread_id: user thread ID
 *
 * @return waitqueue pointer; NULL if not found;
 */
static struct zynpu_thread_wait_queue*
get_thread_wait_queue_no_lock(struct zynpu_thread_wait_queue *head, int uthread_id)
{
	struct zynpu_thread_wait_queue *curr = NULL;

	if (!head)
		return NULL;

	list_for_each_entry(curr, &head->node, node) {
		if (curr->uthread_id == uthread_id)
			return curr;
	}
	return NULL;
}

/*
 * @brief create a new waitqueue for a user thread if there is no existing one
 *
 * @param head: wait queue head
 * @uthread_id: user thread ID
 *
 * @return waitqueue pointer if not existing one; NULL if there has been an existing one;
 */
static struct zynpu_thread_wait_queue *
create_thread_wait_queue_no_lock(struct zynpu_thread_wait_queue *head, int uthread_id)
{
	struct zynpu_thread_wait_queue *ret = NULL;
	struct zynpu_thread_wait_queue *queue = get_thread_wait_queue_no_lock(head, uthread_id);

	/* new thread wait queue */
	if (!queue) {
		queue = kzalloc(sizeof(struct zynpu_thread_wait_queue), GFP_KERNEL);
		queue->uthread_id = uthread_id;
		init_waitqueue_head(&queue->p_wait);
		INIT_LIST_HEAD(&queue->node);

		if (queue && head) {
			list_add_tail(&queue->node, &head->node);
		}
		ret = queue;
	}

	queue->ref_cnt++;
	return ret;
}

/********************************************************************************
 *  The following APIs are called in thread context for session obj management  *
 *  and member query service						    *
 *  -- zynpu_create_session						      *
 *  -- zynpu_destroy_session						     *
 *  -- zynpu_get_session_pid						     *
 ********************************************************************************/
int zynpu_create_session(int pid, void *zynpu_priv,
	struct zynpu_session **p_session)
{
	int ret = 0;
	struct zynpu_session *session = NULL;
	struct device *dev = NULL;

	if ((!zynpu_priv) || (!p_session)) {
		pr_err("invalid input session or common args to be null!");
		goto finish;
	}

	dev = ((struct zynpu_priv *)zynpu_priv)->dev;

	session = kzalloc(sizeof(struct zynpu_session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->user_pid = pid;
	init_session_buf(&session->sbuf_list, NULL, 0);
	mutex_init(&session->sbuf_lock);
	init_session_job(&session->job_list, NULL);
	spin_lock_init(&session->job_lock);
	session->zynpu_priv = zynpu_priv;
	session->wait_queue_head = create_thread_wait_queue_no_lock(NULL, 0);
	init_waitqueue_head(&session->com_wait);
	session->single_thread_poll = 0;

	*p_session = session;
	dev_dbg(dev, "[%d] new session created\n", pid);

finish:
	return ret;
}

/*
 * @brief delete a waitqueue list
 *
 * @param head: wait queue head
 *
 */
static void delete_wait_queue(struct zynpu_thread_wait_queue *wait_queue_head)
{
	struct zynpu_thread_wait_queue *cursor = NULL;
	struct zynpu_thread_wait_queue *next = NULL;

	if (wait_queue_head) {
		list_for_each_entry_safe(cursor, next, &wait_queue_head->node, node) {
			list_del(&cursor->node);
			kfree(cursor);
		}
	}
}

int zynpu_destroy_session(struct zynpu_session *session)
{
	int ret = 0;
	struct device *dev = NULL;
	int pid = 0;

	if (session &&
	    is_session_all_jobs_end(session) &&
	    is_session_all_buffers_freed(session)) {
		dev = ((struct zynpu_priv *)session->zynpu_priv)->dev;
		pid = session->user_pid;
		delete_wait_queue(session->wait_queue_head);
		kfree(session->wait_queue_head);
		kfree(session);
		dev_dbg(dev, "[%d] session destroyed\n", pid);
	} else {
		pr_warn("invalid input session args to be null or invalid operation!");
		ret = -EINVAL;
	}

	return ret;
}

int zynpu_get_session_pid(struct zynpu_session *session)
{
	if (session)
		return session->user_pid;
	else {
		pr_warn("invalid input session args to be null!");
		return -EINVAL;
	}
}

/********************************************************************************
 *  The following APIs are called in thread context for servicing user space    *
 *  request in resource allocation/free and job scheduling via fops	     *
 *  -- zynpu_session_add_buf						     *
 *  -- zynpu_session_detach_buf						  *
 *  -- zynpu_get_session_sbuf_head					       *
 *  -- zynpu_session_mmap_buf						    *
 *  -- zynpu_session_add_job						     *
 *  -- zynpu_session_delete_jobs						 *
 ********************************************************************************/
int zynpu_session_add_buf(struct zynpu_session *session,
	struct buf_request *buf_req, struct zynpu_buffer *buf)
{
	int ret = 0;
	struct session_buf *new_sbuf = NULL;

	if ((!session) || (!buf_req) || (!buf)) {
		pr_err("invalid input session or buf_req or buf args to be null!");
		if (buf_req)
			buf_req->errcode = ZYNPU_ERRCODE_INTERNAL_NULLPTR;
		ret = -EINVAL;
		goto finish;
	}

	new_sbuf = create_session_buf(buf, buf->pa);
	if (!new_sbuf) {
		pr_err("create session buf failed!");
		buf_req->errcode = ZYNPU_ERRCODE_CREATE_KOBJ_ERR;
		ret = -EFAULT;
	} else {
		mutex_lock(&session->sbuf_lock);
		list_add(&new_sbuf->head, &session->sbuf_list.head);

		/* success */
		/* copy buffer descriptor to userland */
		buf_req->desc.pa = buf->pa;
		buf_req->desc.dev_offset = buf->pa;
		buf_req->desc.bytes = buf->bytes;
		buf_req->desc.region_id = buf->region_id;
		buf_req->errcode = 0;
		mutex_unlock(&session->sbuf_lock);
	}

finish:
	return ret;
}

int zynpu_session_detach_buf(struct zynpu_session *session, struct buf_desc *buf_desc)
{
	int ret = 0;
	struct session_buf *target_buf = NULL;

	if ((!session) || (!buf_desc)) {
		pr_err("invalid input session or buf args to be null!");
		ret = -EINVAL;
		goto finish;
	}

	/* LOCK */
	mutex_lock(&session->sbuf_lock);
	target_buf = find_buffer_bydesc_no_lock(session, buf_desc);
	if (!target_buf) {
		pr_err("no corresponding buffer found in this session!");
		ret = -ENOENT;
	} else {
		list_del(&target_buf->head);
		ret = destroy_session_buffer(target_buf);
		if (ret)
			pr_err("destroy session failed!");
		else
			target_buf = NULL;
	}
	mutex_unlock(&session->sbuf_lock);
	/* UNLOCK */

finish:
	return ret;
}

int zynpu_session_mmap_buf(struct zynpu_session *session, struct vm_area_struct *vma, struct device *dev)
{
	int ret = 0;
	u64 offset = 0;
	int len = 0;
	unsigned long vm_pgoff = 0;
	struct session_buf *buf = NULL;

	if ((!session) || (!vma)) {
		pr_err("invalid input session or vma args to be null!");
		ret = -EINVAL;
		goto finish;
	}

	offset = vma->vm_pgoff * PAGE_SIZE;
	len = vma->vm_end - vma->vm_start;

	/* LOCK */
	mutex_lock(&session->sbuf_lock);
	/* to find an allocated buffer with matching dev offset and length */
	buf = find_buffer_byoffset_no_lock(session, offset, len);
	if (!buf) {
		pr_err("invalid operation or args: no corresponding buffer found in this session!");
		ret = -ENOENT;
	} else {
		if (buf->map_num) {
			pr_err("duplicated mmap operations on identical buffer!");
			ret = -ENOTTY;
		} else {
			vm_pgoff = vma->vm_pgoff;
			vma->vm_pgoff = 0;
			vma->vm_flags |= VM_IO;
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

			if (buf->type == ZYNPU_MEM_TYPE_CMA) {
				ret = dma_mmap_coherent(dev, vma, buf->desc.va,
					(dma_addr_t)buf->desc.pa, buf->desc.bytes);
				if (ret)
					pr_err("CMA mmap to userspace failed!");
			} else if ((buf->type == ZYNPU_MEM_TYPE_SRAM) ||
				   (buf->type == ZYNPU_MEM_TYPE_RESERVED)) {
				ret = remap_pfn_range(vma, vma->vm_start, buf->desc.pa >> PAGE_SHIFT,
					vma->vm_end - vma->vm_start, vma->vm_page_prot);
				if (ret)
					pr_err("SRAM mmap to userspace failed!");
			}

			vma->vm_pgoff = vm_pgoff;
			if (!ret)
				buf->map_num++;
		}
	}
	mutex_unlock(&session->sbuf_lock);
	/* UNLOCK */

finish:
    return ret;
}

struct zynpu_buffer *zynpu_get_session_sbuf_head(struct zynpu_session *session)
{
	struct session_buf *session_buf = NULL;
	struct zynpu_buffer *buf_desc = NULL;
	struct list_head *node = NULL;

	if (!session) {
		pr_err("invalid input session or buf_req or buf args to be null!");
		goto finish;
	}

	list_for_each(node, &session->sbuf_list.head) {
		if (!list_empty(node)) {
			session_buf = list_entry(node, struct session_buf, head);
			buf_desc = &session_buf->desc;
			goto finish;
		}
	}

finish:
	return buf_desc;
}

struct session_job *zynpu_session_add_job(struct zynpu_session *session, struct user_job *user_job)
{
	struct session_job *kern_job = NULL;

	if ((!session) || (!user_job)) {
		pr_err("invalid input session or user_job args to be null!");
		if (NULL != user_job)
			user_job->errcode = ZYNPU_ERRCODE_INTERNAL_NULLPTR;
		goto finish;
	}

	kern_job = create_session_job(&user_job->desc);
	if (!kern_job) {
		pr_err("invalid input session or job args to be null!");
		user_job->errcode = ZYNPU_ERRCODE_CREATE_KOBJ_ERR;
	} else {
		/* THREAD LOCK */
		spin_lock_bh(&session->job_lock);
		list_add(&kern_job->head, &session->job_list.head);
		create_thread_wait_queue_no_lock(session->wait_queue_head, kern_job->uthread_id);
		spin_unlock_bh(&session->job_lock);
		/* THREAD UNLOCK */

		/* success */
		user_job->errcode = 0;
	}

finish:
	return kern_job;
}

int zynpu_session_delete_jobs(struct zynpu_session *session)
{
	int ret = 0;
	struct session_job *cursor = NULL;
	struct session_job *next = NULL;

	if (!session) {
		pr_err("invalid input session to be null!");
		ret = -EINVAL;
		goto finish;
	}

	/* THREAD LOCK */
	spin_lock_bh(&session->job_lock);
	list_for_each_entry_safe(cursor, next, &session->job_list.head, head) {
		list_del(&cursor->head);
		destroy_session_job(cursor);
	}
	spin_unlock_bh(&session->job_lock);
	/* THREAD UNLOCK */

finish:
	return ret;
}

/********************************************************************************
 *  The following APIs are called in interrupt context to update end job status *
 *  They will be called by IRQ handlers in job manager module		   *
 *  Note that param session and session_job passed by job manager is assumed    *
 *  to be valid and active (not cancelled by userland)			  *
 *  -- zynpu_session_job_done						    *
 *  -- zynpu_session_job_excep						   *
 *  -- zynpu_session_job_update_pdata					    *
 ********************************************************************************/
void zynpu_session_job_done(struct zynpu_session *session, struct session_job *job,
	int excep_flag)
{
	struct zynpu_thread_wait_queue *queue = NULL;
	wait_queue_head_t *thread_queue = NULL;

	if ((!session) || (!job)) {
		pr_err("invalid input session or job args to be null!");
		return;
	}

	if (ZYNPU_EXCEP_NO_EXCEPTION == excep_flag)
		pr_debug("Done interrupt received...");
	else
		pr_debug("Exception interrupt received...");

	/* IRQ LOCK */
	spin_lock(&session->job_lock);
	job->state = ZYNPU_JOB_STATE_END;
	job->exception_type = ZYNPU_EXCEP_NO_EXCEPTION;

	if (session->single_thread_poll) {
		queue = get_thread_wait_queue_no_lock(session->wait_queue_head,
			job->uthread_id);
		if (queue)
			thread_queue = &queue->p_wait;
		else {
			pr_err("job waitqueue not found!");
			spin_unlock(&session->job_lock);
			return;
		}
	} else {
		thread_queue = &session->com_wait;
	}

	if (thread_queue)
		wake_up_interruptible(thread_queue);
	else
		pr_err("[%d] thread wait queue not found!", job->uthread_id);

	spin_unlock(&session->job_lock);
	/* IRQ UNLOCK */
}
/*
void zynpu_session_job_update_pdata(struct zynpu_session *session, struct session_job *job)
{
	struct zynpu_priv *zynpu = NULL;
	if ((!session) || (!job)) {
		pr_err("invalid input session or desc or scc args to be null!");
		return;
	}

	zynpu = (struct zynpu_priv *)session->zynpu_priv;

	if (zynpu && job->desc.enable_prof)
		zynpu_priv_read_profiling_reg(zynpu, &job->pdata);

	pr_info("TOT WDATA LSB: 0x%x\n", job->pdata.wdata_tot_lsb);
	pr_info("TOT WDATA MSB: 0x%x\n", job->pdata.wdata_tot_msb);
	pr_info("TOT RDATA LSB: 0x%x\n", job->pdata.rdata_tot_lsb);
	pr_info("TOT RDATA MSB: 0x%x\n", job->pdata.rdata_tot_msb);
	pr_info("TOT CYCLE LSB: 0x%x\n", job->pdata.tot_cycle_lsb);
	pr_info("TOT CYCLE MSB: 0x%x\n", job->pdata.tot_cycle_msb);
}
*/

/********************************************************************************
 *  The following APIs are called in thread context for user query service      *
 *  after job end							       *
 *  -- zynpu_session_query_pdata						 *
 *  -- zynpu_session_thread_has_end_job					  *
 *  -- zynpu_session_get_job_status					      *
 ********************************************************************************/
int zynpu_session_thread_has_end_job(struct zynpu_session *session, int uthread_id)
{
	int ret = 0;
	struct session_job *session_job = NULL;
	struct list_head *node = NULL;
	int wake_up_single = 0;

	if (!session) {
		pr_err("invalid input session or excep args to be null!");
		goto finish;
	}

	/**
	 * If uthread_id found in job_list, then the condition returns is specific to
	 * the status of jobs of this thread (thread-specific); otherwise, the condition
	 * is specific to the status of jobs of this session (fd-specific).
	 */
	spin_lock(&session->job_lock);
	list_for_each(node, &session->job_list.head) {
		session_job = list_entry(node, struct session_job, head);
		if (session_job && (session_job->uthread_id == uthread_id)) {
			wake_up_single = 1;
			break;
		}
	}

	list_for_each(node, &session->job_list.head) {
		session_job = list_entry(node, struct session_job, head);
		if (session_job && (session_job->state == ZYNPU_JOB_STATE_END)) {
			if (wake_up_single) {
				if (session_job->uthread_id == uthread_id) {
					ret = 1;
					break;
				}
			} else {
				ret = 1;
				break;
			}
		}
	}
	spin_unlock(&session->job_lock);

finish:
	return ret;
}

int zynpu_session_get_job_status(struct zynpu_session *session, struct job_status_query *job_status)
{
	int ret = 0;
	int query_cnt;
	struct job_status_desc *status = NULL;
	struct session_job *cursor = NULL;
	struct session_job *next = NULL;
	int poll_iter = 0;

	if ((!session) || (!job_status)) {
		pr_err("invalid input session or excep args to be null!");
		goto finish;
	}

	if (job_status->max_cnt < 1) {
		job_status->errcode = ZYNPU_ERRCODE_INVALID_ARGS;
		ret = -EINVAL;
		goto finish;
	}

	if (job_status->get_single_job)
		query_cnt = 1;
	else
		query_cnt = job_status->max_cnt;

	status = kzalloc(query_cnt * sizeof(struct job_status_desc), GFP_KERNEL);
	if (!status) {
		job_status->errcode = ZYNPU_ERRCODE_NO_MEMORY;
		ret = -ENOMEM;
		goto finish;
	}

	job_status->poll_cnt = 0;
	spin_lock(&session->job_lock);
	list_for_each_entry_safe(cursor, next, &session->job_list.head, head) {
		if (job_status->poll_cnt == job_status->max_cnt)
			break;

		if ((((cursor->desc.job_id == job_status->job_id) && (job_status->get_single_job)) ||
		    (!job_status->get_single_job)) &&
		    (cursor->state == ZYNPU_JOB_STATE_END)) {
			status[poll_iter].job_id = cursor->desc.job_id;
			status[poll_iter].thread_id = session->user_pid;
			status[0].state = (cursor->exception_type == ZYNPU_EXCEP_NO_EXCEPTION) ?
				ZYNPU_JOB_STATE_DONE : ZYNPU_JOB_STATE_EXCEPTION;
			status[poll_iter].pdata = cursor->pdata;
			job_status->poll_cnt++;
			list_del(&cursor->head);
			destroy_session_job(cursor);
			cursor = NULL;
			if (job_status->get_single_job)
				break;
		}
	}
	spin_unlock(&session->job_lock);

	if (!job_status->poll_cnt) {
		job_status->errcode = ZYNPU_ERRCODE_ITEM_NOT_FOUND;
		ret = -ENOENT;
		goto clean;
	}

	ret = copy_to_user((struct job_status_desc __user *)job_status->status, status,
			   job_status->poll_cnt * sizeof(struct job_status_desc));
	if (ZYNPU_ERRCODE_NO_ERROR == ret)
		job_status->errcode = 0;

clean:
	kfree(status);

finish:
	return ret;
}

wait_queue_head_t *zynpu_session_get_wait_queue(struct zynpu_session *session, int uthread_id)
{
    struct zynpu_thread_wait_queue *queue = NULL;

	if (!session) {
		pr_err("invalid input session to be null!");
		return NULL;
	}

	/* LOCK */
	spin_lock(&session->job_lock);
	queue = get_thread_wait_queue_no_lock(session->wait_queue_head, uthread_id);
	spin_unlock(&session->job_lock);
	/* UNLOCK */

	if (queue)
		return &queue->p_wait;

	return NULL;
}

void zynpu_session_add_poll_wait_queue(struct zynpu_session *session,
    struct file *filp, struct poll_table_struct *wait, int uthread_id)
{
	struct zynpu_thread_wait_queue *wait_queue = NULL;
	struct session_job *curr = NULL;

	if ((!session) || (!filp) || (!wait)) {
		pr_err("invalid input session to be null!");
		return;
	}

	spin_lock_bh(&session->job_lock);
	list_for_each_entry(curr, &session->job_list.head, head) {
		if (curr->uthread_id == uthread_id) {
			wait_queue = get_thread_wait_queue_no_lock(session->wait_queue_head,
				uthread_id);
			if (wait_queue) {
				poll_wait(filp, &wait_queue->p_wait, wait);
				session->single_thread_poll = 1;
			} else {
				pr_err("thread wait_queue not found!");
			}
			break;
		}
	}

	if (!session->single_thread_poll)
		poll_wait(filp, &session->com_wait, wait);
	spin_unlock_bh(&session->job_lock);
}

void session_job_mark_sched(struct session_job *job)
{
	if (job)
		job->pdata.sched_kt = ktime_get();
}

void session_job_mark_done(struct session_job *job)
{
	if (job)
		job->pdata.done_kt = ktime_get();
}

int is_session_job_prof_enabled(struct session_job *job)
{
	int ret = 0;
	if (job)
		ret = job->desc.enable_prof;
	return ret;
}
