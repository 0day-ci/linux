/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CONTAINER_OF_H
#define _LINUX_CONTAINER_OF_H


#include <linux/build_bug.h>
#include <linux/err.h>
#include <linux/memberof.h>
#include <linux/typeof_member.h>


/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member)  (				\
{									\
	const void *__mptr = (ptr);					\
									\
	static_assert(__same_type(*(ptr), memberof(type, member)) ||	\
		      __same_type(*(ptr), void),			\
		      "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member)));			\
}									\
)

/**
 * container_of_safe - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * If IS_ERR_OR_NULL(ptr), ptr is returned unchanged.
 */
#define container_of_safe(ptr, type, member) 				\
	(IS_ERR_OR_NULL(ptr) ? ERR_CAST(ptr) : container_of(type, member))

#endif	/* _LINUX_CONTAINER_OF_H */
