// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Loongson Technologies, Inc.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include <asm/setup.h>
#include <asm/loongarchregs.h>

static struct irq_domain *irq_domain;

static inline void unmask_loongarch_irq(struct irq_data *d)
{
	set_csr_ecfg(ECFGF(d->hwirq));
}

static inline void mask_loongarch_irq(struct irq_data *d)
{
	clear_csr_ecfg(ECFGF(d->hwirq));
}

#define enable_loongarch_irq  unmask_loongarch_irq
#define disable_loongarch_irq mask_loongarch_irq

static struct irq_chip loongarch_cpu_irq_controller = {
	.name		= "LoongArch",
	.irq_ack	= mask_loongarch_irq,
	.irq_mask	= mask_loongarch_irq,
	.irq_mask_ack	= mask_loongarch_irq,
	.irq_unmask	= unmask_loongarch_irq,
	.irq_eoi	= unmask_loongarch_irq,
	.irq_enable	= enable_loongarch_irq,
	.irq_disable	= disable_loongarch_irq,
};

asmlinkage void __weak plat_irq_dispatch(int irq)
{
	do_IRQ(irq_linear_revmap(irq_domain, irq));
}

static int loongarch_cpu_intc_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct irq_chip *chip;

	chip = &loongarch_cpu_irq_controller;
	set_vi_handler(EXCCODE_INT_START + hwirq, plat_irq_dispatch);
	irq_set_chip_and_handler(irq, chip, handle_percpu_irq);

	return 0;
}

static const struct irq_domain_ops loongarch_cpu_intc_irq_domain_ops = {
	.map = loongarch_cpu_intc_map,
	.xlate = irq_domain_xlate_onecell,
};


int __init loongarch_cpu_irq_init(struct device_node *of_node, struct device_node *parent)
{
	int i;

	/* Mask interrupts. */
	clear_csr_ecfg(ECFG0_IM);
	clear_csr_estat(ESTATF_IP);

	irq_alloc_descs(-1, LOONGSON_CPU_IRQ_BASE, EXCCODE_INT_NUM, 0);

	for (i = LOONGSON_CPU_IRQ_BASE; i <= LOONGSON_CPU_LAST_IRQ; i++)
		irq_set_noprobe(i);

	irq_domain = irq_domain_add_legacy(of_node, EXCCODE_INT_NUM,
		     LOONGSON_CPU_IRQ_BASE, 0, &loongarch_cpu_intc_irq_domain_ops, NULL);

	if (!irq_domain)
		panic("Failed to add irqdomain for loongarch CPU");

	return 0;
}

IRQCHIP_DECLARE(cpu_intc, "loongson,cpu-interrupt-controller", loongarch_cpu_irq_init);
