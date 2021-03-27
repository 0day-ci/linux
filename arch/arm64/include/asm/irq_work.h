/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_IRQ_WORK_H
#define __ASM_IRQ_WORK_H

extern void arch_irq_work_raise(void);
extern void panic_smp_self_stop(void);

static inline bool arch_irq_work_has_interrupt(void)
{
	return true;
}

#endif /* __ASM_IRQ_WORK_H */
