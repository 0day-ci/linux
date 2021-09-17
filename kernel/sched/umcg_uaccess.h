/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _KERNEL_SCHED_UMCG_UACCESS_H
#define _KERNEL_SCHED_UMCG_UACCESS_H

#ifdef CONFIG_X86_64

#include <linux/uaccess.h>

#include <asm/asm.h>
#include <linux/atomic.h>
#include <asm/uaccess.h>

/* TODO: move atomic operations below into arch/ headers */
static inline int __try_cmpxchg_user_32(u32 *uval, u32 __user *uaddr,
						u32 oldval, u32 newval)
{
	int ret = 0;

	asm volatile("\n"
		"1:\t" LOCK_PREFIX "cmpxchgl %4, %2\n"
		"2:\n"
		"\t.section .fixup, \"ax\"\n"
		"3:\tmov     %3, %0\n"
		"\tjmp     2b\n"
		"\t.previous\n"
		_ASM_EXTABLE_UA(1b, 3b)
		: "+r" (ret), "=a" (oldval), "+m" (*uaddr)
		: "i" (-EFAULT), "r" (newval), "1" (oldval)
		: "memory"
	);
	*uval = oldval;
	return ret;
}

static inline int __try_cmpxchg_user_64(u64 *uval, u64 __user *uaddr,
						u64 oldval, u64 newval)
{
	int ret = 0;

	asm volatile("\n"
		"1:\t" LOCK_PREFIX "cmpxchgq %4, %2\n"
		"2:\n"
		"\t.section .fixup, \"ax\"\n"
		"3:\tmov     %3, %0\n"
		"\tjmp     2b\n"
		"\t.previous\n"
		_ASM_EXTABLE_UA(1b, 3b)
		: "+r" (ret), "=a" (oldval), "+m" (*uaddr)
		: "i" (-EFAULT), "r" (newval), "1" (oldval)
		: "memory"
	);
	*uval = oldval;
	return ret;
}

static inline int fix_pagefault(unsigned long uaddr, bool write_fault, int bytes)
{
	struct mm_struct *mm = current->mm;
	int ret;

	/* Validate proper alignment. */
	if (uaddr % bytes)
		return -EINVAL;

	if (mmap_read_lock_killable(mm))
		return -EINTR;
	ret = fixup_user_fault(mm, uaddr, write_fault ? FAULT_FLAG_WRITE : 0,
			NULL);
	mmap_read_unlock(mm);

	return ret < 0 ? ret : 0;
}

/**
 * cmpxchg_32_user_nosleep - compare_exchange 32-bit values
 *
 * Return:
 * 0 - OK
 * -EFAULT: memory access error
 * -EAGAIN: @expected did not match; consult @prev
 */
static inline int cmpxchg_user_32_nosleep(u32 __user *uaddr, u32 *old, u32 new)
{
	int ret = -EFAULT;
	u32 __old = *old;

	if (unlikely(!access_ok(uaddr, sizeof(*uaddr))))
		return -EFAULT;

	pagefault_disable();

	__uaccess_begin_nospec();
	ret = __try_cmpxchg_user_32(old, uaddr, __old, new);
	user_access_end();

	if (!ret)
		ret =  *old == __old ? 0 : -EAGAIN;

	pagefault_enable();
	return ret;
}

/**
 * cmpxchg_64_user_nosleep - compare_exchange 64-bit values
 *
 * Return:
 * 0 - OK
 * -EFAULT: memory access error
 * -EAGAIN: @expected did not match; consult @prev
 */
static inline int cmpxchg_user_64_nosleep(u64 __user *uaddr, u64 *old, u64 new)
{
	int ret = -EFAULT;
	u64 __old = *old;

	if (unlikely(!access_ok(uaddr, sizeof(*uaddr))))
		return -EFAULT;

	pagefault_disable();

	__uaccess_begin_nospec();
	ret = __try_cmpxchg_user_64(old, uaddr, __old, new);
	user_access_end();

	if (!ret)
		ret =  *old == __old ? 0 : -EAGAIN;

	pagefault_enable();

	return ret;
}

/**
 * cmpxchg_32_user - compare_exchange 32-bit values
 *
 * Return:
 * 0 - OK
 * -EFAULT: memory access error
 * -EAGAIN: @expected did not match; consult @prev
 */
static inline int cmpxchg_user_32(u32 __user *uaddr, u32 *old, u32 new)
{
	int ret = -EFAULT;
	u32 __old = *old;

	if (unlikely(!access_ok(uaddr, sizeof(*uaddr))))
		return -EFAULT;

	pagefault_disable();

	while (true) {
		__uaccess_begin_nospec();
		ret = __try_cmpxchg_user_32(old, uaddr, __old, new);
		user_access_end();

		if (!ret) {
			ret =  *old == __old ? 0 : -EAGAIN;
			break;
		}

		if (fix_pagefault((unsigned long)uaddr, true, sizeof(*uaddr)) < 0)
			break;
	}

	pagefault_enable();
	return ret;
}

