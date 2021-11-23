/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UACCESS_BUFFER_LOG_HOOKS_H
#define _LINUX_UACCESS_BUFFER_LOG_HOOKS_H

#ifdef CONFIG_HAVE_ARCH_UACCESS_BUFFER

struct uaccess_buffer_info {
	/*
	 * The pointer to pointer to struct uaccess_descriptor. This is the
	 * value controlled by prctl(PR_SET_UACCESS_DESCRIPTOR_ADDR_ADDR).
	 */
	struct uaccess_descriptor __user *__user *desc_ptr_ptr;

	/*
	 * The pointer to struct uaccess_descriptor read at syscall entry time.
	 */
	struct uaccess_descriptor __user *desc_ptr;

	/*
	 * A pointer to the kernel's temporary copy of the uaccess log for the
	 * current syscall. We log to a kernel buffer in order to avoid leaking
	 * timing information to userspace.
	 */
	struct uaccess_buffer_entry *kbegin;

	/*
	 * The position of the next uaccess buffer entry for the current
	 * syscall.
	 */
	struct uaccess_buffer_entry *kcur;

	/*
	 * A pointer to the end of the kernel's uaccess log.
	 */
	struct uaccess_buffer_entry *kend;

	/*
	 * The pointer to the userspace uaccess log, as read from the
	 * struct uaccess_descriptor.
	 */
	struct uaccess_buffer_entry __user *ubegin;
};

void uaccess_buffer_log_read(const void __user *from, unsigned long n);
void uaccess_buffer_log_write(void __user *to, unsigned long n);

#else

static inline void uaccess_buffer_log_read(const void __user *from,
					   unsigned long n)
{
}
static inline void uaccess_buffer_log_write(void __user *to, unsigned long n)
{
}

#endif

#endif  /* _LINUX_UACCESS_BUFFER_LOG_HOOKS_H */
