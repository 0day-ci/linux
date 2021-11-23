/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UACCESS_BUFFER_H
#define _LINUX_UACCESS_BUFFER_H

#include <linux/sched.h>
#include <uapi/linux/uaccess-buffer.h>

#include <asm-generic/errno-base.h>

#ifdef CONFIG_HAVE_ARCH_UACCESS_BUFFER

static inline bool uaccess_buffer_maybe_blocked(struct task_struct *tsk)
{
	return tsk->uaccess_buffer.desc_ptr_ptr;
}

void __uaccess_buffer_syscall_entry(void);
static inline void uaccess_buffer_syscall_entry(void)
{
	if (uaccess_buffer_maybe_blocked(current))
		__uaccess_buffer_syscall_entry();
}

void __uaccess_buffer_syscall_exit(void);
static inline void uaccess_buffer_syscall_exit(void)
{
	if (uaccess_buffer_maybe_blocked(current))
		__uaccess_buffer_syscall_exit();
}

bool __uaccess_buffer_pre_exit_loop(void);
static inline bool uaccess_buffer_pre_exit_loop(void)
{
	if (!uaccess_buffer_maybe_blocked(current))
		return false;
	return __uaccess_buffer_pre_exit_loop();
}

void __uaccess_buffer_post_exit_loop(void);
static inline void uaccess_buffer_post_exit_loop(bool pending)
{
	if (pending)
		__uaccess_buffer_post_exit_loop();
}

void uaccess_buffer_cancel_log(struct task_struct *tsk);
int uaccess_buffer_set_descriptor_addr_addr(unsigned long addr);

#else

static inline bool uaccess_buffer_maybe_blocked(struct task_struct *tsk)
{
	return false;
}

static inline void uaccess_buffer_syscall_entry(void)
{
}
static inline void uaccess_buffer_syscall_exit(void)
{
}
static inline bool uaccess_buffer_pre_exit_loop(void)
{
	return false;
}
static inline void uaccess_buffer_post_exit_loop(bool pending)
{
}
static inline void uaccess_buffer_cancel_log(struct task_struct *tsk)
{
}

static inline int uaccess_buffer_set_descriptor_addr_addr(unsigned long addr)
{
	return -EINVAL;
}
#endif

#endif  /* _LINUX_UACCESS_BUFFER_H */
