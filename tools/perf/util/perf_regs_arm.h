/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __PERF_REGS_ARM_H
#define __PERF_REGS_ARM_H

/*
 * ARM and ARM64 registers are grouped under enums of the same name.
 * Temporarily rename the name of the enum to prevent the naming collision.
 */
#define perf_event_arm_regs perf_event_arm_regs_workaround

#include "../../arch/arm/include/uapi/asm/perf_regs.h"

static inline const char *__perf_reg_name_arm(int id)
{
	switch (id) {
	case PERF_REG_ARM_R0:
		return "r0";
	case PERF_REG_ARM_R1:
		return "r1";
	case PERF_REG_ARM_R2:
		return "r2";
	case PERF_REG_ARM_R3:
		return "r3";
	case PERF_REG_ARM_R4:
		return "r4";
	case PERF_REG_ARM_R5:
		return "r5";
	case PERF_REG_ARM_R6:
		return "r6";
	case PERF_REG_ARM_R7:
		return "r7";
	case PERF_REG_ARM_R8:
		return "r8";
	case PERF_REG_ARM_R9:
		return "r9";
	case PERF_REG_ARM_R10:
		return "r10";
	case PERF_REG_ARM_FP:
		return "fp";
	case PERF_REG_ARM_IP:
		return "ip";
	case PERF_REG_ARM_SP:
		return "sp";
	case PERF_REG_ARM_LR:
		return "lr";
	case PERF_REG_ARM_PC:
		return "pc";
	default:
		return NULL;
	}

	return NULL;
}

#undef perf_event_arm_regs

#endif /* __PERF_REGS_ARM_H */
