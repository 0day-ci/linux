/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dept Single-event Dependency Tracker
 *
 * Started by Byungchul Park <max.byungchul.park@gmail.com>:
 *
 *  Copyright (c) 2020 LG Electronics, Inc., Byungchul Park
 */

#ifndef __LINUX_DEPT_SDT_H
#define __LINUX_DEPT_SDT_H

#include <linux/dept.h>

#ifdef CONFIG_DEPT
#define DEPT_SDT_MAP_INIT(dname)	{ .name = #dname }

/*
 * SDT(Single-event Dependency Tracker) APIs
 *
 * In case that one dept_map instance maps to a single event, SDT APIs
 * can be used.
 */
#define sdt_map_init(m)							\
	do {								\
		static struct dept_key __key;				\
		dept_map_init(m, &__key, 0, #m);			\
	} while (0)
#define sdt_map_init_key(m, k)		dept_map_init(m, k, 0, #m)

#define sdt_wait(m)							\
	do {								\
		dept_ask_event(m);					\
		dept_wait(m, 1UL, _THIS_IP_, "wait", 0);		\
	} while (0)
/*
 * This will be committed in __schedule() when it actually gets to
 * __schedule(). Both dept_ask_event() and dept_wait() will be performed
 * on the commit in __schedule().
 */
#define sdt_wait_prepare(m)		dept_stage_wait(m, 1UL, "wait", 0)
#define sdt_wait_finish()		dept_clean_stage()
#define sdt_ecxt_enter(m)		dept_ecxt_enter(m, 1UL, _THIS_IP_, "start", "event", 0)
#define sdt_event(m)			dept_event(m, 1UL, _THIS_IP_, "event")
#define sdt_ecxt_exit(m)		dept_ecxt_exit(m, _THIS_IP_)
#else /* !CONFIG_DEPT */
#define DEPT_SDT_MAP_INIT(dname)	{ }

#define sdt_map_init(m)			do { } while (0)
#define sdt_map_init_key(m, k)		do { (void)(k); } while (0)
#define sdt_wait(m)			do { } while (0)
#define sdt_wait_prepare(m)		do { } while (0)
#define sdt_wait_finish()		do { } while (0)
#define sdt_ecxt_enter(m)		do { } while (0)
#define sdt_event(m)			do { } while (0)
#define sdt_ecxt_exit(m)		do { } while (0)
#endif

#define DEFINE_DEPT_SDT(x)		\
	struct dept_map x = DEPT_SDT_MAP_INIT(x)

#endif /* __LINUX_DEPT_SDT_H */
