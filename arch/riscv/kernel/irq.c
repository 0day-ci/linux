// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2018 Christoph Hellwig
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/sbi.h>

static struct fwnode_handle *intc_fwnode;

struct fwnode_handle *riscv_intc_fwnode(void)
{
	if (!intc_fwnode)
		intc_fwnode = irq_domain_alloc_named_fwnode("RISCV-INTC");

	return intc_fwnode;
}
EXPORT_SYMBOL_GPL(riscv_intc_fwnode);

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_stats(p, prec);
	return 0;
}

void __init init_IRQ(void)
{
	irqchip_init();
	if (!handle_arch_irq)
		panic("No interrupt controller found.");
	sbi_ipi_init();
}
