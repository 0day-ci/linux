/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMBEROF_H
#define _LINUX_MEMBEROF_H


#include <linux/NULL.h>


#define memberof(T, m)  (((T *)NULL)->m)


#endif  /* _LINUX_MEMBEROF_H */
