/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIZEOF_FIELD_H
#define _LINUX_SIZEOF_FIELD_H


/**
 * sizeof_field() - Report the size of a struct field in bytes
 *
 * @T: The structure containing the field of interest
 * @m: The field (member) to return the size of
 */
#define sizeof_field(T, m)  sizeof((((T *)0)->m))


#endif  /* _LINUX_SIZEOF_FIELD_H */
