/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_FUTEX_SYSCALL_H
#define _UAPI_LINUX_FUTEX_SYSCALL_H

#include <asm/unistd.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/time_types.h>
#include <sys/syscall.h>

/**
 * futex_syscall_timeout() - __NR_futex/__NR_futex_time64 syscall wrapper
 * @uaddr:  address of first futex
 * @op:   futex op code
 * @val:  typically expected value of uaddr, but varies by op
 * @timeout:  an absolute struct timespec
 * @uaddr2: address of second futex for some ops
 * @val3: varies by op
 */
static inline int
__kernel_futex_syscall_timeout(volatile u_int32_t *uaddr, int op, u_int32_t val,
		      struct timespec *timeout, volatile u_int32_t *uaddr2, int val3)
{
#if defined(__NR_futex_time64)
	if (sizeof(*timeout) != sizeof(struct __kernel_old_timespec)) {
		int ret =  syscall(__NR_futex_time64, uaddr, op, val, timeout, uaddr2, val3);

		if (ret == 0 || errno != ENOSYS)
			return ret;
	}
#endif

#if defined(__NR_futex)
	if (sizeof(*timeout) == sizeof(struct __kernel_old_timespec))
		return syscall(__NR_futex, uaddr, op, val, timeout, uaddr2, val3);

	if (timeout && timeout->tv_sec == (long)timeout->tv_sec) {
		struct __kernel_old_timespec ts32;

		ts32.tv_sec = (__kernel_long_t) timeout->tv_sec;
		ts32.tv_nsec = (__kernel_long_t) timeout->tv_nsec;

		return syscall(__NR_futex, uaddr, op, val, &ts32, uaddr2, val3);
	} else if (!timeout) {
		return syscall(__NR_futex, uaddr, op, val, NULL, uaddr2, val3);
	}
#endif

	errno = ENOSYS;
	return -1;
}

/**
 * futex_syscall_nr_requeue() - __NR_futex/__NR_futex_time64 syscall wrapper
 * @uaddr:  address of first futex
 * @op:   futex op code
 * @val:  typically expected value of uaddr, but varies by op
 * @nr_requeue:  an op specific meaning
 * @uaddr2: address of second futex for some ops
 * @val3: varies by op
 */
static inline int
__kernel_futex_syscall_nr_requeue(volatile u_int32_t *uaddr, int op, u_int32_t val,
			 u_int32_t nr_requeue, volatile u_int32_t *uaddr2, int val3)
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

#endif /* _UAPI_LINUX_FUTEX_SYSCALL_H */
