/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_COMPILER_H
#define __ASM_COMPILER_H

#ifdef ARM64_ASM_ARCH
#define ARM64_ASM_PREAMBLE ".arch " ARM64_ASM_ARCH "\n"
#else
#define ARM64_ASM_PREAMBLE
#endif

/* Open-code TCR_TBID0 value to avoid circular dependency. */
#define tcr_tbid0_enabled() (init_tcr & (1UL << 51))

/*
 * The EL0/EL1 pointer bits used by a pointer authentication code.
 * This is dependent on TBI0/TBI1 being enabled, or bits 63:56 would also apply.
 */
#define ptrauth_user_insn_pac_mask()                                           \
	(tcr_tbid0_enabled() ? GENMASK_ULL(63, vabits_actual) :                \
				     GENMASK_ULL(54, vabits_actual))
#define ptrauth_user_data_pac_mask()	GENMASK_ULL(54, vabits_actual)
#define ptrauth_kernel_pac_mask()	GENMASK_ULL(63, vabits_actual)

/* Valid for EL0 TTBR0 and EL1 TTBR1 instruction pointers */
#define ptrauth_clear_insn_pac(ptr)                                            \
	((ptr & BIT_ULL(55)) ? (ptr | ptrauth_kernel_pac_mask()) :             \
				     (ptr & ~ptrauth_user_insn_pac_mask()))

#define __builtin_return_address(val)                                          \
	((void *)(ptrauth_clear_insn_pac(                                      \
		(unsigned long)__builtin_return_address(val))))

#endif /* __ASM_COMPILER_H */
