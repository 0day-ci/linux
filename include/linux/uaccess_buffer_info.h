/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ACCESS_BUFFER_INFO_H
#define _LINUX_ACCESS_BUFFER_INFO_H

#include <uapi/asm/signal.h>

#ifdef CONFIG_UACCESS_BUFFER

struct uaccess_buffer_info {
	unsigned long addr, size;
	unsigned long store_end_addr;
	sigset_t saved_sigmask;
	u8 state;
};

#else

struct uaccess_buffer_info {
};

#endif

#endif  /* _LINUX_ACCESS_BUFFER_INFO_H */
