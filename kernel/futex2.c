// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * futex2 system call interface by André Almeida <andrealmeid@collabora.com>
 *
 * Copyright 2021 Collabora Ltd.
 */

#include <asm/futex.h>

#include <linux/freezer.h>
#include <linux/syscalls.h>

/*
 * Set of flags that futex2 accepts
 */
#define FUTEX2_MASK (FUTEX_SIZE_MASK | FUTEX_SHARED_FLAG | FUTEX_CLOCK_REALTIME)

/* Mask for each futex in futex_waitv list */
#define FUTEXV_WAITER_MASK (FUTEX_SIZE_MASK | FUTEX_SHARED_FLAG)

/* Mask for sys_futex_waitv flag */
#define FUTEXV_MASK (FUTEX_CLOCK_REALTIME)

/**
 * unqueue_multiple() - Remove various futexes from their futex_hash_bucket
 * @v:	   The list of futexes to unqueue
 * @count: Number of futexes in the list
 *
 * Helper to unqueue a list of futexes. This can't fail.
 *
 * Return:
 *  - >=0 - Index of the last futex that was awoken;
 *  - -1  - No futex was awoken
 */
static int unqueue_multiple(struct futex_vector *v, int count)
{
	int ret = -1, i;

	for (i = 0; i < count; i++) {
		if (!unqueue_me(&v[i].q))
			ret = i;
	}

	return ret;
}

/**
 * futex_wait_multiple_setup() - Prepare to wait and enqueue multiple futexes
 * @vs:		The corresponding futex list
 * @count:	The size of the list
 * @awaken:	Index of the last awoken futex (return parameter)
 *
 * Prepare multiple futexes in a single step and enqueue them. This may fail if
 * the futex list is invalid or if any futex was already awoken. On success the
 * task is ready to interruptible sleep.
 *
 * Return:
 *  -  1 - One of the futexes was awaken by another thread
 *  -  0 - Success
 *  - <0 - -EFAULT, -EWOULDBLOCK or -EINVAL
 */
static int futex_wait_multiple_setup(struct futex_vector *vs, int count, int *awaken)
{
	struct futex_hash_bucket *hb;
	int ret, i;
	u32 uval;

	/*
	 * Enqueuing multiple futexes is tricky, because we need to
	 * enqueue each futex in the list before dealing with the next
	 * one to avoid deadlocking on the hash bucket.  But, before
	 * enqueuing, we need to make sure that current->state is
	 * TASK_INTERRUPTIBLE, so we don't absorb any awake events, which
	 * cannot be done before the get_futex_key of the next key,
	 * because it calls get_user_pages, which can sleep.  Thus, we
	 * fetch the list of futexes keys in two steps, by first pinning
	 * all the memory keys in the futex key, and only then we read
	 * each key and queue the corresponding futex.
	 */
retry:
	for (i = 0; i < count; i++) {
		ret = get_futex_key(vs[i].w.uaddr,
				    vs[i].w.flags & FUTEX_SHARED_FLAG,
				    &vs[i].q.key, FUTEX_READ);
		if (unlikely(ret))
			return ret;
	}

	set_current_state(TASK_INTERRUPTIBLE);

	for (i = 0; i < count; i++) {
		struct futex_q *q = &vs[i].q;
		struct futex_waitv *waitv = &vs[i].w;

		hb = queue_lock(q);
		ret = get_futex_value_locked(&uval, waitv->uaddr);
		if (ret) {
			/*
			 * We need to try to handle the fault, which
			 * cannot be done without sleep, so we need to
			 * undo all the work already done, to make sure
			 * we don't miss any wake ups.  Therefore, clean
			 * up, handle the fault and retry from the
			 * beginning.
			 */
			queue_unlock(hb);
			__set_current_state(TASK_RUNNING);

			*awaken = unqueue_multiple(vs, i);
			if (*awaken >= 0)
				return 1;

			if (get_user(uval, (u32 __user *)waitv->uaddr))
				return -EINVAL;

			goto retry;
		}

		if (uval != waitv->val) {
			queue_unlock(hb);
			__set_current_state(TASK_RUNNING);

			/*
			 * If something was already awaken, we can
			 * safely ignore the error and succeed.
			 */
			*awaken = unqueue_multiple(vs, i);
			if (*awaken >= 0)
				return 1;

			return -EWOULDBLOCK;
		}

		/*
		 * The bucket lock can't be held while dealing with the
		 * next futex. Queue each futex at this moment so hb can
		 * be unlocked.
		 */
		queue_me(&vs[i].q, hb);
	}
	return 0;
}

