/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TYPECHECK_H_INCLUDED
#define TYPECHECK_H_INCLUDED

#include <linux/compiler_types.h>
#include <linux/build_bug.h>

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
({	BUILD_BUG_ON(!__same_type(type, (x))); \
	1; \
})

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function) \
({	typeof(type) __tmp = function; \
	(void)__tmp; \
})

/*
 * Check at compile time that something is a pointer type.
 */
#define typecheck_pointer(x) \
({	typeof(x) __dummy; \
	(void)sizeof(*__dummy); \
	1; \
})

#endif		/* TYPECHECK_H_INCLUDED */
