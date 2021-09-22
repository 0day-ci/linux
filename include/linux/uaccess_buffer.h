/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ACCESS_BUFFER_H
#define _LINUX_ACCESS_BUFFER_H

#include <asm-generic/errno-base.h>

#ifdef CONFIG_UACCESS_BUFFER

void uaccess_buffer_log_read(const void __user *from, unsigned long n);
void uaccess_buffer_log_write(void __user *to, unsigned long n);

void uaccess_buffer_syscall_entry(void);
void uaccess_buffer_syscall_exit(void);

int uaccess_buffer_set_logging(unsigned long addr, unsigned long size,
			       unsigned long store_end_addr);

#else

static inline void uaccess_buffer_log_read(const void __user *from,
					   unsigned long n)
{
}
static inline void uaccess_buffer_log_write(void __user *to, unsigned long n)
{
}

static inline void uaccess_buffer_syscall_entry(void)
{
}
static inline void uaccess_buffer_syscall_exit(void)
{
}

static inline int uaccess_buffer_set_logging(unsigned long addr,
					     unsigned long size,
					     unsigned long store_end_addr)
{
	return -EINVAL;
}
#endif

#endif  /* _LINUX_ACCESS_BUFFER_H */
