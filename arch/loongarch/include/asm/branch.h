/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_BRANCH_H
#define _ASM_BRANCH_H

#include <asm/ptrace.h>

static inline unsigned long exception_epc(struct pt_regs *regs)
{
	return regs->csr_epc;
}

static inline int compute_return_epc(struct pt_regs *regs)
{
	regs->csr_epc += 4;
	return 0;
}

#endif /* _ASM_BRANCH_H */