/**
 * futex_wait_multiple() - Prepare to wait on and enqueue several futexes
 * @vs:		The list of futexes to wait on
 * @count:	The number of objects
 * @to:		Timeout before giving up and returning to userspace
 *
 * Entry point for the FUTEX_WAIT_MULTIPLE futex operation, this function
 * sleeps on a group of futexes and returns on the first futex that
 * triggered, or after the timeout has elapsed.
 *
 * Return:
 *  - >=0 - Hint to the futex that was awoken
 *  - <0  - On error
 */
static int futex_wait_multiple(struct futex_vector *vs, unsigned int count,
			       struct hrtimer_sleeper *to)
{
	int ret, hint = 0;
	unsigned int i;

	while (1) {
		ret = futex_wait_multiple_setup(vs, count, &hint);
		if (ret) {
			if (ret > 0) {
				/* A futex was awaken during setup */
				ret = hint;
			}
			return ret;
		}

		if (to)
			hrtimer_start_expires(&to->timer, HRTIMER_MODE_ABS);

		/*
		 * Avoid sleeping if another thread already tried to
		 * wake us.
		 */
		for (i = 0; i < count; i++) {
			if (plist_node_empty(&vs[i].q.list))
				break;
		}

		if (i == count && (!to || to->task))
			freezable_schedule();

		__set_current_state(TASK_RUNNING);

		ret = unqueue_multiple(vs, count);
		if (ret >= 0)
			return ret;

		if (to && !to->task)
			return -ETIMEDOUT;
		else if (signal_pending(current))
			return -ERESTARTSYS;
		/*
		 * The final case is a spurious wakeup, for
		 * which just retry.
		 */
	}
}

#ifdef CONFIG_COMPAT
/**
 * compat_futex_parse_waitv - Parse a waitv array from userspace
 * @futexv:	Kernel side list of waiters to be filled
 * @uwaitv:     Userspace list to be parsed
 * @nr_futexes: Length of futexv
 *
 * Return: Error code on failure, pointer to a prepared futexv otherwise
 */
static int compat_futex_parse_waitv(struct futex_vector *futexv,
				    struct compat_futex_waitv __user *uwaitv,
				    unsigned int nr_futexes)
{
	struct compat_futex_waitv aux;
	unsigned int i;

	for (i = 0; i < nr_futexes; i++) {
		if (copy_from_user(&aux, &uwaitv[i], sizeof(aux)))
			return -EFAULT;

		if ((aux.flags & ~FUTEXV_WAITER_MASK) ||
		    (aux.flags & FUTEX_SIZE_MASK) != FUTEX_32)
			return -EINVAL;

		futexv[i].w.flags = aux.flags;
		futexv[i].w.val = aux.val;
		futexv[i].w.uaddr = compat_ptr(aux.uaddr);
		futexv[i].q = futex_q_init;
	}

	return 0;
}

COMPAT_SYSCALL_DEFINE4(futex_waitv, struct compat_futex_waitv __user *, waiters,
		       unsigned int, nr_futexes, unsigned int, flags,
		       struct __kernel_timespec __user *, timo)
{
	struct hrtimer_sleeper to;
	struct futex_vector *futexv;
	struct timespec64 ts;
	ktime_t time;
	int ret;

	if (flags & ~FUTEXV_MASK)
		return -EINVAL;

	if (!nr_futexes || nr_futexes > FUTEX_WAITV_MAX || !waiters)
		return -EINVAL;

	if (timo) {
		int flag_clkid = 0;

		if (get_timespec64(&ts, timo))
			return -EFAULT;

		if (!timespec64_valid(&ts))
			return -EINVAL;

		if (flags & FUTEX_CLOCK_REALTIME)
			flag_clkid = FLAGS_CLOCKRT;

		time = timespec64_to_ktime(ts);
		futex_setup_timer(&time, &to, flag_clkid, 0);
	}

	futexv = kcalloc(nr_futexes, sizeof(*futexv), GFP_KERNEL);
	if (!futexv)
		return -ENOMEM;

	ret = compat_futex_parse_waitv(futexv, waiters, nr_futexes);
	if (!ret)
		ret = futex_wait_multiple(futexv, nr_futexes, timo ? &to : NULL);

	if (timo) {
		hrtimer_cancel(&to.timer);
		destroy_hrtimer_on_stack(&to.timer);
	}

	kfree(futexv);
	return ret;
}
#endif

