// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nintendo Wii U "Espresso" interrupt controller support
 * Copyright (C) 2022 The linux-wiiu Team
 *
 * Based on flipper-pic.c
 * Copyright (C) 2004-2009 The GameCube Linux Team
 * Copyright (C) 2007-2009 Albert Herranz
 */

#define DRV_MODULE_NAME "espresso-pic"
#define pr_fmt(fmt) DRV_MODULE_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include "espresso-pic.h"

static DEFINE_PER_CPU(struct espresso_pic *, espresso_pic_cpu);

/*
 * IRQ chip operations
 */

static void espresso_pic_mask_and_ack(struct irq_data *d)
{
	struct espresso_pic *pic = *this_cpu_ptr(&espresso_pic_cpu);
	u32 mask = 1 << irqd_to_hwirq(d);

	out_be32(&pic->icr, mask);
	clrbits32(&pic->imr, mask);
}

static void espresso_pic_ack(struct irq_data *d)
{
	struct espresso_pic *pic = *this_cpu_ptr(&espresso_pic_cpu);
	u32 mask = 1 << irqd_to_hwirq(d);

	out_be32(&pic->icr, mask);
}

static void espresso_pic_mask(struct irq_data *d)
{
	struct espresso_pic *pic = *this_cpu_ptr(&espresso_pic_cpu);
	u32 mask = 1 << irqd_to_hwirq(d);

	clrbits32(&pic->imr, mask);
}

static void espresso_pic_unmask(struct irq_data *d)
{
	struct espresso_pic *pic = *this_cpu_ptr(&espresso_pic_cpu);
	u32 mask = 1 << irqd_to_hwirq(d);

	setbits32(&pic->imr, mask);
}

static struct irq_chip espresso_pic_chip = {
	.name = "espresso-pic",
	.irq_ack = espresso_pic_ack,
	.irq_mask_ack = espresso_pic_mask_and_ack,
	.irq_mask = espresso_pic_mask,
	.irq_unmask = espresso_pic_unmask,
};

/*
 * Domain Ops
 */

static int espresso_pic_match(struct irq_domain *h, struct device_node *node,
			      enum irq_domain_bus_token bus_token)
{
	if (h->fwnode == &node->fwnode) {
		pr_debug("espresso-pic: %s IRQ matches with this driver\n",
			 node->name);
		return 1;
	}
	return 0;
}

static int espresso_pic_alloc(struct irq_domain *h, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	unsigned int i;
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq = fwspec->param[0];

	for (i = 0; i < nr_irqs; i++) {
		irq_set_chip_data(virq + i, h->host_data);
		irq_set_status_flags(virq + i, IRQ_LEVEL);
		irq_set_chip_and_handler(virq + i, &espresso_pic_chip,
					 handle_level_irq);
		irq_domain_set_hwirq_and_chip(h, virq + i, hwirq + i,
					      &espresso_pic_chip, h->host_data);
	}
	return 0;
}

static void espresso_pic_free(struct irq_domain *h, unsigned int virq,
			      unsigned int nr_irqs)
{
	pr_debug("free\n");
}

const struct irq_domain_ops espresso_pic_ops = {
	.match = espresso_pic_match,
	.alloc = espresso_pic_alloc,
	.free = espresso_pic_free,
};

/* Store irq domain for espresso_pic_get_irq (the function gets no arguments) */
static struct irq_domain *espresso_irq_domain;

unsigned int espresso_pic_get_irq(void)
{
	struct espresso_pic *pic = *this_cpu_ptr(&espresso_pic_cpu);
	u32 irq_status, irq;

	irq_status = in_be32(&pic->icr) & in_be32(&pic->imr);

	if (irq_status == 0)
		return 0; /* No IRQs pending */

	/* Return first IRQ */
	irq = __ffs(irq_status);
	return irq_linear_revmap(espresso_irq_domain, irq);
}

void __init espresso_pic_init(void)
{
	struct device_node *np =
		of_find_compatible_node(NULL, NULL, "nintendo,espresso-pic");
	struct irq_domain *host;
	struct resource res;
	void __iomem *regbase;
	unsigned int cpu;

	if (!np) {
		pr_err("could not find device node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &res) != 0) {
		pr_err("could not find resource address\n");
		goto out;
	}

	regbase = ioremap(res.start, resource_size(&res));
	if (IS_ERR(regbase)) {
		pr_err("could not map controller\n");
		goto out;
	}

	for_each_present_cpu(cpu) {
		struct espresso_pic **pic = per_cpu_ptr(&espresso_pic_cpu, cpu);

		/* Compute pic address */
		*pic = regbase + (sizeof(struct espresso_pic) * cpu);

		/* Mask and Ack all IRQs */
		out_be32(&(*pic)->imr, 0);
		out_be32(&(*pic)->icr, 0xFFFFFFFF);
	}

	host = irq_domain_add_linear(np, ESPRESSO_NR_IRQS, &espresso_pic_ops,
				     NULL);
	if (!host) {
		pr_err("failed to allocate irq_domain\n");
		goto out;
	}

	/* Save irq domain for espresso_pic_get_irq */
	espresso_irq_domain = host;

	irq_set_default_host(host);

out:
	of_node_put(np);
}
