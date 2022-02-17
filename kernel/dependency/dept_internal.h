/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dept(DEPendency Tracker) - runtime dependency tracker internal header
 *
 * Started by Byungchul Park <max.byungchul.park@gmail.com>:
 *
 *  Copyright (c) 2020 LG Electronics, Inc., Byungchul Park
 */

#ifndef __DEPT_INTERNAL_H
#define __DEPT_INTERNAL_H

#ifdef CONFIG_DEPT

enum object_t {
#define OBJECT(id, nr) OBJECT_##id,
	#include "dept_object.h"
#undef  OBJECT
	OBJECT_NR,
};

extern struct list_head dept_classes;
extern struct dept_pool dept_pool[];

#endif
#endif /* __DEPT_INTERNAL_H */
