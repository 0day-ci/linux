// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * futex2 system call interface by André Almeida <andrealmeid@collabora.com>
 *
 * Copyright 2021 Collabora Ltd.
 */

#include <asm/futex.h>

#include <linux/syscalls.h>

/*
 * Set of flags that futex2 accepts
 */
#define FUTEX2_MASK (FUTEX_SIZE_MASK | FUTEX_SHARED_FLAG | FUTEX_CLOCK_REALTIME)

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
	unsigned int size = flags & FUTEX_SIZE_MASK, futex_flags = 0;

	if (flags & ~FUTEX2_MASK)
		return -EINVAL;

	if (flags & FUTEX_SHARED_FLAG)
		futex_flags |= FLAGS_SHARED;

	if (size != FUTEX_32)
		return -EINVAL;

	return futex_wake(uaddr, futex_flags, nr_wake, FUTEX_BITSET_MATCH_ANY);
}
