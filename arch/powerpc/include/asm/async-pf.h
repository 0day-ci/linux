/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Async page fault support via PAPR Expropriation/Subvention Notification
 * option(ESN)
 *
 * Copyright 2020 Bharata B Rao, IBM Corp. <bharata@linux.ibm.com>
 */

#ifndef _ASM_POWERPC_ASYNC_PF_H
int handle_async_page_fault(struct pt_regs *regs, unsigned long addr);
#define _ASM_POWERPC_ASYNC_PF_H
#endif
