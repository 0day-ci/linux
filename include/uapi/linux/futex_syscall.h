/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Futex syscall helper functions
 *
 * Copyright (C) 2021 Western Digital.  All Rights Reserved.
 *
 * Author: Alistair Francis <alistair.francis@wdc.com>
 */
#ifndef _UAPI_LINUX_FUTEX_SYSCALL_H
#define _UAPI_LINUX_FUTEX_SYSCALL_H

#include <unistd.h>
#include <errno.h>
#include <linux/futex.h>
#include <linux/types.h>
#include <linux/time_types.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>

/**
 * __kernel_futex_syscall_timeout() - __NR_futex/__NR_futex_time64 syscall wrapper
 * @uaddr:  address of first futex
 * @op:   futex op code
 * @val:  typically expected value of uaddr, but varies by op
 * @timeout:  an absolute struct timespec
 * @uaddr2: address of second futex for some ops
 * @val3: varies by op
 */
static inline int
__kernel_futex_syscall_timeout(volatile uint32_t *uaddr, int op, uint32_t val,
		      struct timespec *timeout, volatile uint32_t *uaddr2, int val3)
{
#if defined(__NR_futex_time64)
	if (sizeof(*timeout) != sizeof(struct __kernel_old_timespec)) {
		int ret = syscall(__NR_futex_time64, uaddr, op, val, timeout, uaddr2, val3);

		if (ret == 0 || errno != ENOSYS)
			return ret;
	}
#endif

#if defined(__NR_futex)
	if (sizeof(*timeout) == sizeof(struct __kernel_old_timespec))
		return syscall(__NR_futex, uaddr, op, val, timeout, uaddr2, val3);

	if (timeout && timeout->tv_sec == (long)timeout->tv_sec) {
		struct __kernel_old_timespec ts_old;

		ts_old.tv_sec = (__kernel_long_t) timeout->tv_sec;
		ts_old.tv_nsec = (__kernel_long_t) timeout->tv_nsec;

		return syscall(__NR_futex, uaddr, op, val, &ts_old, uaddr2, val3);
	} else if (!timeout) {
		return syscall(__NR_futex, uaddr, op, val, NULL, uaddr2, val3);
	}
#endif

	errno = ENOSYS;
	return -1;
}

/**
 * __kernel_futex_syscall_nr_requeue() - __NR_futex/__NR_futex_time64 syscall wrapper
 * @uaddr:  address of first futex
 * @op:   futex op code
 * @val:  typically expected value of uaddr, but varies by op
 * @nr_requeue:  an op specific meaning
 * @uaddr2: address of second futex for some ops
 * @val3: varies by op
 */
static inline int
__kernel_futex_syscall_nr_requeue(volatile uint32_t *uaddr, int op, uint32_t val,
			 uint32_t nr_requeue, volatile uint32_t *uaddr2, int val3)
{
#if defined(__NR_futex_time64)
	int ret =  syscall(__NR_futex_time64, uaddr, op, val, nr_requeue, uaddr2, val3);

	if (ret == 0 || errno != ENOSYS)
		return ret;
#endif

#if defined(__NR_futex)
	return syscall(__NR_futex, uaddr, op, val, nr_requeue, uaddr2, val3);
#endif

	errno = ENOSYS;
	return -1;
}

/**
 * __kernel_futex_syscall_waitv - Wait at multiple futexes, wake on any
 * @waiters:    Array of waiters
 * @nr_waiters: Length of waiters array
 * @flags: Operation flags
 * @timo:  Optional timeout for operation
 */
static inline int
__kernel_futex_syscall_waitv(volatile struct futex_waitv *waiters, unsigned long nr_waiters,
			      unsigned long flags, struct timespec *timo, clockid_t clockid)
{
	/* futex_waitv expects a 64-bit time_t */
	if (sizeof(*timo) == sizeof(struct __kernel_timespec))
		return syscall(__NR_futex_waitv, waiters, nr_waiters, flags, timo, clockid);

	/* If the caller supplied a 32-bit time_t, convert it to 64-bit */
	if (timo) {
		struct __kernel_timespec ts_new;

		ts_new.tv_sec = timo->tv_sec;
		ts_new.tv_nsec = timo->tv_nsec;

		return syscall(__NR_futex_waitv, waiters, nr_waiters, flags, &ts_new, clockid);
	} else
		return syscall(__NR_futex_waitv, waiters, nr_waiters, flags, NULL, clockid);
}

#endif /* _UAPI_LINUX_FUTEX_SYSCALL_H */
