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

#define RTL_ICTL_NUM_PRIO	6

#define REG(x)			(realtek_ictl_base + x)

static DEFINE_RAW_SPINLOCK(irq_lock);
static void __iomem *realtek_ictl_base;
static struct irq_domain *realtek_ictl_domain;

struct realtek_ictl_priority {
	unsigned int routing_value;
	u32 child_mask;
};

static struct realtek_ictl_priority priorities[RTL_ICTL_NUM_PRIO];

/*
 * IRR0-IRR3 store 4 bits per interrupt, but Realtek uses inverted
 * numbering, placing IRQ 31 in the first four bits.
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

static irqreturn_t realtek_irq_dispatch(int irq, void *devid)
{
	struct realtek_ictl_priority *priority = devid;
	unsigned long pending;
	int soc_irq;
	int ret = 0;

	pending = readl(REG(RTL_ICTL_GIMR)) & readl(REG(RTL_ICTL_GISR))
		& priority->child_mask;

	for_each_set_bit(soc_irq, &pending, BITS_PER_LONG) {
		generic_handle_domain_irq(realtek_ictl_domain, soc_irq);
		ret = 1;
	}

	return IRQ_RETVAL(ret);
}

static void __init set_routing(struct realtek_ictl_priority *priority, unsigned int soc_int)
{
	unsigned int priority_old;

	priority_old = read_irr(REG(RTL_ICTL_IRR0), soc_int);
	if (priority_old) {
		pr_warn("int %d already routed to %d, not updating\n", soc_int, priority_old);
		return;
	}

	priority->child_mask |= BIT(soc_int);
	write_irr(REG(RTL_ICTL_IRR0), soc_int, priority->routing_value);
}

static int __init setup_parent_interrupt(struct realtek_ictl_priority *prio_ctl, int parent)
{
	struct device_node *parent_node;
	struct irq_data *irqd;
	unsigned int flags;
	int parent_hwirq;

	irqd = irq_get_irq_data(parent);
	if (!irqd)
		return -ENOENT;

	parent_node = to_of_node(irqd->domain->fwnode);
	parent_hwirq = irqd_to_hwirq(irqd);

	flags = IRQF_PERCPU | IRQF_SHARED;
	if (of_device_is_compatible(parent_node, "mti,cpu-interrupt-controller")
		&& parent_hwirq == 7)
		flags |= IRQF_TIMER;

	return request_irq(parent, realtek_irq_dispatch, flags, "rtl-intc", prio_ctl);
}

static int __init map_interrupts(struct device_node *node)
{
	struct realtek_ictl_priority *prio_ctl;
	struct device_node *cpu_ictl;
	const __be32 *imap;
	u32 imaplen, soc_int, priority, tmp;
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

		/* Map priority (1..6) to MIPS CPU interrupt (2..7) */
		priority = be32_to_cpup(imap + 2);
		if (priority > 6 || priority < 1)
			return -EINVAL;

		prio_ctl = &priorities[priority - 1];

		if (!prio_ctl->routing_value) {
			ret = setup_parent_interrupt(prio_ctl, priority + 1);
			if (ret)
				return ret;

			prio_ctl->routing_value = priority;
		}

		set_routing(prio_ctl, soc_int);

		imap += 3;
	}


	return 0;
}

static int __init realtek_rtl_of_init(struct device_node *node, struct device_node *parent)
{
	unsigned int soc_irq;
	int ret;

	memset(&priorities, 0, sizeof(priorities));

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
