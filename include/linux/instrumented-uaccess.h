/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This header provides generic wrappers for memory access instrumentation for
 * uaccess routines that the compiler cannot emit for: KASAN, KCSAN,
 * uaccess buffers.
 */
#ifndef _LINUX_INSTRUMENTED_UACCESS_H
#define _LINUX_INSTRUMENTED_UACCESS_H

#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/kcsan-checks.h>
#include <linux/types.h>
#include <linux/uaccess-buffer.h>

/**
 * instrument_copy_to_user - instrument reads of copy_to_user
 *
 * Instrument reads from kernel memory, that are due to copy_to_user (and
 * variants). The instrumentation must be inserted before the accesses.
 *
 * @to destination address
 * @from source address
 * @n number of bytes to copy
 */
static __always_inline void
instrument_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	kasan_check_read(from, n);
	kcsan_check_read(from, n);
	uaccess_buffer_log_write(to, n);
}

/**
 * instrument_copy_from_user - instrument writes of copy_from_user
 *
 * Instrument writes to kernel memory, that are due to copy_from_user (and
 * variants). The instrumentation should be inserted before the accesses.
 *
 * @to destination address
 * @from source address
 * @n number of bytes to copy
 */
static __always_inline void
instrument_copy_from_user(const void *to, const void __user *from, unsigned long n)
{
	kasan_check_write(to, n);
	kcsan_check_write(to, n);
	uaccess_buffer_log_read(from, n);
}

#endif /* _LINUX_INSTRUMENTED_UACCESS_H */
