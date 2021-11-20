/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SAME_TYPE_H
#define __LINUX_SAME_TYPE_H


/* Are two types/vars the same type (ignoring qualifiers)? */
#define __same_type(a, b)  __builtin_types_compatible_p(typeof(a), typeof(b))


#endif /* __LINUX_SAME_TYPE_H */
