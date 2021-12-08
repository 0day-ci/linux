// SPDX-License-Identifier: GPL-2.0
/*
 * Support for uaccess logging via uaccess buffers.
 *
 * Copyright (C) 2021, Google LLC.
 */

#include <linux/compat.h>
#include <linux/mm.h>
#include <linux/prctl.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/uaccess-buffer.h>

static void uaccess_buffer_log(unsigned long addr, unsigned long size,
			      unsigned long flags)
{
	struct uaccess_buffer_info *buf = &current->uaccess_buffer;
	struct uaccess_buffer_entry *entry = buf->kcur;

	if (entry == buf->kend || unlikely(uaccess_kernel()))
		return;
	entry->addr = addr;
	entry->size = size;
	entry->flags = flags;

	++buf->kcur;
}

void __uaccess_buffer_log_read(const void __user *from, unsigned long n)
{
	uaccess_buffer_log((unsigned long)from, n, 0);
}
EXPORT_SYMBOL(__uaccess_buffer_log_read);

void __uaccess_buffer_log_write(void __user *to, unsigned long n)
{
	uaccess_buffer_log((unsigned long)to, n, UACCESS_BUFFER_FLAG_WRITE);
}
EXPORT_SYMBOL(__uaccess_buffer_log_write);

bool __uaccess_buffer_pre_exit_loop(void)
{
	struct uaccess_buffer_info *buf = &current->uaccess_buffer;
	struct uaccess_descriptor __user *desc_ptr;
	sigset_t tmp_mask;

	if (get_user(desc_ptr, buf->desc_ptr_ptr) || !desc_ptr)
		return false;

	current->real_blocked = current->blocked;
	sigfillset(&tmp_mask);
	set_current_blocked(&tmp_mask);
	return true;
}

void __uaccess_buffer_post_exit_loop(void)
{
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = current->real_blocked;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

void uaccess_buffer_free(struct task_struct *tsk)
{
	struct uaccess_buffer_info *buf = &tsk->uaccess_buffer;

	kfree(buf->kbegin);
	clear_syscall_work(UACCESS_BUFFER_EXIT);
	buf->kbegin = buf->kcur = buf->kend = NULL;
}

void __uaccess_buffer_syscall_entry(void)
{
	struct uaccess_buffer_info *buf = &current->uaccess_buffer;
	struct uaccess_descriptor desc;

	if (get_user(buf->desc_ptr, buf->desc_ptr_ptr) || !buf->desc_ptr ||
	    put_user(0, buf->desc_ptr_ptr) ||
	    copy_from_user(&desc, buf->desc_ptr, sizeof(desc)))
		return;

	if (desc.size > 1024)
		desc.size = 1024;

	if (buf->kend - buf->kbegin != desc.size)
		buf->kbegin =
			krealloc_array(buf->kbegin, desc.size,
				       sizeof(struct uaccess_buffer_entry),
				       GFP_KERNEL);
	if (!buf->kbegin)
		return;

	set_syscall_work(UACCESS_BUFFER_EXIT);
	buf->kcur = buf->kbegin;
	buf->kend = buf->kbegin + desc.size;
	buf->ubegin =
		(struct uaccess_buffer_entry __user *)(unsigned long)desc.addr;
}

void __uaccess_buffer_syscall_exit(void)
{
	struct uaccess_buffer_info *buf = &current->uaccess_buffer;
	u64 num_entries = buf->kcur - buf->kbegin;
	struct uaccess_descriptor desc;

	clear_syscall_work(UACCESS_BUFFER_EXIT);
	desc.addr = (u64)(unsigned long)(buf->ubegin + num_entries);
	desc.size = buf->kend - buf->kcur;
	buf->kcur = NULL;
	if (copy_to_user(buf->ubegin, buf->kbegin,
			 num_entries * sizeof(struct uaccess_buffer_entry)) == 0)
		(void)copy_to_user(buf->desc_ptr, &desc, sizeof(desc));
}

size_t copy_from_user_nolog(void *to, const void __user *from, size_t len)
{
	size_t retval;

	clear_syscall_work(UACCESS_BUFFER_EXIT);
	retval = copy_from_user(to, from, len);
	if (current->uaccess_buffer.kcur)
		set_syscall_work(UACCESS_BUFFER_EXIT);
	return retval;
}
