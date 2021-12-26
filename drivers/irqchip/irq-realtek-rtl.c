// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Birger Koblitz <mail@birger-koblitz.de>
 * Copyright (C) 2020 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2020 John Crispin <john@phrozen.org>
 */

#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/of_address.h>
#include <linux/irqchip/chained_irq.h>

/* Global Interrupt Mask Register */
#define RTL_ICTL_GIMR		0x00
/* Global Interrupt Status Register */
#define RTL_ICTL_GISR		0x04
/* Interrupt Routing Registers */
#define RTL_ICTL_IRR0		0x08
#define RTL_ICTL_IRR1		0x0c
#define RTL_ICTL_IRR2		0x10
#define RTL_ICTL_IRR3		0x14

#define RTL_ICTL_NUM_OUTPUTS	6

#define REG(x)		(realtek_ictl_base + x)

static DEFINE_RAW_SPINLOCK(irq_lock);
static void __iomem *realtek_ictl_base;
static struct irq_domain *realtek_ictl_domain;

struct realtek_ictl_output {
	unsigned int routing_value;
	u32 child_mask;
};

static struct realtek_ictl_output realtek_ictl_outputs[RTL_ICTL_NUM_OUTPUTS];

/*
 * IRR0-IRR3 store 4 bits per interrupt, but Realtek uses inverted numbering,
 * placing IRQ 31 in the first four bits. A routing value of '0' means the
 * interrupt is left disconnected. Routing values {1..15} connect to output
 * lines {0..14}.
 */
#define IRR_OFFSET(idx)		(4 * (3 - (idx * 4) / 32))
#define IRR_SHIFT(idx)		((idx * 4) % 32)

static inline u32 read_irr(void __iomem *irr0, int idx)
{
	return (readl(irr0 + IRR_OFFSET(idx)) >> IRR_SHIFT(idx)) & 0xf;
}

static inline void write_irr(void __iomem *irr0, int idx, u32 value)
{
	unsigned int offset = IRR_OFFSET(idx);
	unsigned int shift = IRR_SHIFT(idx);
	u32 irr;

	irr = readl(irr0 + offset) & ~(0xf << shift);
	irr |= (value & 0xf) << shift;
	writel(irr, irr0 + offset);
}

static void realtek_ictl_unmask_irq(struct irq_data *i)
{
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&irq_lock, flags);

	value = readl(REG(RTL_ICTL_GIMR));
	value |= BIT(i->hwirq);
	writel(value, REG(RTL_ICTL_GIMR));

	raw_spin_unlock_irqrestore(&irq_lock, flags);
}

static void realtek_ictl_mask_irq(struct irq_data *i)
{
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&irq_lock, flags);

	value = readl(REG(RTL_ICTL_GIMR));
	value &= ~BIT(i->hwirq);
	writel(value, REG(RTL_ICTL_GIMR));

	raw_spin_unlock_irqrestore(&irq_lock, flags);
}

static struct irq_chip realtek_ictl_irq = {
	.name = "realtek-rtl-intc",
	.irq_mask = realtek_ictl_mask_irq,
	.irq_unmask = realtek_ictl_unmask_irq,
};

static int intc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &realtek_ictl_irq, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
	.map = intc_map,
	.xlate = irq_domain_xlate_onecell,
};

static void realtek_irq_dispatch(struct irq_desc *desc)
{
	struct realtek_ictl_output *parent = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int pending;

	chained_irq_enter(chip, desc);
	pending = readl(REG(RTL_ICTL_GIMR)) & readl(REG(RTL_ICTL_GISR))
		& parent->child_mask;

	if (unlikely(!pending)) {
		spurious_interrupt();
		goto out;
	}
	generic_handle_domain_irq(realtek_ictl_domain, __ffs(pending));

out:
	chained_irq_exit(chip, desc);
}

static void __init set_routing(struct realtek_ictl_output *output, unsigned int soc_int)
{
	unsigned int routing_old;

	routing_old = read_irr(REG(RTL_ICTL_IRR0), soc_int);
	if (routing_old) {
		pr_warn("int %d already routed to %d, not updating\n", soc_int, routing_old);
		return;
	}

	output->child_mask |= BIT(soc_int);
	write_irr(REG(RTL_ICTL_IRR0), soc_int, output->routing_value);
}

static int __init map_interrupts(struct device_node *node)
{
	struct realtek_ictl_output *output;
	struct device_node *cpu_ictl;
	const __be32 *imap;
	u32 imaplen, soc_int, cpu_int, tmp;
	int ret, i;

	ret = of_property_read_u32(node, "#address-cells", &tmp);
	if (ret || tmp)
		return -EINVAL;

	imap = of_get_property(node, "interrupt-map", &imaplen);
	if (!imap || imaplen % 3)
		return -EINVAL;

	for (i = 0; i < imaplen; i += 3 * sizeof(u32)) {
		soc_int = be32_to_cpup(imap);
		if (soc_int > 31)
			return -EINVAL;

		cpu_ictl = of_find_node_by_phandle(be32_to_cpup(imap + 1));
		if (!cpu_ictl)
			return -EINVAL;
		ret = of_property_read_u32(cpu_ictl, "#interrupt-cells", &tmp);
		if (ret || tmp != 1)
			return -EINVAL;
		of_node_put(cpu_ictl);

		cpu_int = be32_to_cpup(imap + 2);
		if (cpu_int > 7 || cpu_int < 2)
			return -EINVAL;

		output = &realtek_ictl_outputs[cpu_int - 2];

		if (!output->routing_value) {
			irq_set_chained_handler_and_data(cpu_int, realtek_irq_dispatch, output);
			/* Use routing values (1..6) for CPU interrupts (2..7) */
			output->routing_value = cpu_int - 1;
		}

		set_routing(output, soc_int);

		imap += 3;
	}

	return 0;
}

static int __init realtek_rtl_of_init(struct device_node *node, struct device_node *parent)
{
	unsigned int soc_irq;
	int ret;

	memset(&realtek_ictl_outputs, 0, sizeof(realtek_ictl_outputs));

	realtek_ictl_base = of_iomap(node, 0);
	if (!realtek_ictl_base)
		return -ENXIO;

	/* Disable all cascaded interrupts */
	writel(0, REG(RTL_ICTL_GIMR));
	for (soc_irq = 0; soc_irq < 32; soc_irq++)
		write_irr(REG(RTL_ICTL_IRR0), soc_irq, 0);

	realtek_ictl_domain = irq_domain_add_simple(node, 32, 0, &irq_domain_ops, NULL);

	ret = map_interrupts(node);
	if (ret) {
		pr_err("invalid interrupt map\n");
		return ret;
	}

	return 0;
}

IRQCHIP_DECLARE(realtek_rtl_intc, "realtek,rtl-intc", realtek_rtl_of_init);
