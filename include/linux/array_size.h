/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ARRAY_SIZE_H
#define _LINUX_ARRAY_SIZE_H

#include <linux/compiler.h>


/**
 * ARRAY_SIZE - get the number of elements in array @a
 * @a: array to be sized
 */
#define ARRAY_SIZE(a)  (sizeof((a)) / sizeof((a)[0]) + __must_be_array(a))


#endif  /* _LINUX_ARRAY_SIZE_H */
