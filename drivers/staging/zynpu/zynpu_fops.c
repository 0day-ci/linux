// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_fops.c
 * Implementations of KMD file operation API
 */

#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include "zynpu_mm.h"
#include "zynpu_job_manager.h"
#include "zynpu_session.h"
#include "zynpu.h"

static int zynpu_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct zynpu_priv *zynpu = NULL;
	struct zynpu_session *session  = NULL;
	int pid = task_pid_nr(current);

	zynpu = container_of(filp->f_op, struct zynpu_priv, zynpu_fops);

	ret = zynpu_create_session(pid, zynpu, &session);
	if (ret) {
		return ret;
	} else {
		filp->private_data = session;
		filp->f_pos = 0;
	}

	/* success */
	return ret;
}

static int zynpu_release(struct inode *inode, struct file *filp)
{
	struct zynpu_priv *zynpu = NULL;
	struct zynpu_session *session = filp->private_data;

	if (!session)
		return -EINVAL;

	zynpu = container_of(filp->f_op, struct zynpu_priv, zynpu_fops);

	/* jobs should be cleared prior to buffer free */
	zynpu_job_manager_cancel_session_jobs(&zynpu->job_manager, session);

	zynpu_mm_free_session_buffers(&zynpu->mm, session);

	zynpu_destroy_session(session);

	return 0;
}

static long zynpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int cp_ret = 0;
	struct zynpu_session *session = filp->private_data;
	struct zynpu_priv *zynpu = NULL;

	struct zynpu_cap cap;
	struct buf_request buf_req;
	struct zynpu_buffer buf;
	struct user_job user_job;
	struct session_job *kern_job = NULL;
	struct buf_desc desc;
	struct zynpu_io_req io_req;
	struct job_status_query job;
	u32 job_id;

	if (!session)
		return -EINVAL;

	zynpu = container_of(filp->f_op, struct zynpu_priv, zynpu_fops);

	switch (cmd)
	{
	case IPUIOC_QUERYCAP:
		ret = copy_from_user(&cap, (struct zynpu_cap __user*)arg, sizeof(struct zynpu_cap));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: QUERYCAP copy from user failed!");
		else {
			zynpu_priv_query_capability(zynpu, &cap);
			/* copy cap info/errcode to user for reference */
			cp_ret = copy_to_user((struct zynpu_cap __user*)arg, &cap, sizeof(struct zynpu_cap));
			if ((ZYNPU_ERRCODE_NO_ERROR == ret) && (cp_ret))
				ret = cp_ret;
		}
		break;
	case IPUIOC_REQBUF:
		ret = copy_from_user(&buf_req, (struct buf_request __user*)arg, sizeof(struct buf_request));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: REQBUF copy from user failed!");
		else {
			ret = zynpu_mm_alloc(&zynpu->mm, &buf_req, &buf);
			if (ZYNPU_ERRCODE_NO_ERROR == ret) {
				ret = zynpu_session_add_buf(session, &buf_req, &buf);
				if (ret)
					dev_err(zynpu->dev, "KMD ioctl: add buf failed!");
			}

			/* copy buf info/errcode to user for reference */
			cp_ret = copy_to_user((struct buf_request __user*)arg, &buf_req, sizeof(struct buf_request));
			if ((ZYNPU_ERRCODE_NO_ERROR == ret) && (cp_ret))
				ret = cp_ret;
		}
		break;
	case IPUIOC_RUNJOB:
		ret = copy_from_user(&user_job, (struct user_job __user*)arg, sizeof(struct user_job));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: RUNJOB copy from user failed!");
		else {
			kern_job = zynpu_session_add_job(session, &user_job);
			if (NULL == kern_job)
				dev_err(zynpu->dev, "KMD ioctl: RUNJOB add failed!");
			else {
				ret = zynpu_job_manager_schedule_new_job(&zynpu->job_manager, &user_job, kern_job,
					session);
				if (ret)
					dev_err(zynpu->dev, "KMD ioctl: RUNJOB run failed!");
			}

			/* copy job errcode to user for reference */
			cp_ret = copy_to_user((struct user_job __user*)arg, &user_job, sizeof(struct user_job));
			if ((ZYNPU_ERRCODE_NO_ERROR == ret) && (cp_ret))
				ret = cp_ret;
		}
		break;
	case IPUIOC_KILL_TIMEOUT_JOB:
		ret = copy_from_user(&job_id, (u32 __user*)arg, sizeof(__u32));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: KILL_TIMEOUT_JOB copy from user failed!");
		else
			ret = zynpu_invalidate_timeout_job(&zynpu->job_manager, job_id);
		break;
	case IPUIOC_FREEBUF:
		ret = copy_from_user(&desc, (struct buf_desc __user*)arg, sizeof(struct buf_desc));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: FREEBUF copy from user failed!");
		else {
			/* detach first to validate the free buf request */
			ret = zynpu_session_detach_buf(session, &desc);
			if (ret)
				dev_err(zynpu->dev, "KMD ioctl: detach session buffer failed!");
			else {
				/* do free operation */
				ret = zynpu_mm_free(&zynpu->mm, &desc);
				if (ret)
					dev_err(zynpu->dev, "KMD ioctl: free buf failed!");
			}
		}
		break;
	case IPUIOC_REQIO:
		ret = copy_from_user(&io_req, (struct zynpu_io_req __user *)arg, sizeof(struct zynpu_io_req));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: REQIO copy from user failed!");
		else {
			zynpu_priv_io_rw(zynpu, &io_req);
			ret = copy_to_user((struct zynpu_io_req __user *)arg, &io_req, sizeof(struct zynpu_io_req));
		}
		break;
	case IPUIOC_QUERYSTATUS:
		ret = copy_from_user(&job, (struct job_status_query __user *)arg,
		    sizeof(struct job_status_query));
		if (ret)
			dev_err(zynpu->dev, "KMD ioctl: QUERYSTATUS copy from user failed!");
		else {
			ret = zynpu_session_get_job_status(session, &job);
			if (ZYNPU_ERRCODE_NO_ERROR == ret)
				ret = copy_to_user((struct job_status_query __user *)arg, &job,
				    sizeof(struct job_status_query));
		}
		break;
	default:
		dev_err(zynpu->dev, "no matching ioctl call (cmd = 0x%lx)!", (unsigned long)cmd);
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static int zynpu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;
	struct zynpu_priv *zynpu = NULL;
	struct zynpu_session *session = filp->private_data;

	if (!session)
		return -EINVAL;

	zynpu = container_of(filp->f_op, struct zynpu_priv, zynpu_fops);
	ret = zynpu_session_mmap_buf(session, vma, zynpu->dev);
	if (ret)
		dev_err(zynpu->dev, "mmap to userspace failed!");

	return ret;
}

static unsigned int zynpu_poll(struct file *filp, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	struct zynpu_session *session = filp->private_data;
	int tid = task_pid_nr(current);

	if (!session)
		return 0;

	zynpu_session_add_poll_wait_queue(session, filp, wait, tid);

	if (zynpu_session_thread_has_end_job(session, tid))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

int zynpu_fops_register(struct file_operations *fops)
{
	if (!fops)
		return -EINVAL;

	fops->owner = THIS_MODULE;
	fops->open = zynpu_open;
	fops->poll = zynpu_poll;
	fops->unlocked_ioctl = zynpu_ioctl;
#ifdef CONFIG_COMPAT
	fops->compat_ioctl = zynpu_ioctl;
#endif
	fops->mmap = zynpu_mmap;
	fops->release = zynpu_release;

	return 0;
}
