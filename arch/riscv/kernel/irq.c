// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2018 Christoph Hellwig
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/seq_file.h>
#include <asm/smp.h>

void *irq_stack[NR_CPUS];

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_stats(p, prec);
	return 0;
}

void __init init_IRQ(void)
{
	int cpu;

	irqchip_init();
	if (!handle_arch_irq)
		panic("No interrupt controller found.");

	for_each_possible_cpu(cpu) {
#ifdef CONFIG_VMAP_STACK
		void *s = __vmalloc_node(IRQ_STACK_SIZE, THREAD_ALIGN,
					 THREADINFO_GFP, cpu_to_node(cpu),
					 __builtin_return_address(0));
#else
		void *s = (void *)__get_free_pages(GFP_KERNEL, get_order(IRQ_STACK_SIZE));
#endif

		irq_stack[cpu] = s;
	}
}
