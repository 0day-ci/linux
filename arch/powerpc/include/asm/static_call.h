/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_STATIC_CALL_H
#define _ASM_POWERPC_STATIC_CALL_H

#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)			\
	asm(".pushsection .text, \"ax\"				\n"	\
	    ".align 4						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    "	b " #func "					\n"	\
	    "	nop						\n"	\
	    "	nop						\n"	\
	    "	nop						\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)			\
	asm(".pushsection .text, \"ax\"				\n"	\
	    ".align 4						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    "	blr						\n"	\
	    "	nop						\n"	\
	    "	nop						\n"	\
	    "	nop						\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#endif /* _ASM_POWERPC_STATIC_CALL_H */
