/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UACCESS_64_H
#define _ASM_X86_UACCESS_64_H

/*
 * User space memory access functions
 */
#include <linux/compiler.h>
#include <linux/lockdep.h>
#include <linux/kasan-checks.h>
#include <asm/alternative.h>
#include <asm/cpufeatures.h>
#include <asm/page.h>

/*
 * Copy To/From Userspace
 */

/* Handles exceptions in both to and from, but doesn't do access_ok */
__must_check unsigned long
copy_user_enhanced_fast_string(void *to, const void *from, unsigned len);
__must_check unsigned long
copy_user_generic_string(void *to, const void *from, unsigned len);
__must_check unsigned long
copy_user_generic_unrolled(void *to, const void *from, unsigned len);

static __always_inline __must_check unsigned long
copy_user_generic(void *to, const void *from, unsigned len)
{
	unsigned ret;

	/*
	 * If CPU has ERMS feature, use copy_user_enhanced_fast_string.
	 * Otherwise, if CPU has rep_good feature, use copy_user_generic_string.
	 * Otherwise, use copy_user_generic_unrolled.
	 */
	alternative_call_2(copy_user_generic_unrolled,
			 copy_user_generic_string,
			 X86_FEATURE_REP_GOOD,
			 copy_user_enhanced_fast_string,
			 X86_FEATURE_ERMS,
			 ASM_OUTPUT2("=a" (ret), "=D" (to), "=S" (from),
				     "=d" (len)),
			 "1" (to), "2" (from), "3" (len)
			 : "memory", "rcx", "r8", "r9", "r10", "r11");
	return ret;
}

static __always_inline __must_check unsigned long
raw_copy_from_user(void *dst, const void __user *src, unsigned long size)
{
	return copy_user_generic(dst, (__force void *)src, size);
}

static __always_inline __must_check unsigned long
raw_copy_to_user(void __user *dst, const void *src, unsigned long size)
{
	return copy_user_generic((__force void *)dst, src, size);
}

extern long __copy_user_nocache(void *dst, const void __user *src,
				unsigned size, int zerorest);

extern long __copy_user_flushcache(void *dst, const void __user *src, unsigned size);
extern void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
			   size_t len);

static inline int
__copy_from_user_inatomic_nocache(void *dst, const void __user *src,
				  unsigned size)
{
	kasan_check_write(dst, size);
	return __copy_user_nocache(dst, src, size, 0);
}

static inline int
__copy_from_user_flushcache(void *dst, const void __user *src, unsigned size)
{
	kasan_check_write(dst, size);
	return __copy_user_flushcache(dst, src, size);
}

#define ARCH_HAS_ATOMIC_UACCESS_HELPERS 1

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

static inline int __try_xchg_user_32(u32 *oval, u32 __user *uaddr, u32 newval)
{
	u32 oldval = 0;
	int ret = 0;

	asm volatile("\n"
		"1:\txchgl %0, %2\n"
		"2:\n"
		"\t.section .fixup, \"ax\"\n"
		"3:\tmov     %3, %1\n"
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
		"3:\tmov     %3, %1\n"
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

#endif /* _ASM_X86_UACCESS_64_H */
