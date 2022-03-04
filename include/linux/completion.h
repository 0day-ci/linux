/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H

/*
 * (C) Copyright 2001 Linus Torvalds
 *
 * Atomic wait-for-completion handler data structures.
 * See kernel/sched/completion.c for details.
 */

#include <linux/swait.h>

/*
 * struct completion - structure used to maintain state for a "completion"
 *
 * This is the opaque structure used to maintain the state for a "completion".
 * Completions currently use a FIFO to queue threads that have to wait for
 * the "completion" event.
 *
 * See also:  complete(), wait_for_completion() (and friends _timeout,
 * _interruptible, _interruptible_timeout, and _killable), init_completion(),
 * reinit_completion(), and macros DECLARE_COMPLETION(),
 * DECLARE_COMPLETION_ONSTACK().
 */
struct completion {
	unsigned int done;
	struct swait_queue_head wait;
	struct dept_map dmap;
};

#ifdef CONFIG_DEPT
#define dept_wfc_nocheck(m)			dept_map_nocheck(m)
#define dept_wfc_init(m, k, s, n)		dept_map_init(m, k, s, n)
#define dept_wfc_reinit(m)			dept_map_reinit(m)
#define dept_wfc_wait(m, ip)						\
do {									\
	dept_ask_event(m);						\
	dept_wait(m, 1UL, ip, __func__, 0);				\
} while (0)
#define dept_wfc_complete(m, ip)		dept_event(m, 1UL, ip, __func__)
#define dept_wfc_enter(m, ip)			dept_ecxt_enter(m, 1UL, ip, "completion_context_enter", "complete", 0)
#define dept_wfc_exit(m, ip)			dept_ecxt_exit(m, ip)
#else
#define dept_wfc_nocheck(m)			do { } while (0)
#define dept_wfc_init(m, k, s, n)		do { (void)(n); (void)(k); } while (0)
#define dept_wfc_reinit(m)			do { } while (0)
#define dept_wfc_wait(m, ip)			do { } while (0)
#define dept_wfc_complete(m, ip)		do { } while (0)
#define dept_wfc_enter(m, ip)			do { } while (0)
#define dept_wfc_exit(m, ip)			do { } while (0)
#endif

#ifdef CONFIG_DEPT
#define WFC_DEPT_MAP_INIT(work) .dmap = { .name = #work, .skip_cnt = ATOMIC_INIT(0) }
#else
#define WFC_DEPT_MAP_INIT(work)
#endif

#define init_completion_nocheck(x) __init_completion(x, NULL, #x, false)
#define init_completion(x)					\
	do {							\
		static struct dept_key __dkey;			\
		__init_completion(x, &__dkey, #x, true);	\
	} while (0)

#define init_completion_map(x, m) init_completion(x)
static inline void complete_acquire(struct completion *x) {}
static inline void complete_release(struct completion *x) {}

#define COMPLETION_INITIALIZER(work) \
	{ 0, __SWAIT_QUEUE_HEAD_INITIALIZER((work).wait), \
	WFC_DEPT_MAP_INIT(work) }

#define COMPLETION_INITIALIZER_ONSTACK_MAP(work, map) \
	(*({ init_completion_map(&(work), &(map)); &(work); }))

#define COMPLETION_INITIALIZER_ONSTACK(work) \
	(*({ init_completion(&work); &work; }))

/**
 * DECLARE_COMPLETION - declare and initialize a completion structure
 * @work:  identifier for the completion structure
 *
 * This macro declares and initializes a completion structure. Generally used
 * for static declarations. You should use the _ONSTACK variant for automatic
 * variables.
 */
#define DECLARE_COMPLETION(work) \
	struct completion work = COMPLETION_INITIALIZER(work)

/*
 * Lockdep needs to run a non-constant initializer for on-stack
 * completions - so we use the _ONSTACK() variant for those that
 * are on the kernel stack:
 */
/**
 * DECLARE_COMPLETION_ONSTACK - declare and initialize a completion structure
 * @work:  identifier for the completion structure
 *
 * This macro declares and initializes a completion structure on the kernel
 * stack.
 */
#ifdef CONFIG_LOCKDEP
# define DECLARE_COMPLETION_ONSTACK(work) \
	struct completion work = COMPLETION_INITIALIZER_ONSTACK(work)
# define DECLARE_COMPLETION_ONSTACK_MAP(work, map) \
	struct completion work = COMPLETION_INITIALIZER_ONSTACK_MAP(work, map)
#else
# define DECLARE_COMPLETION_ONSTACK(work) DECLARE_COMPLETION(work)
# define DECLARE_COMPLETION_ONSTACK_MAP(work, map) DECLARE_COMPLETION(work)
#endif

/**
 * init_completion - Initialize a dynamically allocated completion
 * @x:  pointer to completion structure that is to be initialized
 *
 * This inline function will initialize a dynamically created completion
 * structure.
 */
static inline void __init_completion(struct completion *x,
				     struct dept_key *dkey,
				     const char *name, bool check)
{
	x->done = 0;

	if (check)
		dept_wfc_init(&x->dmap, dkey, 0, name);
	else
		dept_wfc_nocheck(&x->dmap);

	init_swait_queue_head(&x->wait);
}

/**
 * reinit_completion - reinitialize a completion structure
 * @x:  pointer to completion structure that is to be reinitialized
 *
 * This inline function should be used to reinitialize a completion structure so it can
 * be reused. This is especially important after complete_all() is used.
 */
static inline void reinit_completion(struct completion *x)
{
	x->done = 0;
	dept_wfc_reinit(&x->dmap);
}

extern void wait_for_completion(struct completion *);
extern void wait_for_completion_io(struct completion *);
extern int wait_for_completion_interruptible(struct completion *x);
extern int wait_for_completion_killable(struct completion *x);
extern unsigned long wait_for_completion_timeout(struct completion *x,
						   unsigned long timeout);
extern unsigned long wait_for_completion_io_timeout(struct completion *x,
						    unsigned long timeout);
extern long wait_for_completion_interruptible_timeout(
	struct completion *x, unsigned long timeout);
extern long wait_for_completion_killable_timeout(
	struct completion *x, unsigned long timeout);
extern bool try_wait_for_completion(struct completion *x);
extern bool completion_done(struct completion *x);

extern void complete(struct completion *);
extern void complete_all(struct completion *);

#endif