static int futex_parse_waitv(struct futex_vector *futexv,
			     struct futex_waitv __user *uwaitv,
			     unsigned int nr_futexes)
{
	struct futex_waitv aux;
	unsigned int i;

	for (i = 0; i < nr_futexes; i++) {
		if (copy_from_user(&aux, &uwaitv[i], sizeof(aux)))
			return -EFAULT;

		if ((aux.flags & ~FUTEXV_WAITER_MASK) ||
		    (aux.flags & FUTEX_SIZE_MASK) != FUTEX_32)
			return -EINVAL;

		futexv[i].w.flags = aux.flags;
		futexv[i].w.val = aux.val;
		futexv[i].w.uaddr = aux.uaddr;
		futexv[i].q = futex_q_init;
	}

	return 0;
}

SYSCALL_DEFINE4(futex_waitv, struct futex_waitv __user *, waiters,
		unsigned int, nr_futexes, unsigned int, flags,
		struct __kernel_timespec __user *, timo)
{
	struct hrtimer_sleeper to;
	struct futex_vector *futexv;
	struct timespec64 ts;
	ktime_t time;
	int ret;

	if (flags & ~FUTEXV_MASK)
		return -EINVAL;

	if (!nr_futexes || nr_futexes > FUTEX_WAITV_MAX || !waiters)
		return -EINVAL;

	if (timo) {
		int flag_clkid = 0;

		if (get_timespec64(&ts, timo))
			return -EFAULT;

		if (!timespec64_valid(&ts))
			return -EINVAL;

		if (flags & FUTEX_CLOCK_REALTIME)
			flag_clkid = FLAGS_CLOCKRT;

		time = timespec64_to_ktime(ts);
		futex_setup_timer(&time, &to, flag_clkid, 0);
	}

	futexv = kcalloc(nr_futexes, sizeof(*futexv), GFP_KERNEL);
	if (!futexv)
		return -ENOMEM;

	ret = futex_parse_waitv(futexv, waiters, nr_futexes);
	if (!ret)
		ret = futex_wait_multiple(futexv, nr_futexes, timo ? &to : NULL);

	if (timo) {
		hrtimer_cancel(&to.timer);
		destroy_hrtimer_on_stack(&to.timer);
	}

	kfree(futexv);
	return ret;
}

static long ksys_futex_wait(void __user *uaddr, u64 val, unsigned int flags,
			    struct __kernel_timespec __user *timo)
{
	unsigned int size = flags & FUTEX_SIZE_MASK, futex_flags = 0;
	ktime_t *kt = NULL, time;
	struct timespec64 ts;

	if (flags & ~FUTEX2_MASK)
		return -EINVAL;

	if (flags & FUTEX_SHARED_FLAG)
		futex_flags |= FLAGS_SHARED;

	if (flags & FUTEX_CLOCK_REALTIME)
		futex_flags |= FLAGS_CLOCKRT;

	if (size != FUTEX_32)
		return -EINVAL;

	if (timo) {
		if (get_timespec64(&ts, timo))
			return -EFAULT;

		if (!timespec64_valid(&ts))
			return -EINVAL;

		time = timespec64_to_ktime(ts);
		kt = &time;
	}

	return futex_wait(uaddr, futex_flags, val, kt, FUTEX_BITSET_MATCH_ANY);
}

/**
 * sys_futex_wait - Wait on a futex address if (*uaddr) == val
 * @uaddr: User address of futex
 * @val:   Expected value of futex
 * @flags: Specify the size of futex and the clockid
 * @timo:  Optional absolute timeout.
 *
 * The user thread is put to sleep, waiting for a futex_wake() at uaddr, if the
 * value at *uaddr is the same as val (otherwise, the syscall returns
 * immediately with -EAGAIN).
 *
 * Returns 0 on success, error code otherwise.
 */
