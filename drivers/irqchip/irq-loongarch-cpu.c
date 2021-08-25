// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include <asm/loongarch.h>
#include <asm/setup.h>

static struct irq_domain *irq_domain;

static inline void enable_loongarch_irq(struct irq_data *d)
{
	set_csr_ecfg(ECFGF(d->hwirq));
}

#define eoi_loongarch_irq enable_loongarch_irq

static inline void disable_loongarch_irq(struct irq_data *d)
{
	clear_csr_ecfg(ECFGF(d->hwirq));
}

#define ack_loongarch_irq disable_loongarch_irq

static struct irq_chip loongarch_cpu_irq_controller = {
	.name		= "LoongArch",
	.irq_ack	= ack_loongarch_irq,
	.irq_eoi	= eoi_loongarch_irq,
	.irq_enable	= enable_loongarch_irq,
	.irq_disable	= disable_loongarch_irq,
};

asmlinkage void default_handle_irq(int irq)
{
	do_IRQ(irq_linear_revmap(irq_domain, irq));
}

static int loongarch_cpu_intc_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct irq_chip *chip;

	irq_set_noprobe(irq);
	chip = &loongarch_cpu_irq_controller;
	set_vi_handler(EXCCODE_INT_START + hwirq, default_handle_irq);
	irq_set_chip_and_handler(irq, chip, handle_percpu_irq);

	return 0;
}

static const struct irq_domain_ops loongarch_cpu_intc_irq_domain_ops = {
	.map = loongarch_cpu_intc_map,
	.xlate = irq_domain_xlate_onecell,
};

int __init loongarch_cpu_irq_init(void)
{
	/* Mask interrupts. */
	clear_csr_ecfg(ECFG0_IM);
	clear_csr_estat(ESTATF_IP);

	irq_domain = irq_domain_add_simple(NULL, EXCCODE_INT_NUM,
		     LOONGSON_CPU_IRQ_BASE, &loongarch_cpu_intc_irq_domain_ops, NULL);

	if (!irq_domain)
		panic("Failed to add irqdomain for LoongArch CPU");

	return 0;
}
