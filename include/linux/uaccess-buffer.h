/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UACCESS_BUFFER_H
#define _LINUX_UACCESS_BUFFER_H

#include <linux/sched.h>
#include <uapi/linux/uaccess-buffer.h>

#include <asm-generic/errno-base.h>

#ifdef CONFIG_HAVE_ARCH_UACCESS_BUFFER

static inline bool uaccess_buffer_maybe_blocked(struct task_struct *tsk)
{
	return test_task_syscall_work(tsk, UACCESS_BUFFER_ENTRY);
}

void __uaccess_buffer_syscall_entry(void);
static inline void uaccess_buffer_syscall_entry(void)
{
	__uaccess_buffer_syscall_entry();
}

void __uaccess_buffer_syscall_exit(void);
static inline void uaccess_buffer_syscall_exit(void)
{
	__uaccess_buffer_syscall_exit();
}

bool __uaccess_buffer_pre_exit_loop(void);
static inline bool uaccess_buffer_pre_exit_loop(void)
{
	if (!test_syscall_work(UACCESS_BUFFER_ENTRY))
		return false;
	return __uaccess_buffer_pre_exit_loop();
}

void __uaccess_buffer_post_exit_loop(void);
static inline void uaccess_buffer_post_exit_loop(bool pending)
{
	if (pending)
		__uaccess_buffer_post_exit_loop();
}

static inline int uaccess_buffer_set_descriptor_addr_addr(unsigned long addr)
{
	current->uaccess_buffer.desc_ptr_ptr =
		(struct uaccess_descriptor __user * __user *)addr;
	if (addr)
		set_syscall_work(UACCESS_BUFFER_ENTRY);
	else
		clear_syscall_work(UACCESS_BUFFER_ENTRY);
	return 0;
}

size_t copy_from_user_nolog(void *to, const void __user *from, size_t len);

void uaccess_buffer_free(struct task_struct *tsk);

void __uaccess_buffer_log_read(const void __user *from, unsigned long n);
static inline void uaccess_buffer_log_read(const void __user *from, unsigned long n)
{
	if (unlikely(test_syscall_work(UACCESS_BUFFER_EXIT)))
		__uaccess_buffer_log_read(from, n);
}

void __uaccess_buffer_log_write(void __user *to, unsigned long n);
static inline void uaccess_buffer_log_write(void __user *to, unsigned long n)
{
	if (unlikely(test_syscall_work(UACCESS_BUFFER_EXIT)))
		__uaccess_buffer_log_write(to, n);
}

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
static inline int uaccess_buffer_set_descriptor_addr_addr(unsigned long addr)
{
	return -EINVAL;
}
static inline void uaccess_buffer_free(struct task_struct *tsk)
{
}

#define copy_from_user_nolog(to, from, len) copy_from_user(to, from, len)

static inline void uaccess_buffer_log_read(const void __user *from,
					   unsigned long n)
{
}
static inline void uaccess_buffer_log_write(void __user *to, unsigned long n)
{
}

#endif

#endif  /* _LINUX_UACCESS_BUFFER_H */
