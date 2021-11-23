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

	if (!entry || unlikely(uaccess_kernel()))
		return;
	entry->addr = addr;
	entry->size = size;
	entry->flags = flags;

	++buf->kcur;
	if (buf->kcur == buf->kend)
		buf->kcur = 0;
}

void uaccess_buffer_log_read(const void __user *from, unsigned long n)
{
	uaccess_buffer_log((unsigned long)from, n, 0);
}
EXPORT_SYMBOL(uaccess_buffer_log_read);

void uaccess_buffer_log_write(void __user *to, unsigned long n)
{
	uaccess_buffer_log((unsigned long)to, n, UACCESS_BUFFER_FLAG_WRITE);
}
EXPORT_SYMBOL(uaccess_buffer_log_write);

int uaccess_buffer_set_descriptor_addr_addr(unsigned long addr)
{
	current->uaccess_buffer.desc_ptr_ptr =
		(struct uaccess_descriptor __user * __user *)addr;
	uaccess_buffer_cancel_log(current);
	return 0;
}

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

void uaccess_buffer_cancel_log(struct task_struct *tsk)
{
	struct uaccess_buffer_info *buf = &tsk->uaccess_buffer;

	if (buf->kcur) {
		buf->kcur = 0;
		kfree(buf->kbegin);
	}
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

	buf->kbegin = kmalloc_array(
		desc.size, sizeof(struct uaccess_buffer_entry), GFP_KERNEL);
	buf->kcur = buf->kbegin;
	buf->kend = buf->kbegin + desc.size;
	buf->ubegin = (struct uaccess_buffer_entry __user *)desc.addr;
}

void __uaccess_buffer_syscall_exit(void)
{
	struct uaccess_buffer_info *buf = &current->uaccess_buffer;
	u64 num_entries = buf->kcur - buf->kbegin;
	struct uaccess_descriptor desc;

	if (!buf->kcur)
		return;

	desc.addr = (u64)(buf->ubegin + num_entries);
	desc.size = buf->kend - buf->kcur;
	buf->kcur = 0;
	if (copy_to_user(buf->ubegin, buf->kbegin,
			 num_entries * sizeof(struct uaccess_buffer_entry)) == 0)
		(void)copy_to_user(buf->desc_ptr, &desc, sizeof(desc));

	kfree(buf->kbegin);
}
