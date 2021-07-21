/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_POWERPC_FPU_API_H
#define _ASM_POWERPC_FPU_API_H

/*
 * Use kernel_fpu_begin/end() if you intend to use FPU in kernel context. It
 * disables preemption so be careful if you intend to use it for long periods
 * of time.
 * TODO: If you intend to use the FPU in irq/softirq you need to check first with
 * irq_fpu_usable() if it is possible.
 */

extern bool kernel_fpu_enabled(void);
extern void kernel_fpu_begin(void);
extern void kernel_fpu_end(void);

#endif /* _ASM_POWERPC_FPU_API_H */
