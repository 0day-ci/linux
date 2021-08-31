/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_STATIC_CALL_H
#define _ASM_POWERPC_STATIC_CALL_H

#define __POWERPC_SCT(name, inst)					\
	asm(".pushsection .text, \"ax\"				\n"	\
	    ".align 5						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    inst "						\n"	\
	    "	lis	12,1f@ha				\n"	\
	    "	lwz	12,1f@l(12)				\n"	\
	    "	mtctr	12					\n"	\
	    "	bctr						\n"	\
	    "1:	.long 0						\n"	\
	    "	nop						\n"	\
	    "	nop						\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)	__POWERPC_SCT(name, "b " #func)
#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)	__POWERPC_SCT(name, "blr")

#endif /* _ASM_POWERPC_STATIC_CALL_H */
