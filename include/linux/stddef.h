/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#include <uapi/linux/stddef.h>

#undef NULL
#define NULL ((void *)0)

enum {
	false	= 0,
	true	= 1
};

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE, MEMBER)	__compiler_offsetof(TYPE, MEMBER)
#else
#define offsetof(TYPE, MEMBER)	((size_t)&((TYPE *)0)->MEMBER)
#endif

/**
 * sizeof_field(TYPE, MEMBER)
 *
 * @TYPE: The structure containing the field of interest
 * @MEMBER: The field to return the size of
 */
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

/**
 * offsetofend(TYPE, MEMBER)
 *
 * @TYPE: The type of the structure
 * @MEMBER: The member within the structure to get the end offset of
 */
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER)	+ sizeof_field(TYPE, MEMBER))

/**
 * struct_group_attr(NAME, ATTRS, MEMBERS)
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members. Includes structure attributes argument.
 *
 * @NAME: The name of the mirrored sub-struct
 * @ATTRS: Any struct attributes (normally empty)
 * @MEMBERS: The member declarations for the mirrored structs
 */
#define struct_group_attr(NAME, ATTRS, MEMBERS) \
	union { \
		struct { MEMBERS } ATTRS; \
		struct { MEMBERS } ATTRS NAME; \
	}

/**
 * struct_group(NAME, MEMBERS)
 *
 * Used to create an anonymous union of two structs with identical
 * layout and size: one anonymous and one named. The former can be
 * used normally without sub-struct naming, and the latter can be
 * used to reason about the start, end, and size of the group of
 * struct members.
 *
 * @NAME: The name of the mirrored sub-struct
 * @MEMBERS: The member declarations for the mirrored structs
 */
#define struct_group(NAME, MEMBERS)	\
	struct_group_attr(NAME, /* no attrs */, MEMBERS)

#endif