SYSCALL_DEFINE4(futex_wait, void __user *, uaddr, u64, val, unsigned int, flags,
		struct __kernel_timespec __user *, timo)
{
	return ksys_futex_wait(uaddr, val, flags, timo);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(futex_wait, void __user *, uaddr, compat_u64, val,
		       unsigned int, flags,
		       struct __kernel_timespec __user *, timo)
{
	return ksys_futex_wait(uaddr, val, flags, timo);
}
#endif

long ksys_futex_wake(void __user *uaddr, unsigned int nr_wake,
		     unsigned int flags)
{
	unsigned int size = flags & FUTEX_SIZE_MASK, futex_flags = 0;

	if (flags & ~FUTEX2_MASK)
		return -EINVAL;

	if (flags & FUTEX_SHARED_FLAG)
		futex_flags |= FLAGS_SHARED;

	if (size != FUTEX_32)
		return -EINVAL;

	return futex_wake(uaddr, futex_flags, nr_wake, FUTEX_BITSET_MATCH_ANY);
}

/**
 * sys_futex_wake - Wake a number of futexes waiting on an address
 * @uaddr:   Address of futex to be woken up
 * @nr_wake: Number of futexes waiting in uaddr to be woken up
 * @flags:   Flags for size and shared
 *
 * Wake `nr_wake` threads waiting at uaddr.
 *
 * Returns the number of woken threads on success, error code otherwise.
 */
SYSCALL_DEFINE3(futex_wake, void __user *, uaddr, unsigned int, nr_wake,
		unsigned int, flags)
{
	return ksys_futex_wake(uaddr, nr_wake, flags);
}

#ifdef CONFIG_COMPAT
static int compat_futex_parse_requeue(struct compat_futex_requeue __user *rq,
				      void __user **uaddr, unsigned int *flags)
{
	struct compat_futex_requeue aux;
	unsigned int futex_flags = 0;

	if (copy_from_user(&aux, rq, sizeof(*rq)))
		return -EFAULT;

	if (aux.flags & ~FUTEXV_WAITER_MASK ||
	    (aux.flags & FUTEX_SIZE_MASK) != FUTEX_32)
		return -EINVAL;

	if (aux.flags & FUTEX_SHARED_FLAG)
		futex_flags |= FLAGS_SHARED;

	*uaddr = compat_ptr(aux.uaddr);
	*flags = futex_flags;

	return 0;
}

COMPAT_SYSCALL_DEFINE6(futex_requeue, struct compat_futex_requeue __user *, rq1,
		       struct compat_futex_requeue __user *, rq2,
		       unsigned int, nr_wake, unsigned int, nr_requeue,
		       compat_u64, cmpval, unsigned int, flags)
{
	void __user *uaddr1, *uaddr2;
	unsigned int flags1, flags2;
	u32 val = cmpval;
	int ret;

	if (flags)
		return -EINVAL;

	ret = compat_futex_parse_requeue(rq1, &uaddr1, &flags1);
	if (ret)
		return ret;

	ret = compat_futex_parse_requeue(rq2, &uaddr2, &flags2);
	if (ret)
		return ret;

	return futex_requeue(uaddr1, flags1, uaddr2, flags2, nr_wake, nr_requeue, &val, 0);
}
#endif

static int futex_parse_requeue(struct futex_requeue __user *rq,
			       void __user **uaddr, unsigned int *flags)
{
	struct futex_requeue aux;
	unsigned int futex_flags = 0;

	if (copy_from_user(&aux, rq, sizeof(*rq)))
		return -EFAULT;

	if (aux.flags & ~FUTEXV_WAITER_MASK ||
	    (aux.flags & FUTEX_SIZE_MASK) != FUTEX_32)
		return -EINVAL;

	if (aux.flags & FUTEX_SHARED_FLAG)
		futex_flags |= FLAGS_SHARED;

	*uaddr = aux.uaddr;
	*flags = futex_flags;

	return 0;
}

/**
 * sys_futex_requeue - Wake futexes at rq1 and requeue from rq1 to rq2
 * @rq1:	Address of futexes to be waken/dequeued
 * @rq2:	Address for the futexes to be enqueued
 * @nr_wake:    Number of futexes waiting in uaddr1 to be woken up
 * @nr_requeue: Number of futexes to be requeued from uaddr1 to uaddr2
 * @cmpval:     Expected value at uaddr1
 * @flags:      Reserved flags arg for requeue operation expansion. Must be 0.
 *
 * If (rq1->uaddr == cmpval), wake at uaddr1->uaddr a nr_wake number of
 * waiters and then, remove a number of nr_requeue waiters at rq1->uaddr
 * and add then to rq2->uaddr list. Each uaddr has its own set of flags,
 * that must be defined at struct futex_requeue (such as size, shared, NUMA).
 *
 * Return the number of the woken futexes + the number of requeued ones on
 * success, error code otherwise.
 */
SYSCALL_DEFINE6(futex_requeue, struct futex_requeue __user *, rq1,
		struct futex_requeue __user *, rq2,
		unsigned int, nr_wake, unsigned int, nr_requeue,
		u64, cmpval, unsigned int, flags)
{
	void __user *uaddr1, *uaddr2;
	unsigned int flags1, flags2;
	u32 val = cmpval;
	int ret;

	if (flags)
		return -EINVAL;

	ret = futex_parse_requeue(rq1, &uaddr1, &flags1);
	if (ret)
		return ret;

	ret = futex_parse_requeue(rq2, &uaddr2, &flags2);
	if (ret)
		return ret;

	return futex_requeue(uaddr1, flags1, uaddr2, flags2, nr_wake, nr_requeue, &val, 0);
}
