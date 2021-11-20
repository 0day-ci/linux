/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ARRAY_SIZE_H
#define _LINUX_ARRAY_SIZE_H

#include <linux/must_be.h>
#include <linux/same_type.h>


/* &a[0] degrades to a pointer: a different type from an array */
#define __is_array(a)  (!__same_type((a), &(a)[0]))

#define __must_be_array(a)  __must_be(__is_array(a))

/**
 * ARRAY_SIZE - get the number of elements in array @a
 * @a: array to be sized
 */
#define ARRAY_SIZE(a)  (sizeof((a)) / sizeof((a)[0]) + __must_be_array(a))


#endif  /* _LINUX_ARRAY_SIZE_H */
