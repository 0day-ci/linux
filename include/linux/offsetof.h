/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OFFSETOF_H
#define _LINUX_OFFSETOF_H


#include <uapi/linux/stddef.h>

#include <linux/memberof.h>


#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(T, m)	__compiler_offsetof(T, m)
#else
#define offsetof(T, m)	((size_t)&memberof(T, m))
#endif


#endif  /* _LINUX_OFFSETOF_H */
