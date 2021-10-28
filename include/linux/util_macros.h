/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HELPER_MACROS_H_
#define _LINUX_HELPER_MACROS_H_

#include <linux/math.h>

#define __find_closest(x, a, as, op)					\
({									\
	typeof(as) __fc_i, __fc_as = (as) - 1;				\
	typeof(x) __fc_x = (x);						\
	typeof(*a) const *__fc_a = (a);					\
	for (__fc_i = 0; __fc_i < __fc_as; __fc_i++) {			\
		if (__fc_x op DIV_ROUND_CLOSEST(__fc_a[__fc_i] +	\
						__fc_a[__fc_i + 1], 2))	\
			break;						\
	}								\
	(__fc_i);							\
})

/**
 * find_closest - locate the closest element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element. Must be sorted
 *  in ascending order.
 * @as: Size of 'a'.
 *
 * Returns the index of the element closest to 'x'.
 */
#define find_closest(x, a, as) __find_closest(x, a, as, <=)

/**
 * find_closest_descending - locate the closest element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element. Must be sorted
 *  in descending order.
 * @as: Size of 'a'.
 *
 * Similar to find_closest() but 'a' is expected to be sorted in descending
 * order.
 */
#define find_closest_descending(x, a, as) __find_closest(x, a, as, >=)

/**
 * find_closest_unsorted - locate the closest element in a unsorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element.
 * @as: Size of 'a'.
 *
 * Similar to find_closest() but 'a' has no requirement to being sorted
 */
#define find_closest_unsorted(x, a, as)					\
({									\
	typeof(x) __fc_best_delta, __fc_delta;				\
	typeof(as) __fc_i, __fc_best_idx;				\
	bool __fc_first = true;						\
	for (__fc_i = 0; __fc_i < (as); __fc_i++) {			\
		__fc_delta = abs(a[__fc_i] - (x));			\
		if (__fc_first || __fc_delta < __fc_best_delta) {	\
			__fc_best_delta = __fc_delta;			\
			__fc_best_idx = __fc_i;				\
		}							\
		__fc_first = false;					\
	}								\
	(__fc_best_idx);						\
})

#endif
