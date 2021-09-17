/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _UAPI_ASM_PTRACE_H
#define _UAPI_ASM_PTRACE_H

#include <linux/types.h>

#ifndef __KERNEL__
#include <stdint.h>
#endif

/* For PTRACE_{POKE,PEEK}USR. 0 - 31 are GPRs, 32 is PC, 33 is BADVADDR. */
#define GPR_BASE	0
#define GPR_NUM		32
#define GPR_END		(GPR_BASE + GPR_NUM - 1)
#define PC		(GPR_END + 1)
#define BADVADDR	(GPR_END + 2)

/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception.
 *
 * If you add a register here, also add it to regoffset_table[] in
 * arch/loongarch/kernel/ptrace.c.
 */
struct pt_regs {
	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Saved special registers. */
	unsigned long csr_crmd;
	unsigned long csr_prmd;
	unsigned long csr_euen;
	unsigned long csr_ecfg;
	unsigned long csr_estat;
	unsigned long csr_epc;
	unsigned long csr_badvaddr;
	unsigned long orig_a0;
	unsigned long __last[0];
} __attribute__((aligned(8)));

#define PTRACE_SYSEMU			0x1f
#define PTRACE_SYSEMU_SINGLESTEP	0x20

#endif /* _UAPI_ASM_PTRACE_H */
