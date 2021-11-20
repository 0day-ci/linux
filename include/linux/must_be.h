/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MUST_BE_H
#define _LINUX_MUST_BE_H


#ifdef __CHECKER__
#define BUILD_BUG_ON_ZERO(e) (0)
#else  /* __CHECKER__ */
/*
 * Force a compilation error if condition is true, but also produce a
 * result (of value 0 and type int), so the expression can be used
 * e.g. in a structure initializer (or where-ever else comma expressions
 * aren't permitted).
 */
#define BUILD_BUG_ON_ZERO(e)  ((int)(sizeof(struct { int:(-!!(e)); })))
#endif  /* __CHECKER__ */

#define __must_be(e)  BUILD_BUG_ON_ZERO(!(e))


#endif	/* _LINUX_MUST_BE_H */
