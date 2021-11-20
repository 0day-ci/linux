/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TYPEOF_MEMBER_H
#define _LINUX_TYPEOF_MEMBER_H


#include <linux/memberof.h>


#define typeof_member(T, m)  typeof(memberof(T, m))


#endif	/* _LINUX_TYPEOF_MEMBER_H */
