/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FUTEX_H
#define _FUTEX_H

#include <asm/futex.h>

#include <linux/syscalls.h>
#include <linux/time_namespace.h>

/*
 * Futex flags used to encode options to functions and preserve them across
 * restarts.
 */
#ifdef CONFIG_MMU
# define FLAGS_SHARED		0x01
#else
/*
 * NOMMU does not have per process address space. Let the compiler optimize
 * code away.
 */
# define FLAGS_SHARED		0x00
#endif
#define FLAGS_CLOCKRT		0x02
#define FLAGS_HAS_TIMEOUT	0x04

/**
 * struct futex_q - The hashed futex queue entry, one per waiting task
 * @list:		priority-sorted list of tasks waiting on this futex
 * @task:		the task waiting on the futex
 * @lock_ptr:		the hash bucket lock
 * @key:		the key the futex is hashed on
 * @pi_state:		optional priority inheritance state
 * @rt_waiter:		rt_waiter storage for use with requeue_pi
 * @requeue_pi_key:	the requeue_pi target futex key
 * @bitset:		bitset for the optional bitmasked wakeup
 *
 * We use this hashed waitqueue, instead of a normal wait_queue_entry_t, so
 * we can wake only the relevant ones (hashed queues may be shared).
 *
 * A futex_q has a woken state, just like tasks have TASK_RUNNING.
 * It is considered woken when plist_node_empty(&q->list) || q->lock_ptr == 0.
 * The order of wakeup is always to make the first condition true, then
 * the second.
 *
 * PI futexes are typically woken before they are removed from the hash list via
 * the rt_mutex code. See unqueue_me_pi().
 */
struct futex_q {
	struct plist_node list;

	struct task_struct *task;
	spinlock_t *lock_ptr;
	union futex_key key;
	struct futex_pi_state *pi_state;
	struct rt_mutex_waiter *rt_waiter;
	union futex_key *requeue_pi_key;
	u32 bitset;
	atomic_t requeue_state;
#ifdef CONFIG_PREEMPT_RT
	struct rcuwait requeue_wait;
#endif
} __randomize_layout;

/*
 * On PREEMPT_RT, the hash bucket lock is a 'sleeping' spinlock with an
 * underlying rtmutex. The task which is about to be requeued could have
 * just woken up (timeout, signal). After the wake up the task has to
 * acquire hash bucket lock, which is held by the requeue code.  As a task
 * can only be blocked on _ONE_ rtmutex at a time, the proxy lock blocking
 * and the hash bucket lock blocking would collide and corrupt state.
 *
 * On !PREEMPT_RT this is not a problem and everything could be serialized
 * on hash bucket lock, but aside of having the benefit of common code,
 * this allows to avoid doing the requeue when the task is already on the
 * way out and taking the hash bucket lock of the original uaddr1 when the
 * requeue has been completed.
 *
 * The following state transitions are valid:
 *
 * On the waiter side:
 *   Q_REQUEUE_PI_NONE		-> Q_REQUEUE_PI_IGNORE
 *   Q_REQUEUE_PI_IN_PROGRESS	-> Q_REQUEUE_PI_WAIT
 *
 * On the requeue side:
 *   Q_REQUEUE_PI_NONE		-> Q_REQUEUE_PI_INPROGRESS
 *   Q_REQUEUE_PI_IN_PROGRESS	-> Q_REQUEUE_PI_DONE/LOCKED
 *   Q_REQUEUE_PI_IN_PROGRESS	-> Q_REQUEUE_PI_NONE (requeue failed)
 *   Q_REQUEUE_PI_WAIT		-> Q_REQUEUE_PI_DONE/LOCKED
 *   Q_REQUEUE_PI_WAIT		-> Q_REQUEUE_PI_IGNORE (requeue failed)
 *
 * The requeue side ignores a waiter with state Q_REQUEUE_PI_IGNORE as this
 * signals that the waiter is already on the way out. It also means that
 * the waiter is still on the 'wait' futex, i.e. uaddr1.
 *
 * The waiter side signals early wakeup to the requeue side either through
 * setting state to Q_REQUEUE_PI_IGNORE or to Q_REQUEUE_PI_WAIT depending
 * on the current state. In case of Q_REQUEUE_PI_IGNORE it can immediately
 * proceed to take the hash bucket lock of uaddr1. If it set state to WAIT,
 * which means the wakeup is interleaving with a requeue in progress it has
 * to wait for the requeue side to change the state. Either to DONE/LOCKED
 * or to IGNORE. DONE/LOCKED means the waiter q is now on the uaddr2 futex
 * and either blocked (DONE) or has acquired it (LOCKED). IGNORE is set by
 * the requeue side when the requeue attempt failed via deadlock detection
 * and therefore the waiter q is still on the uaddr1 futex.
 */
enum {
	Q_REQUEUE_PI_NONE		=  0,
	Q_REQUEUE_PI_IGNORE,
	Q_REQUEUE_PI_IN_PROGRESS,
	Q_REQUEUE_PI_WAIT,
	Q_REQUEUE_PI_DONE,
	Q_REQUEUE_PI_LOCKED,
};

static const struct futex_q futex_q_init = {
	/* list gets initialized in queue_me()*/
	.key            = FUTEX_KEY_INIT,
	.bitset         = FUTEX_BITSET_MATCH_ANY,
	.requeue_state  = ATOMIC_INIT(Q_REQUEUE_PI_NONE),
};

inline struct hrtimer_sleeper *
futex_setup_timer(ktime_t *time, struct hrtimer_sleeper *timeout,
		  int flags, u64 range_ns);

static __always_inline int
futex_init_timeout(u32 cmd, u32 op, struct timespec64 *ts, ktime_t *t)
{
	if (!timespec64_valid(ts))
		return -EINVAL;

	*t = timespec64_to_ktime(*ts);
	if (cmd == FUTEX_WAIT)
		*t = ktime_add_safe(ktime_get(), *t);
	else if (cmd != FUTEX_LOCK_PI && !(op & FUTEX_CLOCK_REALTIME))
		*t = timens_ktime_to_host(CLOCK_MONOTONIC, *t);
	return 0;
}

/**
 * struct futex_vector - Auxiliary struct for futex_waitv()
 * @w: Userspace provided data
 * @q: Kernel side data
 *
 * Struct used to build an array with all data need for futex_waitv()
 */
struct futex_vector {
	struct futex_waitv w;
	struct futex_q q;
};

int futex_wait_multiple(struct futex_vector *vs, unsigned int count,
			struct hrtimer_sleeper *to);

#endif
