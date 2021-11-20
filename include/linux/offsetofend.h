/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OFFSETOFEND_H
#define _LINUX_OFFSETOFEND_H


#include <linux/offsetof.h>
#include <linux/sizeof_field.h>


/**
 * offsetofend() - Report the offset of a struct field within the struct
 *
 * @T: The type of the structure
 * @m: The member within the structure to get the end offset of
 */
#define offsetofend(T, m)  (offsetof(T, m) + sizeof_field(T, m))


#endif  /* _LINUX_OFFSETOFEND_H */
