/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2022 Jisheng Zhang <jszhang@kernel.org>
 */

#ifndef __ASM_CPUFEATURE_H
#define __ASM_CPUFEATURE_H

#include <asm/cpucaps.h>

#include <linux/bug.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>

extern DECLARE_BITMAP(cpu_hwcaps, RISCV_NCAPS);
extern struct static_key_false cpu_hwcap_keys[RISCV_NCAPS];
extern struct static_key_false riscv_const_caps_ready;

static __always_inline bool system_capabilities_finalized(void)
{
	return static_branch_likely(&riscv_const_caps_ready);
}

/*
 * Test for a capability with a runtime check.
 *
 * Before the capability is detected, this returns false.
 */
static inline bool cpus_have_cap(unsigned int num)
{
	if (num >= RISCV_NCAPS)
		return false;
	return test_bit(num, cpu_hwcaps);
}

/*
 * Test for a capability without a runtime check.
 *
 * Before capabilities are finalized, this returns false.
 * After capabilities are finalized, this is patched to avoid a runtime check.
 *
 * @num must be a compile-time constant.
 */
static __always_inline bool __cpus_have_const_cap(int num)
{
	if (num >= RISCV_NCAPS)
		return false;
	return static_branch_unlikely(&cpu_hwcap_keys[num]);
}

/*
 * Test for a capability without a runtime check.
 *
 * Before capabilities are finalized, this will BUG().
 * After capabilities are finalized, this is patched to avoid a runtime check.
 *
 * @num must be a compile-time constant.
 */
static __always_inline bool cpus_have_final_cap(int num)
{
	if (system_capabilities_finalized())
		return __cpus_have_const_cap(num);
	else
		BUG();
}

/*
 * Test for a capability, possibly with a runtime check.
 *
 * Before capabilities are finalized, this behaves as cpus_have_cap().
 * After capabilities are finalized, this is patched to avoid a runtime check.
 *
 * @num must be a compile-time constant.
 */
static __always_inline bool cpus_have_const_cap(int num)
{
	if (system_capabilities_finalized())
		return __cpus_have_const_cap(num);
	else
		return cpus_have_cap(num);
}

static inline void cpus_set_cap(unsigned int num)
{
	if (num >= RISCV_NCAPS) {
		pr_warn("Attempt to set an illegal CPU capability (%d >= %d)\n",
			num, RISCV_NCAPS);
	} else {
		__set_bit(num, cpu_hwcaps);
	}
}

#endif