/**
 * cmpxchg_64_user - compare_exchange 64-bit values
 *
 * Return:
 * 0 - OK
 * -EFAULT: memory access error
 * -EAGAIN: @expected did not match; consult @prev
 */
static inline int cmpxchg_user_64(u64 __user *uaddr, u64 *old, u64 new)
{
	int ret = -EFAULT;
	u64 __old = *old;

	if (unlikely(!access_ok(uaddr, sizeof(*uaddr))))
		return -EFAULT;

	pagefault_disable();

	while (true) {
		__uaccess_begin_nospec();
		ret = __try_cmpxchg_user_64(old, uaddr, __old, new);
		user_access_end();

		if (!ret) {
			ret =  *old == __old ? 0 : -EAGAIN;
			break;
		}

		if (fix_pagefault((unsigned long)uaddr, true, sizeof(*uaddr)) < 0)
			break;
	}

	pagefault_enable();

	return ret;
}

static inline int __try_xchg_user_32(u32 *oval, u32 __user *uaddr, u32 newval)
{
	u32 oldval = 0;
	int ret = 0;

	asm volatile("\n"
		"1:\txchgl %0, %2\n"
		"2:\n"
		"\t.section .fixup, \"ax\"\n"
		"3:\tmov     %3, %0\n"
		"\tjmp     2b\n"
		"\t.previous\n"
		_ASM_EXTABLE_UA(1b, 3b)
		: "=r" (oldval), "=r" (ret), "+m" (*uaddr)
		: "i" (-EFAULT), "0" (newval), "1" (0)
	);

	if (ret)
		return ret;

	*oval = oldval;
	return 0;
}

static inline int __try_xchg_user_64(u64 *oval, u64 __user *uaddr, u64 newval)
{
	u64 oldval = 0;
	int ret = 0;

	asm volatile("\n"
		"1:\txchgq %0, %2\n"
		"2:\n"
		"\t.section .fixup, \"ax\"\n"
		"3:\tmov     %3, %0\n"
		"\tjmp     2b\n"
		"\t.previous\n"
		_ASM_EXTABLE_UA(1b, 3b)
		: "=r" (oldval), "=r" (ret), "+m" (*uaddr)
		: "i" (-EFAULT), "0" (newval), "1" (0)
	);

	if (ret)
		return ret;

	*oval = oldval;
	return 0;
}

/**
 * xchg_32_user - atomically exchange 64-bit values
 *
 * Return:
 * 0 - OK
 * -EFAULT: memory access error
 */
static inline int xchg_user_32(u32 __user *uaddr, u32 *val)
{
	int ret = -EFAULT;

	if (unlikely(!access_ok(uaddr, sizeof(*uaddr))))
		return -EFAULT;

	pagefault_disable();

	while (true) {

		__uaccess_begin_nospec();
		ret = __try_xchg_user_32(val, uaddr, *val);
		user_access_end();

		if (!ret)
			break;

		if (fix_pagefault((unsigned long)uaddr, true, sizeof(*uaddr)) < 0)
			break;
	}

	pagefault_enable();

	return ret;
}

/**
 * xchg_64_user - atomically exchange 64-bit values
 *
 * Return:
 * 0 - OK
 * -EFAULT: memory access error
 */
static inline int xchg_user_64(u64 __user *uaddr, u64 *val)
{
	int ret = -EFAULT;

	if (unlikely(!access_ok(uaddr, sizeof(*uaddr))))
		return -EFAULT;

	pagefault_disable();

	while (true) {

		__uaccess_begin_nospec();
		ret = __try_xchg_user_64(val, uaddr, *val);
		user_access_end();

		if (!ret)
			break;

		if (fix_pagefault((unsigned long)uaddr, true, sizeof(*uaddr)) < 0)
			break;
	}

	pagefault_enable();

	return ret;
}

/**
 * get_user_nosleep - get user value without sleeping.
 *
 * get_user() might sleep and therefore cannot be used in preempt-disabled
 * regions.
 */
#define get_user_nosleep(out, uaddr)			\
({							\
	int ret = -EFAULT;				\
							\
	if (access_ok((uaddr), sizeof(*(uaddr)))) {	\
		pagefault_disable();			\
							\
		if (!__get_user((out), (uaddr)))	\
			ret = 0;			\
							\
		pagefault_enable();			\
	}						\
	ret;						\
})

#endif  /* CONFIG_X86_64 */
#endif  /* _KERNEL_SCHED_UMCG_UACCESS_H */
