/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/uaccess.h>

#define __get_user_nofault(dst, src, type, err_label)			\
do {									\
	int __gu_err;							\
									\
	__get_user_common(*((type *)(dst)), sizeof(type),		\
			  (__force type *)(src));			\
	if (unlikely(__gu_err))						\
		goto err_label;						\
} while (0)


static inline int __get_addr(unsigned long *a, unsigned long *p, bool user)
{
	if (user)
		__get_user_nofault(a, p, unsigned long, fault);
	else
		__get_kernel_nofault(a, p, unsigned long, fault);

	return 0;

fault:
	return -EFAULT;
}

static inline int __get_inst16(u16 *i, u16 *p, bool user)
{
	if (user)
		__get_user_nofault(i, p, u16, fault);
	else
		__get_kernel_nofault(i, p, u16, fault);

	return 0;

fault:
	return -EFAULT;
}

static inline int __get_inst32(u32 *i, u32 *p, bool user)
{
	if (user)
		__get_user_nofault(i, p, u32, fault);
	else
		__get_kernel_nofault(i, p, u32, fault);

	return 0;

fault:
	return -EFAULT;
}
